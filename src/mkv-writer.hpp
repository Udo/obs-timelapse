#pragma once

#include <QString>

#include <cstdint>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVStream;
struct SwsContext;

namespace timelapse {

class MkvWriter final {
public:
	MkvWriter() = default;
	~MkvWriter();

	MkvWriter(const MkvWriter &) = delete;
	MkvWriter &operator=(const MkvWriter &) = delete;

	bool open(const QString &partialPath, const QString &finalPath, uint32_t width, uint32_t height, int fps,
		  QString &error);
	bool writeFrame(const uint8_t *pixels, uint32_t stride, QString &error);
	bool finish(QString &error);

private:
	bool openInternal(uint32_t width, uint32_t height, int fps, QString &error);
	bool drainPackets(QString &error);
	void release() noexcept;

	QString partialPath_;
	QString finalPath_;
	uint32_t sourceHeight_ = 0;
	AVFormatContext *format_ = nullptr;
	AVCodecContext *codec_ = nullptr;
	AVStream *stream_ = nullptr;
	AVFrame *frame_ = nullptr;
	SwsContext *scaler_ = nullptr;
	int64_t nextPts_ = 0;
	bool headerWritten_ = false;
	bool finished_ = false;
};

} // namespace timelapse
