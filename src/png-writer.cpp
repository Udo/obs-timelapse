#include "png-writer.hpp"

#include <QImage>
#include <QImageWriter>
#include <QSaveFile>

namespace timelapse {

bool writePngAtomically(const QString &path, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride,
			int compression, QString &error)
{
	QImage image(pixels, static_cast<int>(width), static_cast<int>(height), static_cast<int>(stride),
		     QImage::Format_ARGB32);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		error = QStringLiteral("Could not open the output file: %1").arg(file.errorString());
		return false;
	}
	{
		// Destroy the encoder before commit: Windows cannot replace a file while
		// QImageWriter still owns its open handle.
		QImageWriter writer(&file, "png");
		writer.setCompression((compression * 100) / 9);
		if (!writer.write(image)) {
			error = QStringLiteral("Could not encode the image: %1").arg(writer.errorString());
			file.cancelWriting();
			return false;
		}
	}
	if (!file.commit()) {
		error = QStringLiteral("Could not finalize the output file: %1").arg(file.errorString());
		return false;
	}
	return true;
}

} // namespace timelapse
