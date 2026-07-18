#pragma once

#include <QString>

#include <cstddef>

namespace timelapse {

enum class OutputMode { Png, Mkv };

struct CaptureSettings {
	QString outputDirectory;
	QString namePrefix;
	OutputMode mode = OutputMode::Png;
	double intervalSeconds = 10.0;
	int playbackFps = 30;
	int pngCompression = 6;
	std::size_t queueCapacity = 4;
};

CaptureSettings loadSettings();
bool saveSettings(const CaptureSettings &settings, QString &error);
QString validateSettings(const CaptureSettings &settings);
const char *outputModeName(OutputMode mode);

} // namespace timelapse
