#pragma once

#include "settings.hpp"

#include <QDialog>

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;

namespace timelapse {

class Controller;
struct SessionStatus;

class TimelapseDialog final : public QDialog {
public:
	explicit TimelapseDialog(Controller &controller, QWidget *parent = nullptr);
	bool persistSettings(QString &error);

protected:
	void closeEvent(QCloseEvent *event) override;
	void reject() override;

private:
	void browseOutputDirectory();
	void openOutputFolder();
	void toggleCapture();
	void refreshStatus();
	void updateModeControls();
	void updateEstimate();

	CaptureSettings formSettings() const;
	void setFormSettings(const CaptureSettings &settings);
	QString timingText(const SessionStatus &status, bool active) const;
	void showError(const QString &message);

	Controller &controller_;
	QLineEdit *outputDirectory_ = nullptr;
	QLineEdit *namePrefix_ = nullptr;
	QComboBox *mode_ = nullptr;
	QDoubleSpinBox *interval_ = nullptr;
	QSpinBox *playbackFps_ = nullptr;
	QSpinBox *pngCompression_ = nullptr;
	QSpinBox *queueCapacity_ = nullptr;
	QPushButton *browseButton_ = nullptr;
	QPushButton *startStopButton_ = nullptr;
	QPushButton *openFolderButton_ = nullptr;
	QLabel *estimateValue_ = nullptr;
	QLabel *stateValue_ = nullptr;
	QLabel *countsValue_ = nullptr;
	QLabel *timingValue_ = nullptr;
	QLabel *pathValue_ = nullptr;
	QLabel *errorValue_ = nullptr;
	QLabel *backgroundHint_ = nullptr;
	QTimer *statusTimer_ = nullptr;
};

} // namespace timelapse
