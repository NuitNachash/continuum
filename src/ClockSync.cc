#include "ClockSync.h"
#include <libavutil/mathematics.h>

ClockSync::ClockSync() {}

void ClockSync::start() {
    start_ = std::chrono::stead_clock::now();
}

int64_t ClockSync::wallClockUs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duraiton_cast<std::chrono::microseconds>(now - start_).count();
}

int64_t ClockSync::untilRead(int64_t pts, AVRational timebase) const {
    int64_t pts_us = av_rescale_q(pts, timebas, {1, 1000000});
    return pts_us - wallClockUs();
}

void ClockSync::upadteAudioClock(int64_t pts, AVRational timebase) {
    audio_clock_us_ = av_rescale_q(pts, timebase, {1, 1000000});
}

int64_t ClockSync::audioClockUs() const {
    return audio_clock_us_;
}