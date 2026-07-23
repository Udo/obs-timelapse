#include "controller.hpp"

#include "controls-button.hpp"
#include "timelapse-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace timelapse {
namespace {

QString elapsedClock(const QDateTime &startedAt)
{
	const qint64 total =
		startedAt.isValid() ? std::max<qint64>(0, startedAt.secsTo(QDateTime::currentDateTimeUtc())) : 0;
	const qint64 hours = total / 3600;
	return hours > 0 ? QStringLiteral("%1:%2:%3")
				   .arg(hours)
				   .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
				   .arg(total % 60, 2, 10, QLatin1Char('0'))
			 : QStringLiteral("%1:%2")
				   .arg(total / 60, 2, 10, QLatin1Char('0'))
				   .arg(total % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

Controller::Controller()
	: mainWindow_(static_cast<QWidget *>(obs_frontend_get_main_window())),
	  settings_(loadSettings())
{
	maintenanceTimer_ = std::make_unique<QTimer>();
	maintenanceTimer_->setInterval(250);
	QObject::connect(maintenanceTimer_.get(), &QTimer::timeout, [this] {
		maintainSession();
		refreshControlsButton();
	});
	maintenanceTimer_->start();
}

Controller::~Controller()
{
	shutdown();
}

void Controller::showDialog()
{
	if (shuttingDown_)
		return;
	if (dialog_) {
		dialog_->show();
		dialog_->raise();
		dialog_->activateWindow();
		return;
	}

	auto *dialog = new TimelapseDialog(*this, mainWindow_);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog_ = dialog;
	dialog->show();
}

void Controller::installControlsButton() noexcept
{
	try {
		if (shuttingDown_ || controlsButton_)
			return;
		const ControlsWidgets widgets = injectControlsWidgets(mainWindow_);
		controlsRow_ = widgets.row;
		controlsButton_ = widgets.button;
		controlsPauseButton_ = widgets.pauseButton;
		controlsCounter_ = widgets.counter;
		if (!controlsRow_ || !controlsButton_ || !controlsPauseButton_ || !controlsCounter_)
			return;
		QObject::connect(controlsButton_, &QPushButton::clicked, [this] { toggleControlsCapture(); });
		QObject::connect(controlsPauseButton_, &QPushButton::clicked, [this] { toggleControlsPause(); });
		refreshControlsButton();
	} catch (...) {
		// A changed private OBS widget tree must leave the optional button absent.
		if (controlsRow_)
			delete controlsRow_.data();
		controlsRow_ = nullptr;
		controlsButton_ = nullptr;
		controlsPauseButton_ = nullptr;
		controlsCounter_ = nullptr;
	}
}

void Controller::toggleControlsCapture()
{
	if (stopping_)
		return;
	if (active()) {
		stop(QStringLiteral("stopped from Controls dock"));
	} else {
		QString error;
		if (!start(settings_, error)) {
			QMessageBox::critical(controlsButton_, obs_module_text("Timelapse.ErrorTitle"), error);
			showDialog();
		}
	}
	refreshControlsButton();
}

void Controller::toggleControlsPause()
{
	if (!session_ || stopping_)
		return;
	session_->setPaused(!session_->status().paused);
	refreshControlsButton();
}

void Controller::refreshControlsButton()
{
	if (!controlsButton_ || !controlsPauseButton_ || !controlsCounter_)
		return;
	const SessionStatus current = status();
	const bool showCounter = active() && current.startedAt.isValid();
	if (stopping_) {
		controlsButton_->setText(obs_module_text("Timelapse.ControlsStopping"));
	} else if (current.state == SessionState::Starting) {
		controlsButton_->setText(obs_module_text("Timelapse.ControlsStarting"));
	} else {
		controlsButton_->setText(obs_module_text(active() ? "Timelapse.Stop" : "Timelapse.Start"));
	}
	controlsButton_->setAccessibleName(controlsButton_->text());
	if (active()) {
		controlsButton_->setToolTip(QString::fromUtf8(obs_module_text("Timelapse.ControlsActiveHelp"))
						    .arg(current.writtenFrames)
						    .arg(current.droppedFrames));
	} else if (current.writtenFrames > 0 || current.droppedFrames > 0) {
		controlsButton_->setToolTip(QString::fromUtf8(obs_module_text("Timelapse.ControlsLastHelp"))
						    .arg(current.writtenFrames)
						    .arg(current.droppedFrames));
	} else {
		controlsButton_->setToolTip(obs_module_text("Timelapse.StartStopHelp"));
	}
	controlsButton_->setEnabled(!stopping_);
	controlsButton_->setChecked(active());
	controlsPauseButton_->setText(obs_module_text(current.paused ? "Timelapse.Resume" : "Timelapse.Pause"));
	controlsPauseButton_->setToolTip(
		obs_module_text(current.paused ? "Timelapse.ResumeHelp" : "Timelapse.PauseHelp"));
	controlsPauseButton_->setAccessibleName(controlsPauseButton_->text());
	controlsPauseButton_->setVisible(active());
	controlsPauseButton_->setEnabled(active() && !stopping_);
	controlsCounter_->setVisible(showCounter);
	if (showCounter) {
		const QString elapsed = elapsedClock(current.startedAt);
		controlsCounter_->setText(QString::fromUtf8(obs_module_text("Timelapse.ControlsCounterFormat"))
						  .arg(current.writtenFrames)
						  .arg(elapsed));
		controlsCounter_->setToolTip(QString::fromUtf8(obs_module_text("Timelapse.ControlsCounterHelp"))
						     .arg(current.writtenFrames)
						     .arg(elapsed)
						     .arg(current.droppedFrames));
	}
}

bool Controller::updateSettings(const CaptureSettings &settings, QString &error)
{
	error = validateSettings(settings);
	if (!error.isEmpty())
		return false;
	if (!saveSettings(settings, error))
		return false;
	settings_ = settings;
	return true;
}

bool Controller::start(const CaptureSettings &settings, QString &error)
{
	if (shuttingDown_) {
		error = QStringLiteral("OBS is shutting down.");
		return false;
	}
	if (stopping_) {
		error = QStringLiteral("The previous timelapse session is still being finalized.");
		return false;
	}
	if (session_) {
		error = QStringLiteral("A timelapse session is already active.");
		return false;
	}

	error = validateSettings(settings);
	if (!error.isEmpty())
		return false;

	auto session = std::make_unique<CaptureSession>(settings);
	if (!session->start(error)) {
		lastStatus_ = session->status();
		return false;
	}
	// Save only after output initialization succeeds, so a bad path cannot replace saved-good settings.
	if (!updateSettings(settings, error)) {
		lastStatus_ = session->stop(QStringLiteral("configuration save failed"));
		return false;
	}
	session_ = std::move(session);
	lastStatus_ = session_->status();
	return true;
}

void Controller::stop(const QString &reason)
{
	if (!session_ || stopping_)
		return;

	// Finalizing joins the writer thread and can take a while (draining the
	// frame queue, flushing the encoder); keep that off the OBS UI thread.
	stopping_ = true;
	stopDone_.store(false);
	stopThread_ = std::thread([this, reason] {
		session_->stop(reason);
		stopDone_.store(true);
	});
}

void Controller::reapStopThread()
{
	if (stopThread_.joinable())
		stopThread_.join();
	if (session_) {
		lastStatus_ = session_->status();
		session_.reset();
	}
	stopping_ = false;
}

void Controller::maintainSession()
{
	if (stopping_) {
		if (stopDone_.load())
			reapStopThread();
		return;
	}
	if (!session_)
		return;
	if (session_->status().state == SessionState::Failed)
		stop(QStringLiteral("output failure"));
}

void Controller::shutdown() noexcept
{
	if (shuttingDown_)
		return;
	shuttingDown_ = true;
	try {
		if (maintenanceTimer_)
			maintenanceTimer_->stop();
		if (dialog_) {
			QString error;
			if (!dialog_->persistSettings(error) && !error.isEmpty())
				obs_log(LOG_WARNING, "could not save dialog settings during shutdown: %s",
					error.toUtf8().constData());
			delete dialog_.data();
		}
		if (controlsRow_)
			delete controlsRow_.data();
		controlsRow_ = nullptr;
		controlsButton_ = nullptr;
		controlsPauseButton_ = nullptr;
		controlsCounter_ = nullptr;
		if (stopping_)
			reapStopThread();
		if (session_) {
			lastStatus_ = session_->stop(QStringLiteral("OBS shutdown"));
			session_.reset();
		}
		maintenanceTimer_.reset();
	} catch (...) {
		obs_log(LOG_ERROR, "unexpected exception while shutting down OBS Timelapse");
	}
}

bool Controller::active() const
{
	return session_ != nullptr;
}

bool Controller::stopping() const
{
	return stopping_;
}

SessionStatus Controller::status() const
{
	return session_ ? session_->status() : lastStatus_;
}

CaptureSettings Controller::settings() const
{
	return settings_;
}

} // namespace timelapse
