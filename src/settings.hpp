#pragma once

#include <QString>

#include <cstddef>

namespace timelapse {

enum class OutputMode { Png, Mkv };

inline constexpr double MinimumIntervalSeconds = 0.1;
inline constexpr double MaximumIntervalSeconds = 86400.0;
inline constexpr int MinimumPlaybackFps = 1;
inline constexpr int MaximumPlaybackFps = 60;
inline constexpr int MinimumPngCompression = 0;
inline constexpr int MaximumPngCompression = 9;
inline constexpr std::size_t MinimumQueueCapacity = 2;
inline constexpr std::size_t MaximumQueueCapacity = 32;

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
