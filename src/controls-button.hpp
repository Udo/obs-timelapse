#pragma once

class QLabel;
class QPushButton;
class QWidget;

namespace timelapse {

struct ControlsWidgets {
	QWidget *row = nullptr;
	QPushButton *button = nullptr;
	QLabel *counter = nullptr;
};

ControlsWidgets injectControlsWidgets(QWidget *mainWindow) noexcept;

} // namespace timelapse
