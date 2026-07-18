#include "core-utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace timelapse {

uint32_t calculateFrameDivisor(double intervalSeconds, uint32_t fpsNumerator, uint32_t fpsDenominator)
{
	if (!std::isfinite(intervalSeconds) || intervalSeconds <= 0.0 || fpsNumerator == 0 || fpsDenominator == 0)
		return 1;

	const long double frames = static_cast<long double>(intervalSeconds) * fpsNumerator / fpsDenominator;
	const long double maximum = std::numeric_limits<uint32_t>::max();
	if (frames >= maximum)
		return std::numeric_limits<uint32_t>::max();

	return std::max<uint32_t>(1, static_cast<uint32_t>(std::llround(frames)));
}

double calculateEffectiveInterval(uint32_t divisor, uint32_t fpsNumerator, uint32_t fpsDenominator)
{
	if (divisor == 0 || fpsNumerator == 0 || fpsDenominator == 0)
		return 0.0;

	return static_cast<double>(divisor) * fpsDenominator / fpsNumerator;
}

QString sanitizeNamePrefix(const QString &value)
{
	QString result;
	result.reserve(value.size());
	bool previousSeparator = false;

	for (const QChar character : value.trimmed()) {
		if (character.isLetterOrNumber() || character == QLatin1Char('-')) {
			result.append(character);
			previousSeparator = false;
		} else if (!previousSeparator && !result.isEmpty()) {
			result.append(QLatin1Char('_'));
			previousSeparator = true;
		}
	}

	while (result.endsWith(QLatin1Char('_')))
		result.chop(1);

	return result.left(64);
}

QString formatDuration(double seconds)
{
	if (!std::isfinite(seconds) || seconds < 0.0)
		seconds = 0.0;
	if (seconds < 10.0)
		return QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
	if (seconds < 60.0)
		return QStringLiteral("%1 s").arg(qRound(seconds));

	const qint64 total = qRound64(seconds);
	if (total < 3600)
		return QStringLiteral("%1 min %2 s").arg(total / 60).arg(total % 60);
	return QStringLiteral("%1 h %2 min").arg(total / 3600).arg((total % 3600) / 60);
}

QString makeSessionName(const QString &prefix, const QDateTime &startedAt)
{
	const QString timestamp = startedAt.toUTC().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
	const QString safePrefix = sanitizeNamePrefix(prefix);
	return safePrefix.isEmpty() ? timestamp : timestamp + QLatin1Char('-') + safePrefix;
}

} // namespace timelapse
