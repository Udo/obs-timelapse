#include "capture-session.hpp"

#include "core-utils.hpp"
#include "mkv-writer.hpp"
#include "png-writer.hpp"

#include <obs-module.h>
#include <obs.h>
#include <plugin-support.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace timelapse {
namespace {

constexpr auto ManifestFileName = "session.json";

QString frameFileName(uint64_t sequence)
{
	return QStringLiteral("frame_%1.png").arg(sequence, 8, 10, QLatin1Char('0'));
}

} // namespace

QString sessionStateName(SessionState state)
{
	switch (state) {
	case SessionState::Starting:
		return QStringLiteral("Starting");
	case SessionState::Running:
		return QStringLiteral("Running");
	case SessionState::Stopping:
		return QStringLiteral("Stopping");
	case SessionState::Failed:
		return QStringLiteral("Failed");
	case SessionState::Stopped:
		return QStringLiteral("Stopped");
	}
	return QStringLiteral("Unknown");
}

CaptureSession::CaptureSession(CaptureSettings settings) : settings_(std::move(settings)) {}

CaptureSession::~CaptureSession()
{
	stop(QStringLiteral("plugin shutdown"));
}

QString CaptureSession::createUniqueSessionPath(const QString &baseName) const
{
	const QDir outputRoot(settings_.outputDirectory);
	QString candidate = outputRoot.filePath(baseName);
	for (int suffix = 2; QFileInfo::exists(candidate); ++suffix)
		candidate = outputRoot.filePath(baseName + QStringLiteral("-%1").arg(suffix));
	return candidate;
}

bool CaptureSession::start(QString &error)
{
	if (state_.load() != SessionState::Stopped) {
		error = QStringLiteral("The capture session has already been started.");
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(stopMutex_);
		if (finalized_) {
			error = QStringLiteral("A finished capture session cannot be restarted.");
			return false;
		}
	}

	state_.store(SessionState::Starting);
	try {
		error = validateSettings(settings_);
		if (!error.isEmpty())
			throw std::runtime_error(error.toStdString());
		obs_video_info videoInfo{};
		if (!obs_get_video_info(&videoInfo) || videoInfo.output_width == 0 || videoInfo.output_height == 0 ||
		    videoInfo.fps_num == 0 || videoInfo.fps_den == 0) {
			error = QStringLiteral("OBS video output is not initialized.");
			throw std::runtime_error(error.toStdString());
		}

		width_ = videoInfo.output_width;
		height_ = videoInfo.output_height;
		if (width_ > std::numeric_limits<uint32_t>::max() / 4) {
			error = QStringLiteral("OBS output width is too large to capture safely.");
			throw std::runtime_error(error.toStdString());
		}
		rowBytes_ = width_ * 4;
		const uint64_t frameBytes = static_cast<uint64_t>(rowBytes_) * height_;
		if (frameBytes == 0 || frameBytes > std::numeric_limits<std::size_t>::max()) {
			error = QStringLiteral("OBS output dimensions are too large to capture safely.");
			throw std::runtime_error(error.toStdString());
		}

		QDir outputRoot(settings_.outputDirectory);
		if (!outputRoot.exists() && !QDir().mkpath(settings_.outputDirectory)) {
			error = QStringLiteral("Could not create the output directory: %1")
					.arg(settings_.outputDirectory);
			throw std::runtime_error(error.toStdString());
		}

		startedAt_ = QDateTime::currentDateTimeUtc();
		sessionPath_ = createUniqueSessionPath(makeSessionName(settings_.namePrefix, startedAt_));
		if (!QDir().mkpath(sessionPath_)) {
			error = QStringLiteral("Could not create the session directory: %1").arg(sessionPath_);
			throw std::runtime_error(error.toStdString());
		}
		if (settings_.mode == OutputMode::Mkv) {
			mkvWriter_ = std::make_unique<MkvWriter>();
			const QDir sessionDirectory(sessionPath_);
			const QString partialPath = sessionDirectory.filePath(QStringLiteral("timelapse.mkv.partial"));
			const QString finalPath = sessionDirectory.filePath(QStringLiteral("timelapse.mkv"));
			if (!mkvWriter_->open(partialPath, finalPath, width_, height_, settings_.playbackFps, error))
				throw std::runtime_error(error.toStdString());
		}

		frameDivisor_ = calculateFrameDivisor(settings_.intervalSeconds, videoInfo.fps_num, videoInfo.fps_den);
		effectiveIntervalSeconds_ =
			calculateEffectiveInterval(frameDivisor_, videoInfo.fps_num, videoInfo.fps_den);

		availableFrames_.reserve(settings_.queueCapacity);
		pendingFrames_.reserve(settings_.queueCapacity);
		for (std::size_t index = 0; index < settings_.queueCapacity; ++index) {
			auto frame = std::make_unique<Frame>();
			frame->pixels.resize(static_cast<std::size_t>(frameBytes));
			availableFrames_.push_back(std::move(frame));
		}

		QString manifestError;
		if (!writeManifest(QString(), false, manifestError)) {
			error = manifestError;
			throw std::runtime_error(error.toStdString());
		}

		stopRequested_ = false;
		worker_ = std::thread(&CaptureSession::workerLoop, this);
		state_.store(SessionState::Running);
		accepting_.store(true);
		{
			std::lock_guard<std::mutex> lock(callbackMutex_);
			callbacksEnabled_ = true;
		}

		video_scale_info conversion{};
		conversion.format = VIDEO_FORMAT_BGRA;
		conversion.width = width_;
		conversion.height = height_;
		conversion.range = VIDEO_RANGE_FULL;
		conversion.colorspace = VIDEO_CS_SRGB;
		obs_add_raw_video_callback2(&conversion, frameDivisor_, &CaptureSession::rawVideoCallback, this);
		subscribed_ = true;

		obs_log(LOG_INFO, "%s capture started: path='%s', interval=%.3fs (divisor=%u), size=%ux%u, queue=%zu",
			settings_.mode == OutputMode::Png ? "PNG" : "MKV", sessionPath_.toUtf8().constData(),
			effectiveIntervalSeconds_, frameDivisor_, width_, height_, settings_.queueCapacity);
		return true;
	} catch (const std::exception &exception) {
		if (error.isEmpty())
			error = QString::fromUtf8(exception.what());
	} catch (...) {
		error = QStringLiteral("Unexpected failure while starting the capture session.");
	}

	setFailure(error);
	stop(QStringLiteral("start failed"));
	return false;
}

void CaptureSession::rawVideoCallback(void *parameter, video_data *frame) noexcept
{
	if (!parameter || !frame)
		return;

	auto *session = static_cast<CaptureSession *>(parameter);
	bool entered = false;
	try {
		{
			std::lock_guard<std::mutex> lock(session->callbackMutex_);
			if (!session->callbacksEnabled_)
				return;
			++session->callbacksInFlight_;
			entered = true;
		}
		session->receiveFrame(*frame);
	} catch (...) {
		session->droppedFrames_.fetch_add(1);
	}

	if (entered) {
		try {
			std::lock_guard<std::mutex> lock(session->callbackMutex_);
			if (--session->callbacksInFlight_ == 0)
				session->callbackReady_.notify_all();
		} catch (...) {
		}
	}
}

void CaptureSession::receiveFrame(const video_data &frame) noexcept
{
	if (!accepting_.load(std::memory_order_relaxed) || !frame.data[0] || frame.linesize[0] < rowBytes_) {
		droppedFrames_.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	std::unique_ptr<Frame> destination;
	{
		std::lock_guard<std::mutex> lock(queueMutex_);
		if (!accepting_.load(std::memory_order_relaxed) || availableFrames_.empty()) {
			droppedFrames_.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		destination = std::move(availableFrames_.back());
		availableFrames_.pop_back();
	}

	for (uint32_t row = 0; row < height_; ++row) {
		std::memcpy(destination->pixels.data() + static_cast<std::size_t>(row) * rowBytes_,
			    frame.data[0] + static_cast<std::size_t>(row) * frame.linesize[0], rowBytes_);
	}

	{
		std::lock_guard<std::mutex> lock(queueMutex_);
		if (!accepting_.load()) {
			availableFrames_.push_back(std::move(destination));
			droppedFrames_.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		destination->sourceTimestamp = frame.timestamp;
		destination->sequence = acceptedFrames_.fetch_add(1, std::memory_order_relaxed) + 1;
		pendingFrames_.push_back(std::move(destination));
	}
	queueReady_.notify_one();
}

bool CaptureSession::writePng(const Frame &frame, QString &error) const
{
	const QString finalPath = QDir(sessionPath_).filePath(frameFileName(frame.sequence));
	QString writerError;
	if (!writePngAtomically(finalPath, frame.pixels.data(), width_, height_, rowBytes_, settings_.pngCompression,
				writerError)) {
		error = QStringLiteral("Could not write PNG frame %1: %2").arg(frame.sequence).arg(writerError);
		return false;
	}
	return true;
}

void CaptureSession::workerLoop() noexcept
{
	try {
		for (;;) {
			std::unique_ptr<Frame> frame;
			{
				std::unique_lock<std::mutex> lock(queueMutex_);
				queueReady_.wait(lock, [this] { return stopRequested_ || !pendingFrames_.empty(); });
				if (pendingFrames_.empty()) {
					if (stopRequested_)
						break;
					continue;
				}
				frame = std::move(pendingFrames_.front());
				pendingFrames_.erase(pendingFrames_.begin());
			}

			QString error;
			const bool written = settings_.mode == OutputMode::Png
						     ? writePng(*frame, error)
						     : mkvWriter_->writeFrame(frame->pixels.data(), rowBytes_, error);
			if (!written) {
				setFailure(error);
				accepting_.store(false);
			}
			if (written)
				writtenFrames_.fetch_add(1);

			{
				std::lock_guard<std::mutex> lock(queueMutex_);
				availableFrames_.push_back(std::move(frame));
				if (state_.load() == SessionState::Failed) {
					while (!pendingFrames_.empty()) {
						availableFrames_.push_back(std::move(pendingFrames_.front()));
						pendingFrames_.erase(pendingFrames_.begin());
						droppedFrames_.fetch_add(1);
					}
				}
			}
		}

		if (mkvWriter_ && status().error.isEmpty()) {
			QString error;
			if (!mkvWriter_->finish(error))
				setFailure(error);
		}
	} catch (const std::exception &exception) {
		setFailure(QStringLiteral("Capture worker failed: %1").arg(QString::fromUtf8(exception.what())));
	} catch (...) {
		setFailure(QStringLiteral("Capture worker failed unexpectedly."));
	}
}

bool CaptureSession::writeManifest(const QString &stopReason, bool complete, QString &error) const
{
	if (sessionPath_.isEmpty())
		return true;

	QJsonObject manifest;
	manifest.insert(QStringLiteral("schema_version"), 1);
	manifest.insert(QStringLiteral("plugin_version"), QString::fromUtf8(PLUGIN_VERSION));
	manifest.insert(QStringLiteral("mode"), QString::fromUtf8(outputModeName(settings_.mode)));
	manifest.insert(QStringLiteral("started_at_utc"), startedAt_.toString(Qt::ISODateWithMs));
	manifest.insert(QStringLiteral("complete"), complete);
	manifest.insert(QStringLiteral("stop_reason"), stopReason);
	manifest.insert(QStringLiteral("width"), static_cast<int>(width_));
	manifest.insert(QStringLiteral("height"), static_cast<int>(height_));
	manifest.insert(QStringLiteral("requested_interval_seconds"), settings_.intervalSeconds);
	manifest.insert(QStringLiteral("effective_interval_seconds"), effectiveIntervalSeconds_);
	manifest.insert(QStringLiteral("frame_divisor"), static_cast<qint64>(frameDivisor_));
	manifest.insert(QStringLiteral("accepted_frames"), static_cast<qint64>(acceptedFrames_.load()));
	manifest.insert(QStringLiteral("written_frames"), static_cast<qint64>(writtenFrames_.load()));
	manifest.insert(QStringLiteral("dropped_frames"), static_cast<qint64>(droppedFrames_.load()));
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		manifest.insert(QStringLiteral("error"), error_);
	}

	QSaveFile file(QDir(sessionPath_).filePath(QString::fromUtf8(ManifestFileName)));
	if (!file.open(QIODevice::WriteOnly)) {
		error = QStringLiteral("Could not open the session manifest for writing: %1").arg(file.errorString());
		return false;
	}
	if (file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
		error = QStringLiteral("Could not save the session manifest: %1").arg(file.errorString());
		return false;
	}
	return true;
}

void CaptureSession::setFailure(const QString &error) noexcept
{
	try {
		{
			std::lock_guard<std::mutex> lock(statusMutex_);
			if (error_.isEmpty())
				error_ = error;
		}
		state_.store(SessionState::Failed);
		obs_log(LOG_ERROR, "capture session failed: %s", error.toUtf8().constData());
	} catch (...) {
		state_.store(SessionState::Failed);
	}
}

SessionStatus CaptureSession::stop(const QString &reason) noexcept
{
	try {
		std::lock_guard<std::mutex> stopLock(stopMutex_);
		if (finalized_)
			return status();

		const SessionState previousState = state_.load();
		if (previousState != SessionState::Stopped && previousState != SessionState::Failed)
			state_.store(SessionState::Stopping);

		accepting_.store(false);
		{
			std::lock_guard<std::mutex> lock(callbackMutex_);
			callbacksEnabled_ = false;
		}
		if (subscribed_) {
			obs_remove_raw_video_callback(&CaptureSession::rawVideoCallback, this);
			subscribed_ = false;
		}
		{
			std::unique_lock<std::mutex> lock(callbackMutex_);
			callbackReady_.wait(lock, [this] { return callbacksInFlight_ == 0; });
		}

		{
			std::lock_guard<std::mutex> lock(queueMutex_);
			stopRequested_ = true;
		}
		queueReady_.notify_all();
		if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
			worker_.join();

		QString manifestError;
		const bool failed = !status().error.isEmpty();
		if (!writeManifest(reason, !failed, manifestError) && !failed)
			setFailure(manifestError);

		if (status().error.isEmpty())
			state_.store(SessionState::Stopped);
		else
			state_.store(SessionState::Failed);

		if (!sessionPath_.isEmpty()) {
			obs_log(LOG_INFO, "capture stopped: path='%s', written=%llu, dropped=%llu, reason='%s'",
				sessionPath_.toUtf8().constData(),
				static_cast<unsigned long long>(writtenFrames_.load()),
				static_cast<unsigned long long>(droppedFrames_.load()), reason.toUtf8().constData());
		}
		finalized_ = true;
	} catch (...) {
		state_.store(SessionState::Failed);
		finalized_ = true;
	}
	return status();
}

SessionStatus CaptureSession::status() const
{
	SessionStatus result;
	result.state = state_.load();
	result.acceptedFrames = acceptedFrames_.load();
	result.writtenFrames = writtenFrames_.load();
	result.droppedFrames = droppedFrames_.load();
	result.effectiveIntervalSeconds = effectiveIntervalSeconds_;
	result.mode = settings_.mode;
	result.playbackFps = settings_.playbackFps;
	result.startedAt = startedAt_;
	result.outputPath = sessionPath_;
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		result.error = error_;
	}
	return result;
}

} // namespace timelapse
