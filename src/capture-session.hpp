#pragma once

#include "settings.hpp"

#include <media-io/video-io.h>

#include <QDateTime>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace timelapse {

class MkvWriter;

enum class SessionState { Starting, Running, Stopping, Stopped, Failed };

struct SessionStatus {
	SessionState state = SessionState::Stopped;
	uint64_t acceptedFrames = 0;
	uint64_t writtenFrames = 0;
	uint64_t droppedFrames = 0;
	double effectiveIntervalSeconds = 0.0;
	OutputMode mode = OutputMode::Png;
	int playbackFps = 0;
	QDateTime startedAt;
	QString outputPath;
	QString error;
	bool paused = false;
};

class CaptureSession final {
public:
	explicit CaptureSession(CaptureSettings settings);
	~CaptureSession();

	CaptureSession(const CaptureSession &) = delete;
	CaptureSession &operator=(const CaptureSession &) = delete;

	bool start(QString &error);
	void setPaused(bool paused) noexcept;
	SessionStatus stop(const QString &reason) noexcept;
	SessionStatus status() const;

private:
	struct Frame {
		std::vector<uint8_t> pixels;
		uint64_t sourceTimestamp = 0;
		uint64_t sequence = 0;
	};

	static void rawVideoCallback(void *parameter, video_data *frame) noexcept;
	void receiveFrame(const video_data &frame);
	void workerLoop() noexcept;
	bool writePng(const Frame &frame, QString &error) const;
	bool writeManifest(const QString &stopReason, bool complete, QString &error) const;
	void setFailure(const QString &error) noexcept;
	QString createUniqueSessionPath(const QString &baseName) const;

	const CaptureSettings settings_;
	QDateTime startedAt_;
	QString sessionPath_;
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	uint32_t rowBytes_ = 0;
	uint32_t frameDivisor_ = 1;
	double effectiveIntervalSeconds_ = 0.0;

	std::atomic<SessionState> state_{SessionState::Stopped};
	std::atomic<bool> accepting_{false};
	std::atomic<bool> paused_{false};
	std::atomic<uint64_t> acceptedFrames_{0};
	std::atomic<uint64_t> writtenFrames_{0};
	std::atomic<uint64_t> droppedFrames_{0};
	bool subscribed_ = false;
	std::mutex callbackMutex_;
	std::condition_variable callbackReady_;
	bool callbacksEnabled_ = false;
	std::size_t callbacksInFlight_ = 0;

	mutable std::mutex statusMutex_;
	QString error_;
	std::mutex stopMutex_;
	bool finalized_ = false;

	std::mutex queueMutex_;
	std::condition_variable queueReady_;
	std::vector<std::unique_ptr<Frame>> availableFrames_;
	std::deque<std::unique_ptr<Frame>> pendingFrames_;
	bool stopRequested_ = false;
	std::thread worker_;
	std::unique_ptr<MkvWriter> mkvWriter_;
};


} // namespace timelapse
