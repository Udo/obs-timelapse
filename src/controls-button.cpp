#include "controls-button.hpp"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

namespace timelapse {
ControlsWidgets injectControlsWidgets(QWidget *mainWindow) noexcept
{
	QPointer<QWidget> row;
	try {
		if (!mainWindow)
			return {};

		auto *dock = mainWindow->findChild<QDockWidget *>(QStringLiteral("controlsDock"));
		if (!dock || !dock->widget())
			return {};
		auto *controlsFrame = dock->widget()->findChild<QWidget *>(QStringLiteral("controlsFrame"));
		if (!controlsFrame)
			return {};
		auto *layout = qobject_cast<QVBoxLayout *>(controlsFrame->layout());
		if (!layout || layout->objectName() != QStringLiteral("buttonsVLayout") || layout->count() < 1)
			return {};

		row = new QWidget(controlsFrame);
		auto *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->setSpacing(2);
		auto *button = new QPushButton(row);
		button->setObjectName(QStringLiteral("obsTimelapseButton"));
		button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
		button->setAutoDefault(false);
		button->setCheckable(true);
		auto *pauseButton = new QPushButton(row);
		pauseButton->setObjectName(QStringLiteral("obsTimelapsePauseButton"));
		pauseButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
		pauseButton->setAutoDefault(false);
		pauseButton->setVisible(false);
		auto *counter = new QLabel(row);
		counter->setFixedWidth(86);
		counter->setAlignment(Qt::AlignCenter);
		counter->setVisible(false);
		rowLayout->addWidget(button, 1);
		rowLayout->addWidget(pauseButton, 1);
		rowLayout->addWidget(counter);
		layout->insertWidget(layout->count() - 1, row);
		return {row, button, pauseButton, counter};
	} catch (...) {
		if (row)
			delete row.data();
		return {};
	}
}

} // namespace timelapse
