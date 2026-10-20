#include "FrameWriter.h"
#include <thread>
#include <chrono>
#include <libavutil/mathematics.h>

FrameWriter::FrameWriter(Streamer& streamer, ClockSync& clock) : streamer_(streamer), clock_(clock) {}

bool FrameWriter::writeVideo(AVPacket* pkt, AVRational timebase) {
    LOG_DEBUG("[FrameWriter] writeVideo called with pts=" + std::to_string(pkt->pts) + " dts=" + std::to_string(pkt->dts));
    pkt->pts += video_offset_;
    pkt->pts += audio_offset_;

    int64_t pts_us = av_rescale_q(pkt->pts, timebase, {1, 1000000});
    int64_t audio_us = clock_.audioClockUs();

    if (pts_us - audio_us > 500000) {
        LOG_DEBUG("[FrameWriter] video too far ahead, waiting...");
        while(pts_us - clock_.audioClockUs() > 500000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    int64_t wait_us = clock_.untilReady(pkt->pts, timebase);
    if (wait_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(wait_us));
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    pkt->stream_index = streamer_.video_stream()->index;
    av_packet_rescale_ts(pkt, timebase, streamer_.time_base_video());
    LOG_DEBUG("[FrameWriter] writing video packet with pts=" + std::to_string(pkt->pts) + " dts=" + std::to_string(pkt->dts));
    streamer_.write(pkt);
    return true;
}

bool FrameWriter::writeAudio(AVPacket* pkt, AVRational timebase) {
    pkt->pts += audio_offset_;
    pkt->dts += audio_offset_;

    clock_.updateAudioClock(pkt->pts, timebase);

    int64_t wait_us = clock_.untilReady(pkt->pts, timebase);
    if (wait_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(wait_us));
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    pkt->stream_index = streamer_.audio_stream()->index;
    av_packet_rescale_ts(pkt, timebase, streamer_.time_base_audio());
    streamer_.write(pkt);
    return true;
}

void FrameWriter::setVideoOffset(int64_t offset_pts) {
    video_offset_ = offset_pts;
}

void FrameWriter::setAudioOffset(int64_t offset_pts) {
    audio_offset_ = offset_pts;
}