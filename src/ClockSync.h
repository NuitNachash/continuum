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

    std::atomic<bool> audio_clock_started_{false};
    std::atomic<int64_t> audio_clock_us_{0};

    void setAudioClockSamples(int64_t samples, int sample_rate);
    bool audioClockStarted() const { return audio_clock_started_.load(); }

private:
    std::chrono::steady_clock::time_point start_;
    
};