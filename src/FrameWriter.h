#pragma once
#include "ClockSync.h"
#include "Streamer.h"
#include "logger.h"
#include <libavcodec/avcodec.h>
#include <atomic>
#include <mutex>

class FrameWriter {
public:
    explicity FrameWriter(Streamer& streamer, ClockSync& clock);

    bool writeVideo(AVPacket* pkt, AVRational timebase);

    bool writeAudio(AVPacket* pkt, AVRational timebase);

    void setVideoOffset(int64_t offset_pts);

    void setAudioOffset(int64_t offset_pts);

    int64_t videoOffset() const { return video_offset_; }
    int64_t audioOffset() const { return audio_offset_; }

private:
    Streamer& streamer_;
    ClockSync& clock_;

    std::atomic<int64_t> video_offset_ = 0;
    std::atomic<int64_t> audio_offset_ = 0;

    std::mutex write_mutex_;
};