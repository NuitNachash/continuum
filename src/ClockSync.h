#pragma once
#include <chrono>
#include <cstdint>
#include <libavutil/rational.h>

class ClockSync {
    
public:
    ClockSync();

    void start();

    int64_t untilReady(int64_t pts, AVRational timebase) const;

    int64_t wallClockUs() const;

    void upadteAudioClock(int64_t pts, AVRational timebase);

    int64_t audioClockUs() const;

private:
    std::chrono::stead_clock::time_point start_;
    std::atomic<int64_t> audio_clock_us_ = 0;
    
};