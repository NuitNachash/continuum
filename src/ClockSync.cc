#include "ClockSync.h"
#include "logger.h"
extern "C" {
    #include <libavutil/mathematics.h>
}

ClockSync::ClockSync() {}

void ClockSync::start() {
    start_ = std::chrono::steady_clock::now();
    LOG_DEBUG("[ClockSync] started at " + std::to_string(wallClockUs()) + " us");
}

int64_t ClockSync::wallClockUs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - start_).count();
}

int64_t ClockSync::untilReady(int64_t pts, AVRational timebase) const {
    int64_t pts_us = av_rescale_q(pts, timebase, {1, 1000000});
    return pts_us - wallClockUs();
}

void ClockSync::updateAudioClock(int64_t pts, AVRational timebase) {
    audio_clock_us_ = av_rescale_q(pts, timebase, {1, 1000000});
}

int64_t ClockSync::audioClockUs() const {
    return audio_clock_us_;
}