#include "Streamer.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>

/*  Initializes the output stream

    Creates an FLV container for RTMP streaming, copies encoder
    parameters into the output streams, opens the network connection,
    and writes the container header
*/

Streamer::Streamer(const config& cfg, const Encoder& encoder) {

    // Create an output format context for RTMP/FLV streaming
    int ret = avformat_alloc_output_context2(
        &fmt_, nullptr, "flv", cfg.rtmpUrl.c_str()
    );

    if (ret < 0 || !fmt_)
        throw std::runtime_error("failed to create output context");

    // The encoder provides global codec headers separately
    // This flag is requred by some container formats
    fmt_->flags |= AVFMT_GLOBALHEADER;

    // Create the video output stream
    video_stream_ = avformat_new_stream(fmt_, nullptr);
    if (!video_stream_)
        throw std::runtime_error("failed to create video stream");

    // Copy H2.64 encoder parameters into the output stream
    // The streamer does not encode; it only packages and sends packets
    avcodec_parameters_from_context(
        video_stream_->codecpar,
        encoder.video_context()
    );

    video_stream_->time_base = encoder.video_time_base();

    // Create the audio output stream
    audio_stream_ = avformat_new_stream(fmt_, nullptr);
    if (!audio_stream_)
        throw std::runtime_error("failed to create audio stream");

    // Create the audio output stream
    avcodec_parameters_from_context(
        audio_stream_->codecpar,
        encoder.audio_context()
    );

    audio_stream_->time_base = encoder.audio_time_base();
    
    av_opt_set(fmt_->priv_data, "timeout", "5000000", 0);

    // Open the RTMP connection
    ret = avio_open(&fmt_->pb, cfg.rtmpUrl.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0)
        throw std::runtime_error("avio_open failed");

    // Write the FLV header and initialize the stream
    ret = avformat_write_header(fmt_, nullptr);
    if (ret < 0)
        throw std::runtime_error("write_header failed");

    std::cout << "[Streamer] RTMP connected\n";
}

// Cleans up the RTMP output connection
Streamer::~Streamer() {
    if (fmt_) {
        // Finish writing the container before closing
        av_write_trailer(fmt_);

        // Close network output
        if (fmt_->pb)
            avio_closep(&fmt_->pb);

        // Release FFmpeg output context
        avformat_free_context(fmt_);
    }
}

// Creates a timestamp for streamer log messages
std::string streamTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Streamer-specific log file
std::ofstream streamLogFile;

// Writes diagnositc messages to console and log file
void streamLog(const std::string& msg) {
    std::string line = "[" + streamTimestamp() + "] " + msg;
    std::cout << line << "\n";
    if (streamLogFile.is_open()) {
        streamLogFile << line << "\n";
        streamLogFile.flush();
    }
}

// Track the last time each stream type was written
// Used temporarily to debug delays in packet delivery
static auto last_video_write = std::chrono::steady_clock::now();
static auto last_audio_write = std::chrono::steady_clock::now();

// Sends an encoded packet to the RTMP output
// Packets are already compressed by Encoder
// This function only handles timing diagnositcs and muxing
int Streamer::write(AVPacket* pkt) {

    // Open diagnostic log file if it has not already been opened
    streamLogFile.open("continuum_streamer.log", std::ios::app);
    auto t0 = std::chrono::steady_clock::now();

    // Determine whether this packet contains video or audio
    bool isVideo = (pkt->stream_index == video_stream_->index);

    // Write packet into the FLV muxer and send it to RTMP
    int ret = av_interleaved_write_frame(fmt_, pkt);

    // Measure time spent writing the packet
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();

    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        streamLog(std::string("[Streamer] write error: ") + err);
    }
    return ret;
}
