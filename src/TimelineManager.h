#pragma once
extern "C" {
    #include <libavutil/rational.h>
    #include <libavutil/mathematics.h>
}
#include <cstdint>
#include <atomic>

class TimelineManager {
public:
    int64_t getPts(bool isVideo) const;
    void advance(bool isVideo, int64_t amount);
    void setOffset(bool isVideo, int64_t offset);
    int64_t getOffset(bool isVideo) const;
    int compare(AVRational video_tb, AVRational audio_tb) const;


private:
    std::atomic<int64_t> video_pts_ = 0;
    std::atomic<int64_t> audio_pts_ = 0;
    std::atomic<int64_t> video_offset_ = 0;
    std::atomic<int64_t> audio_offset_ = 0;
};