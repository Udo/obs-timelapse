#include "mkv-writer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <QFile>

#include <algorithm>

namespace timelapse {
namespace {

QString ffmpegError(const QString &operation, int code)
{
	char message[AV_ERROR_MAX_STRING_SIZE]{};
	av_strerror(code, message, sizeof(message));
	return QStringLiteral("%1: %2").arg(operation, QString::fromUtf8(message));
}

} // namespace

MkvWriter::~MkvWriter()
{
	release();
}

bool MkvWriter::open(const QString &partialPath, const QString &finalPath, uint32_t width, uint32_t height, int fps,
		     QString &error)
{
	if (!partialPath_.isEmpty()) {
		error = QStringLiteral("An MKV writer cannot be reopened.");
		return false;
	}
	partialPath_ = partialPath;
	finalPath_ = finalPath;
	QFile::remove(partialPath_);

	if (openInternal(width, height, fps, error))
		return true;

	release();
	QFile::remove(partialPath_);
	partialPath_.clear();
	finalPath_.clear();
	return false;
}

bool MkvWriter::openInternal(uint32_t width, uint32_t height, int fps, QString &error)
{
	// H.264 with YUV 4:2:0 needs even dimensions; the scaler absorbs a
	// one-pixel crop when the OBS output size is odd.
	sourceHeight_ = height;
	const uint32_t encodeWidth = width & ~1u;
	const uint32_t encodeHeight = height & ~1u;
	if (encodeWidth == 0 || encodeHeight == 0) {
		error = QStringLiteral("The OBS output is too small for MKV encoding.");
		return false;
	}

	const QByteArray encodedPath = partialPath_.toUtf8();
	int result = avformat_alloc_output_context2(&format_, nullptr, "matroska", encodedPath.constData());
	if (result < 0 || !format_) {
		error = result < 0 ? ffmpegError(QStringLiteral("Could not create the MKV container"), result)
				   : QStringLiteral("Could not create the MKV container.");
		return false;
	}

	const AVCodec *encoder = avcodec_find_encoder_by_name("libx264");
	if (!encoder)
		encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!encoder) {
		error = QStringLiteral("This OBS installation does not provide an H.264 encoder for MKV output.");
		return false;
	}

	stream_ = avformat_new_stream(format_, nullptr);
	codec_ = avcodec_alloc_context3(encoder);
	if (!stream_ || !codec_) {
		error = QStringLiteral("Could not allocate the MKV video encoder.");
		return false;
	}

	codec_->codec_id = encoder->id;
	codec_->codec_type = AVMEDIA_TYPE_VIDEO;
	codec_->width = static_cast<int>(encodeWidth);
	codec_->height = static_cast<int>(encodeHeight);
	codec_->pix_fmt = AV_PIX_FMT_YUV420P;
	codec_->time_base = AVRational{1, fps};
	codec_->framerate = AVRational{fps, 1};
	codec_->gop_size = std::max(fps * 2, 1);
	codec_->max_b_frames = 0;
	codec_->colorspace = AVCOL_SPC_BT709;
	codec_->color_primaries = AVCOL_PRI_BT709;
	codec_->color_trc = AVCOL_TRC_IEC61966_2_1;
	codec_->color_range = AVCOL_RANGE_MPEG;
	if (format_->oformat->flags & AVFMT_GLOBALHEADER)
		codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if (codec_->priv_data) {
		av_opt_set(codec_->priv_data, "preset", "veryfast", 0);
		av_opt_set(codec_->priv_data, "crf", "18", 0);
	}

	result = avcodec_open2(codec_, encoder, nullptr);
	if (result < 0) {
		error = ffmpegError(QStringLiteral("Could not open the H.264 encoder"), result);
		return false;
	}

	stream_->time_base = codec_->time_base;
	result = avcodec_parameters_from_context(stream_->codecpar, codec_);
	if (result < 0) {
		error = ffmpegError(QStringLiteral("Could not configure the MKV video stream"), result);
		return false;
	}

	if (!(format_->oformat->flags & AVFMT_NOFILE)) {
		result = avio_open(&format_->pb, encodedPath.constData(), AVIO_FLAG_WRITE);
		if (result < 0) {
			error = ffmpegError(QStringLiteral("Could not open the partial MKV file"), result);
			return false;
		}
	}

	result = avformat_write_header(format_, nullptr);
	if (result < 0) {
		error = ffmpegError(QStringLiteral("Could not write the MKV header"), result);
		return false;
	}
	headerWritten_ = true;

	frame_ = av_frame_alloc();
	packet_ = av_packet_alloc();
	if (!frame_ || !packet_) {
		error = QStringLiteral("Could not allocate MKV frame storage.");
		return false;
	}
	frame_->format = codec_->pix_fmt;
	frame_->width = codec_->width;
	frame_->height = codec_->height;
	result = av_frame_get_buffer(frame_, 32);
	if (result < 0) {
		error = ffmpegError(QStringLiteral("Could not allocate MKV frame storage"), result);
		return false;
	}

	scaler_ = sws_getContext(static_cast<int>(width), static_cast<int>(height), AV_PIX_FMT_BGRA, codec_->width,
				 codec_->height, codec_->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!scaler_) {
		error = QStringLiteral("Could not initialize MKV color conversion.");
		return false;
	}
	// Match the stream tags: BT.709 matrix, full-range sRGB in, limited-range YUV out.
	sws_setColorspaceDetails(scaler_, sws_getCoefficients(SWS_CS_ITU709), 1, sws_getCoefficients(SWS_CS_ITU709), 0,
				 0, 1 << 16, 1 << 16);
	return true;
}

bool MkvWriter::drainPackets(QString &error)
{
	for (;;) {
		const int result = avcodec_receive_packet(codec_, packet_);
		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
			break;
		if (result < 0) {
			error = ffmpegError(QStringLiteral("Could not receive an encoded MKV frame"), result);
			return false;
		}

		if (packet_->duration <= 0)
			packet_->duration = 1;
		av_packet_rescale_ts(packet_, codec_->time_base, stream_->time_base);
		packet_->stream_index = stream_->index;
		const int writeResult = av_interleaved_write_frame(format_, packet_);
		av_packet_unref(packet_);
		if (writeResult < 0) {
			error = ffmpegError(QStringLiteral("Could not append a frame to the MKV file"), writeResult);
			return false;
		}
	}
	return true;
}

bool MkvWriter::writeFrame(const uint8_t *pixels, uint32_t stride, QString &error)
{
	if (finished_ || !pixels || !frame_ || !scaler_) {
		error = QStringLiteral("The MKV writer is not ready.");
		return false;
	}

	int result = av_frame_make_writable(frame_);
	if (result < 0) {
		error = ffmpegError(QStringLiteral("Could not prepare an MKV frame"), result);
		return false;
	}

	const uint8_t *sourceData[] = {pixels, nullptr, nullptr, nullptr};
	const int sourceStride[] = {static_cast<int>(stride), 0, 0, 0};
	result = sws_scale(scaler_, sourceData, sourceStride, 0, static_cast<int>(sourceHeight_), frame_->data,
			   frame_->linesize);
	if (result != codec_->height) {
		error = QStringLiteral("Could not convert a captured frame for MKV encoding.");
		return false;
	}

	frame_->pts = nextPts_++;
	result = avcodec_send_frame(codec_, frame_);
	if (result < 0) {
		error = ffmpegError(QStringLiteral("Could not submit a frame to the MKV encoder"), result);
		return false;
	}
	return drainPackets(error);
}

bool MkvWriter::finish(QString &error)
{
	if (finished_)
		return true;
	finished_ = true;

	if (!format_ || !codec_ || !headerWritten_) {
		error = QStringLiteral("The MKV writer was not initialized completely.");
		release();
		return false;
	}

	int result = avcodec_send_frame(codec_, nullptr);
	if (result < 0 && result != AVERROR_EOF) {
		error = ffmpegError(QStringLiteral("Could not flush the MKV encoder"), result);
		release();
		return false;
	}
	if (!drainPackets(error)) {
		release();
		return false;
	}

	result = av_write_trailer(format_);
	if (result < 0) {
		error = ffmpegError(QStringLiteral("Could not finalize the MKV file"), result);
		release();
		return false;
	}
	headerWritten_ = false;
	release();

	QFile::remove(finalPath_);
	if (!QFile::rename(partialPath_, finalPath_)) {
		error = QStringLiteral("Could not rename the completed MKV file to %1.").arg(finalPath_);
		return false;
	}
	return true;
}

void MkvWriter::release() noexcept
{
	if (format_ && format_->pb && !(format_->oformat->flags & AVFMT_NOFILE))
		avio_closep(&format_->pb);
	if (scaler_)
		sws_freeContext(scaler_);
	if (frame_)
		av_frame_free(&frame_);
	if (packet_)
		av_packet_free(&packet_);
	if (codec_)
		avcodec_free_context(&codec_);
	if (format_)
		avformat_free_context(format_);
	format_ = nullptr;
	scaler_ = nullptr;
	stream_ = nullptr;
}

} // namespace timelapse
