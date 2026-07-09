#include "Streamer.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>

Streamer::Streamer(const config& cfg, const Encoder& encoder) {

    int ret = avformat_alloc_output_context2(
        &fmt_, nullptr, "flv", cfg.rtmpUrl.c_str()
    );

    if (ret < 0 || !fmt_)
        throw std::runtime_error("failed to create output context");

    fmt_->flags |= AVFMT_GLOBALHEADER;

    video_stream_ = avformat_new_stream(fmt_, nullptr);
    if (!video_stream_)
        throw std::runtime_error("failed to create video stream");

    avcodec_parameters_from_context(
        video_stream_->codecpar,
        encoder.video_context()
    );

    video_stream_->time_base = encoder.video_time_base();

    audio_stream_ = avformat_new_stream(fmt_, nullptr);
    if (!audio_stream_)
        throw std::runtime_error("failed to create audio stream");

    avcodec_parameters_from_context(
        audio_stream_->codecpar,
        encoder.audio_context()
    );

    audio_stream_->time_base = encoder.audio_time_base();

    ret = avio_open(&fmt_->pb, cfg.rtmpUrl.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0)
        throw std::runtime_error("avio_open failed");

    ret = avformat_write_header(fmt_, nullptr);
    if (ret < 0)
        throw std::runtime_error("write_header failed");

    std::cout << "[Streamer] RTMP connected\n";
}

Streamer::~Streamer() {
    if (fmt_) {
        av_write_trailer(fmt_);
        if (fmt_->pb)
            avio_closep(&fmt_->pb);
        avformat_free_context(fmt_);
    }
}

std::string streamTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::ofstream streamLogFile;

void streamLog(const std::string& msg) {
    std::string line = "[" + streamTimestamp() + "] " + msg;
    std::cout << line << "\n";
    if (streamLogFile.is_open()) {
        streamLogFile << line << "\n";
        streamLogFile.flush();
    }
}

static auto last_video_write = std::chrono::steady_clock::now();
static auto last_audio_write = std::chrono::steady_clock::now();

int Streamer::write(AVPacket* pkt) {
    streamLogFile.open("continuum_streamer.log", std::ios::app);
    auto t0 = std::chrono::steady_clock::now();

    bool isVideo = (pkt->stream_index == video_stream_->index);
    if (isVideo) {
        auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(t0 - last_video_write).count();
        if (gap > 100) streamLog("[Streamer] video write gap: " + std::to_string(gap) + "ms");
        last_video_write = t0;
    } else {
        auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(t0 - last_audio_write).count();
        if (gap > 100) streamLog("[Streamer] audio write gap: " + std::to_string(gap) + "ms");
        last_audio_write = t0;
    }
    int ret = av_interleaved_write_frame(fmt_, pkt);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();

    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        streamLog(std::string("[Streamer] write error: ") + err);
    }
    return ret;
}