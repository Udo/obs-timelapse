#pragma once

#include "capture-session.hpp"

#include <QPointer>

#include <atomic>
#include <memory>
#include <thread>

class QDialog;
class QLabel;
class QPushButton;
class QTimer;
class QWidget;

namespace timelapse {

class Controller final {
public:
	Controller();
	~Controller();

	Controller(const Controller &) = delete;
	Controller &operator=(const Controller &) = delete;

	void showDialog();
	void installControlsButton() noexcept;
	bool start(const CaptureSettings &settings, QString &error);
	void stop(const QString &reason);
	void shutdown() noexcept;
	bool active() const;
	bool stopping() const;
	SessionStatus status() const;
	CaptureSettings settings() const;

private:
	void maintainSession();
	void reapStopThread();
	void toggleControlsCapture();
	void refreshControlsButton();

	QWidget *mainWindow_ = nullptr;
	QPointer<QDialog> dialog_;
	QPointer<QWidget> controlsRow_;
	QPointer<QPushButton> controlsButton_;
	QPointer<QLabel> controlsCounter_;
	std::unique_ptr<QTimer> maintenanceTimer_;
	std::unique_ptr<CaptureSession> session_;
	SessionStatus lastStatus_;
	CaptureSettings settings_;
	std::thread stopThread_;
	std::atomic<bool> stopDone_{false};
	bool stopping_ = false;
	bool shuttingDown_ = false;
};

} // namespace timelapse
