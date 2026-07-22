#include "settings.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QDir>
#include <QStandardPaths>

namespace timelapse {
namespace {

constexpr auto Section = "OBS-Timelapse";

QString defaultOutputDirectory()
{
	QString path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	if (path.isEmpty())
		path = QDir::homePath();
	return QDir(path).filePath(QStringLiteral("OBS Timelapse"));
}

config_t *userConfig()
{
	return obs_frontend_get_user_config();
}

} // namespace

const char *outputModeName(OutputMode mode)
{
	return mode == OutputMode::Mkv ? "mkv" : "png";
}

CaptureSettings loadSettings()
{
	CaptureSettings settings;
	settings.outputDirectory = defaultOutputDirectory();

	config_t *config = userConfig();
	if (!config)
		return settings;

	const QByteArray defaultPath = settings.outputDirectory.toUtf8();
	config_set_default_string(config, Section, "OutputDirectory", defaultPath.constData());
	config_set_default_string(config, Section, "NamePrefix", "");
	config_set_default_string(config, Section, "Mode", "png");
	config_set_default_double(config, Section, "IntervalSeconds", 10.0);
	config_set_default_int(config, Section, "PlaybackFps", 30);
	config_set_default_int(config, Section, "PngCompression", 6);
	config_set_default_int(config, Section, "QueueCapacity", 4);

	settings.outputDirectory = QString::fromUtf8(config_get_string(config, Section, "OutputDirectory"));
	settings.namePrefix = QString::fromUtf8(config_get_string(config, Section, "NamePrefix"));
	settings.mode = QString::fromUtf8(config_get_string(config, Section, "Mode")) == QStringLiteral("mkv")
				? OutputMode::Mkv
				: OutputMode::Png;
	settings.intervalSeconds = config_get_double(config, Section, "IntervalSeconds");
	settings.playbackFps = static_cast<int>(config_get_int(config, Section, "PlaybackFps"));
	settings.pngCompression = static_cast<int>(config_get_int(config, Section, "PngCompression"));
	settings.queueCapacity = static_cast<std::size_t>(config_get_int(config, Section, "QueueCapacity"));
	return settings;
}

bool saveSettings(const CaptureSettings &settings, QString &error)
{
	config_t *config = userConfig();
	if (!config) {
		error = QStringLiteral("OBS user configuration is unavailable.");
		return false;
	}

	const QByteArray outputDirectory = settings.outputDirectory.toUtf8();
	const QByteArray namePrefix = settings.namePrefix.toUtf8();
	config_set_string(config, Section, "OutputDirectory", outputDirectory.constData());
	config_set_string(config, Section, "NamePrefix", namePrefix.constData());
	config_set_string(config, Section, "Mode", outputModeName(settings.mode));
	config_set_double(config, Section, "IntervalSeconds", settings.intervalSeconds);
	config_set_int(config, Section, "PlaybackFps", settings.playbackFps);
	config_set_int(config, Section, "PngCompression", settings.pngCompression);
	config_set_int(config, Section, "QueueCapacity", static_cast<int64_t>(settings.queueCapacity));

	if (config_save_safe(config, "tmp", nullptr) != CONFIG_SUCCESS) {
		error = QStringLiteral("OBS could not save the plugin configuration.");
		return false;
	}

	return true;
}

QString validateSettings(const CaptureSettings &settings)
{
	if (settings.outputDirectory.trimmed().isEmpty())
		return QStringLiteral("Choose an output directory.");
	if (settings.intervalSeconds < MinimumIntervalSeconds || settings.intervalSeconds > MaximumIntervalSeconds)
		return QStringLiteral("The capture interval must be between 0.1 seconds and 24 hours.");
	if (settings.playbackFps < MinimumPlaybackFps || settings.playbackFps > MaximumPlaybackFps)
		return QStringLiteral("The playback frame rate must be between 1 and 60 FPS.");
	if (settings.pngCompression < MinimumPngCompression || settings.pngCompression > MaximumPngCompression)
		return QStringLiteral("PNG compression must be between 0 and 9.");
	if (settings.queueCapacity < MinimumQueueCapacity || settings.queueCapacity > MaximumQueueCapacity)
		return QStringLiteral("The frame queue must contain between 2 and 32 frames.");
	return {};
}

} // namespace timelapse
