#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include "Config.h"
#include "Encoder.h"

class Streamer {
public:
    Streamer(const config& cfg, const Encoder& encoder);
    ~Streamer();

    int write(AVPacket* pkt);

    AVStream* video_stream() const { return video_stream_; }
    AVStream* audio_stream() const { return audio_stream_; }

    AVRational time_base_video() const { return video_stream_->time_base; }
    AVRational time_base_audio() const { return audio_stream_->time_base; }

private:
    AVFormatContext* fmt_ = nullptr;

    AVStream* video_stream_ = nullptr;
    AVStream* audio_stream_ = nullptr;
};