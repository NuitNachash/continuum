#pragma once

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/audio_fifo.h>
    #include <libswscale/swscale.h>
}
#include "Config.h"

class VideoFrameSource {
public:
    explicit VideoFrameSource(const config& cfg);
    virtual ~VideoFrameSource();

    VideoFrameSource(const VideoFrameSource&) = delete;
    VideoFrameSource& operator=(const VideoFrameSource&) = delete;

    AVFrame* next();

    int64_t frame_count() const {
        return frame_count_;
    }

    void switchFile(const std::string& path);

    int64_t getDuration() const {
        if (!fmt_ || fmt_->duration == AV_NOPTS_VALUE)
            return 0;
        return fmt_->duration;
    }

private:
    virtual void fill();
    SwsContext* sws_ = nullptr;
    AVFrame* scaled_frame_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* dec_ctx_ = nullptr;
    AVPacket* pkt_ = nullptr;
    int video_stream_index_ = -1;
    config cfg_;
    int64_t frame_count_ = 0;
    int64_t first_pts_ = 0;

    void openFile(const std::string& path);
    void closeFile();
};