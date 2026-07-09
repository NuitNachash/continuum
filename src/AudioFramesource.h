#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
#include <libavutil/audio_fifo.h>
}

#include "Config.h"


class AudioFrameSource {
public:

    explicit AudioFrameSource(const config& cfg, AVRational audio_time_base);
    ~AudioFrameSource();
    AVRational audio_time_base_;
    AudioFrameSource(const AudioFrameSource&) = delete;
    AudioFrameSource& operator=(const AudioFrameSource&) = delete;

    
    AVFrame* next();
    void switchFile(const std::string& path);
    int fifoSize() const {
        return av_audio_fifo_size(audio_fifo_);
    }

private:
    void initDecoder();
    void initResampler();
    void convertToEncoderFormat(AVFrame* decoded);
    void openFile(const std::string& path);
    void closeFile();

private:
    config cfg_;

    AVFormatContext* fmt_ = nullptr;

    AVCodecContext* dec_ctx_ = nullptr;

    SwrContext* swr_ = nullptr;

    AVFrame* decoded_frame_ = nullptr;
    AVFrame* converted_frame_ = nullptr;
    AVPacket* pkt_ = nullptr;

    AVChannelLayout out_ch_layout_;

    int audio_stream_index_ = -1;

    bool initAudioFifo();
    bool pushToFifo(AVFrame* frame);
    AVFrame* popFifoFrame1024();

    AVAudioFifo* audio_fifo_ = nullptr;
    int fifo_channels_ = 2;
    AVSampleFormat fifo_format_ = AV_SAMPLE_FMT_FLTP;

    std::string current_path_;
};