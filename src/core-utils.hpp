#pragma once

#include <QDateTime>
#include <QString>

#include <cstdint>

namespace timelapse {

uint32_t calculateFrameDivisor(double intervalSeconds, uint32_t fpsNumerator, uint32_t fpsDenominator);
double calculateEffectiveInterval(uint32_t divisor, uint32_t fpsNumerator, uint32_t fpsDenominator);
QString sanitizeNamePrefix(const QString &value);
QString makeSessionName(const QString &prefix, const QDateTime &startedAt);
QString formatDuration(double seconds);

} // namespace timelapse
