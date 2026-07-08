#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include <functional>
#include <string>
#include "Config.h"

class Encoder {
public:
	using PacketCallback = std::function<void(AVPacket*)>;

	explicit Encoder(const config& cfg);
	~Encoder();

	Encoder(const Encoder&) = delete;
	Encoder& operator=(const Encoder&) = delete;

	int sendVideo(AVFrame* frame);
	int sendAudio(AVFrame* frame);

	int receiveVideo(const PacketCallback& cb);
	int receiveAudio(const PacketCallback& cb);

	AVRational video_time_base() const;
	AVRational audio_time_base() const;
	
	AVCodecContext* video_context() const { return video_ctx_; }
	AVCodecContext* audio_context() const { return audio_ctx_; }

private:
	AVCodecContext* video_ctx_ = nullptr;
	AVCodecContext* audio_ctx_ = nullptr;
};