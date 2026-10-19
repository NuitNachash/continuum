#pragma once
#include <chrono>
#include <cstdint>
#include <libavutil/rational.h>
#include <atomic>

class ClockSync {
    
public:
    ClockSync();

    void start();

    int64_t untilReady(int64_t pts, AVRational timebase) const;

    int64_t wallClockUs() const;

    void updateAudioClock(int64_t pts, AVRational timebase);

    int64_t audioClockUs() const;

private:
    std::chrono::steady_clock::time_point start_;
    std::atomic<int64_t> audio_clock_us_ = 0;
};