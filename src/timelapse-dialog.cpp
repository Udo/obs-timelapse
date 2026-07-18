#include "timelapse-dialog.hpp"

#include "controller.hpp"
#include "core-utils.hpp"

#include <obs-module.h>

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <array>

namespace timelapse {
namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString stateStyleSheet(SessionState state)
{
	switch (state) {
	case SessionState::Starting:
	case SessionState::Stopping:
		return QStringLiteral("color: #e0a800; font-weight: bold;");
	case SessionState::Running:
		return QStringLiteral("color: #4caf50; font-weight: bold;");
	case SessionState::Failed:
		return QStringLiteral("color: #d9534f; font-weight: bold;");
	case SessionState::Stopped:
		break;
	}
	return {};
}

} // namespace

TimelapseDialog::TimelapseDialog(Controller &controller, QWidget *parent) : QDialog(parent), controller_(controller)
{
	setWindowTitle(text("Timelapse.WindowTitle"));
	setMinimumWidth(620);

	outputDirectory_ = new QLineEdit(this);
	outputDirectory_->setToolTip(text("Timelapse.OutputDirectoryHelp"));
	browseButton_ = new QPushButton(text("Timelapse.Browse"), this);
	browseButton_->setAutoDefault(false);
	browseButton_->setToolTip(text("Timelapse.BrowseHelp"));
	auto *outputRow = new QWidget(this);
	auto *outputLayout = new QHBoxLayout(outputRow);
	outputLayout->setContentsMargins(0, 0, 0, 0);
	outputLayout->addWidget(outputDirectory_, 1);
	outputLayout->addWidget(browseButton_);

	namePrefix_ = new QLineEdit(this);
	namePrefix_->setPlaceholderText(text("Timelapse.NamePrefixPlaceholder"));
	namePrefix_->setToolTip(text("Timelapse.NamePrefixHelp"));
	mode_ = new QComboBox(this);
	mode_->addItem(text("Timelapse.ModePng"), static_cast<int>(OutputMode::Png));
	mode_->addItem(text("Timelapse.ModeMkv"), static_cast<int>(OutputMode::Mkv));
	mode_->setToolTip(text("Timelapse.OutputModeHelp"));

	interval_ = new QDoubleSpinBox(this);
	interval_->setRange(0.1, 86400.0);
	interval_->setDecimals(1);
	interval_->setSuffix(text("Timelapse.SecondsSuffix"));
	interval_->setToolTip(text("Timelapse.IntervalHelp"));

	playbackFps_ = new QSpinBox(this);
	playbackFps_->setRange(1, 60);
	playbackFps_->setSuffix(text("Timelapse.FpsSuffix"));
	playbackFps_->setMinimumWidth(180);
	playbackFps_->setToolTip(text("Timelapse.PlaybackFpsHelp"));
	pngCompression_ = new QSpinBox(this);
	pngCompression_->setRange(0, 9);
	pngCompression_->setMinimumWidth(180);
	pngCompression_->setToolTip(text("Timelapse.PngCompressionHelp"));
	queueCapacity_ = new QSpinBox(this);
	queueCapacity_->setRange(2, 32);
	queueCapacity_->setMinimumWidth(180);
	queueCapacity_->setToolTip(text("Timelapse.QueueHelp"));

	estimateValue_ = new QLabel(this);
	estimateValue_->setWordWrap(true);
	estimateValue_->setStyleSheet(QStringLiteral("color: gray;"));

	auto *settingsGroup = new QGroupBox(text("Timelapse.SettingsGroup"), this);
	auto *form = new QFormLayout(settingsGroup);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->addRow(text("Timelapse.OutputDirectory"), outputRow);
	form->addRow(text("Timelapse.NamePrefix"), namePrefix_);
	form->addRow(text("Timelapse.OutputMode"), mode_);
	form->addRow(text("Timelapse.Interval"), interval_);
	form->addRow(text("Timelapse.PlaybackFps"), playbackFps_);
	form->addRow(text("Timelapse.PngCompression"), pngCompression_);
	form->addRow(text("Timelapse.QueueCapacity"), queueCapacity_);
	form->addRow(estimateValue_);

	stateValue_ = new QLabel(this);
	countsValue_ = new QLabel(this);
	timingValue_ = new QLabel(this);
	pathValue_ = new QLabel(this);
	pathValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	pathValue_->setWordWrap(true);
	openFolderButton_ = new QPushButton(text("Timelapse.OpenFolder"), this);
	openFolderButton_->setAutoDefault(false);
	openFolderButton_->setEnabled(false);
	openFolderButton_->setToolTip(text("Timelapse.OpenFolderHelp"));
	auto *pathRow = new QWidget(this);
	auto *pathLayout = new QHBoxLayout(pathRow);
	pathLayout->setContentsMargins(0, 0, 0, 0);
	pathLayout->addWidget(pathValue_, 1);
	pathLayout->addWidget(openFolderButton_, 0, Qt::AlignTop);
	errorValue_ = new QLabel(this);
	errorValue_->setWordWrap(true);
	errorValue_->setStyleSheet(QStringLiteral("color: #d9534f;"));

	auto *statusGroup = new QGroupBox(text("Timelapse.StatusGroup"), this);
	auto *statusForm = new QFormLayout(statusGroup);
	statusForm->addRow(text("Timelapse.State"), stateValue_);
	statusForm->addRow(text("Timelapse.FrameCounts"), countsValue_);
	statusForm->addRow(text("Timelapse.Timing"), timingValue_);
	statusForm->addRow(text("Timelapse.CurrentOutput"), pathRow);
	statusForm->addRow(text("Timelapse.LastError"), errorValue_);

	backgroundHint_ = new QLabel(text("Timelapse.BackgroundHint"), this);
	backgroundHint_->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
	backgroundHint_->setVisible(false);

	startStopButton_ = new QPushButton(this);
	startStopButton_->setAutoDefault(false);
	startStopButton_->setToolTip(text("Timelapse.StartStopHelp"));
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	buttons->addButton(startStopButton_, QDialogButtonBox::ActionRole);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(settingsGroup);
	layout->addWidget(statusGroup);
	layout->addWidget(backgroundHint_);
	layout->addWidget(buttons);

	connect(browseButton_, &QPushButton::clicked, this, &TimelapseDialog::browseOutputDirectory);
	connect(openFolderButton_, &QPushButton::clicked, this, &TimelapseDialog::openOutputFolder);
	connect(startStopButton_, &QPushButton::clicked, this, &TimelapseDialog::toggleCapture);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	connect(mode_, &QComboBox::currentIndexChanged, this, &TimelapseDialog::updateModeControls);
	connect(interval_, &QDoubleSpinBox::valueChanged, this, &TimelapseDialog::updateEstimate);
	connect(playbackFps_, &QSpinBox::valueChanged, this, &TimelapseDialog::updateEstimate);

	statusTimer_ = new QTimer(this);
	statusTimer_->setInterval(250);
	connect(statusTimer_, &QTimer::timeout, this, &TimelapseDialog::refreshStatus);
	statusTimer_->start();

	setFormSettings(controller_.settings());
	refreshStatus();
}

CaptureSettings TimelapseDialog::formSettings() const
{
	CaptureSettings settings;
	settings.outputDirectory = outputDirectory_->text().trimmed();
	settings.namePrefix = namePrefix_->text();
	settings.mode = static_cast<OutputMode>(mode_->currentData().toInt());
	settings.intervalSeconds = interval_->value();
	settings.playbackFps = playbackFps_->value();
	settings.pngCompression = pngCompression_->value();
	settings.queueCapacity = static_cast<std::size_t>(queueCapacity_->value());
	return settings;
}

void TimelapseDialog::setFormSettings(const CaptureSettings &settings)
{
	outputDirectory_->setText(settings.outputDirectory);
	namePrefix_->setText(settings.namePrefix);
	const int modeIndex = mode_->findData(static_cast<int>(settings.mode));
	mode_->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);
	interval_->setValue(settings.intervalSeconds);
	playbackFps_->setValue(settings.playbackFps);
	pngCompression_->setValue(settings.pngCompression);
	queueCapacity_->setValue(static_cast<int>(settings.queueCapacity));
}

void TimelapseDialog::browseOutputDirectory()
{
	const QString selected = QFileDialog::getExistingDirectory(this, text("Timelapse.ChooseOutputDirectory"),
								   outputDirectory_->text());
	if (!selected.isEmpty())
		outputDirectory_->setText(selected);
}

void TimelapseDialog::openOutputFolder()
{
	const QString path = controller_.status().outputPath;
	if (!path.isEmpty())
		QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void TimelapseDialog::toggleCapture()
{
	if (controller_.stopping())
		return;
	if (controller_.active()) {
		controller_.stop(QStringLiteral("stopped by user"));
		refreshStatus();
		return;
	}

	QString error;
	if (!controller_.start(formSettings(), error))
		showError(error);
	refreshStatus();
}

QString TimelapseDialog::timingText(const SessionStatus &status, bool active) const
{
	const bool mkv = status.mode == OutputMode::Mkv && status.playbackFps > 0;
	const double videoSeconds = mkv ? static_cast<double>(status.writtenFrames) / status.playbackFps : 0.0;

	if (active && status.startedAt.isValid()) {
		const double elapsed = status.startedAt.msecsTo(QDateTime::currentDateTimeUtc()) / 1000.0;
		QString result = text("Timelapse.TimingActive")
					 .arg(formatDuration(elapsed))
					 .arg(formatDuration(status.effectiveIntervalSeconds));
		if (mkv)
			result = text("Timelapse.TimingVideo").arg(result, formatDuration(videoSeconds));
		return result;
	}
	if (mkv && status.writtenFrames > 0)
		return text("Timelapse.TimingFinal").arg(formatDuration(videoSeconds));
	return text("Timelapse.None");
}

void TimelapseDialog::refreshStatus()
{
	const SessionStatus status = controller_.status();
	const bool active = controller_.active();
	const bool stopping = controller_.stopping();

	stateValue_->setText(sessionStateName(status.state));
	stateValue_->setStyleSheet(stateStyleSheet(status.state));

	QString dropped = QString::number(status.droppedFrames);
	if (status.droppedFrames > 0)
		dropped = QStringLiteral("<span style=\"color: #d9534f; font-weight: bold;\">%1</span>").arg(dropped);
	countsValue_->setText(
		text("Timelapse.CountsFormat").arg(status.acceptedFrames).arg(status.writtenFrames).arg(dropped));

	timingValue_->setText(timingText(status, active));
	pathValue_->setText(status.outputPath.isEmpty() ? text("Timelapse.None") : status.outputPath);
	openFolderButton_->setEnabled(!status.outputPath.isEmpty());
	errorValue_->setText(status.error.isEmpty() ? text("Timelapse.None") : status.error);

	startStopButton_->setText(stopping ? text("Timelapse.Stopping")
				  : active ? text("Timelapse.Stop")
					   : text("Timelapse.Start"));
	startStopButton_->setEnabled(!stopping);
	backgroundHint_->setVisible(active && !stopping);

	const std::array<QWidget *, 8> settingWidgets = {outputDirectory_, namePrefix_,  mode_,
							 interval_,        playbackFps_, pngCompression_,
							 queueCapacity_,   browseButton_};
	for (QWidget *widget : settingWidgets)
		widget->setEnabled(!active);
	updateModeControls();
}

void TimelapseDialog::updateModeControls()
{
	const OutputMode selected = static_cast<OutputMode>(mode_->currentData().toInt());
	const bool locked = controller_.active();
	playbackFps_->setEnabled(selected == OutputMode::Mkv && !locked);
	pngCompression_->setEnabled(selected == OutputMode::Png && !locked);
	updateEstimate();
}

void TimelapseDialog::updateEstimate()
{
	const double interval = interval_->value();
	const OutputMode selected = static_cast<OutputMode>(mode_->currentData().toInt());
	const double framesPerHour = interval > 0.0 ? 3600.0 / interval : 0.0;

	if (selected == OutputMode::Mkv) {
		const int fps = playbackFps_->value();
		const double videoSeconds = fps > 0 ? framesPerHour / fps : 0.0;
		estimateValue_->setText(text("Timelapse.EstimateMkv")
						.arg(formatDuration(videoSeconds))
						.arg(qRound(framesPerHour))
						.arg(fps));
	} else {
		estimateValue_->setText(text("Timelapse.EstimatePng").arg(qRound(framesPerHour)));
	}
}

void TimelapseDialog::showError(const QString &message)
{
	QMessageBox::critical(this, text("Timelapse.ErrorTitle"), message);
}

} // namespace timelapse
