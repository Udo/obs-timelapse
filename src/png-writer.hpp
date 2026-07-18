#pragma once

#include <QString>

#include <cstdint>

namespace timelapse {

bool writePngAtomically(const QString &path, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride,
			int compression, QString &error);

} // namespace timelapse
