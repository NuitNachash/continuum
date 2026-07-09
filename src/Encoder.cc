#include "Encoder.h"
extern "C" {
    #include <libswresample/swresample.h>
}
#include <iostream>
#include <stdexcept>

// Initializes both video and audio encoders
// Video is encoded using H.264 and audio is encoded using AAC
Encoder::Encoder(const config& cfg) {

    // Find the H.264 video encoder implementation
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        throw std::runtime_error("H.264 encoder not found");

    // Allocate and configure the video encoder context
    video_ctx_ = avcodec_alloc_context3(codec);
    if (!video_ctx_)
        throw std::runtime_error("video avcodec_alloc_context3 failed");

	video_ctx_->codec_id = AV_CODEC_ID_H264;
	video_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;

    // Video dimensions come from the config
	video_ctx_->width = cfg.width;
	video_ctx_->height = cfg.height;

    // H.264 streaming commonly uses YUV420 planar format
	video_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;

    // Time base represents the duration of one video timestamp unit
	video_ctx_->time_base = AVRational{1, cfg.fps};

	video_ctx_->framerate = AVRational{cfg.fps, 1};

    // Keyframe interval
    // Creates an I-frame every 2 seconds to allow stream recovery/seeking
	video_ctx_->gop_size = cfg.fps * 2;

    // Disable B-frames to reduce latency for live streaming
	video_ctx_->max_b_frames = 0;

    // Video bitrate used by the encoder
	video_ctx_->bit_rate = 6000000;     

    // Required when writign encoding data into containers such as FLV
    video_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Configure x264 for faster encoding and lower latency
    av_opt_set(video_ctx_->priv_data, "preset", "veryfast",     0);
    av_opt_set(video_ctx_->priv_data, "tune",   "zerolatency",  0);

    // Open the H.264 encoder
    int ret = avcodec_open2(video_ctx_, codec, nullptr);
    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        throw std::runtime_error(std::string("Video avcodec_open2 failed: ") + err);
    }

    std::cout << "[Encoder] opened video at H.264 "
              << video_ctx_->width << "x" << video_ctx_->height
              << " @ " << cfg.fps << "fps"
              << "  time_base=" << video_ctx_->time_base.num << "/" << video_ctx_->time_base.den
              << '\n';

    // Find AAC audio encoder
    const AVCodec* audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audio_codec)
        throw std::runtime_error("AAC encoder not found");
    
    // Allocate and configure the audio encoder context
    audio_ctx_ = avcodec_alloc_context3(audio_codec);
    if (!audio_ctx_)
        throw std::runtime_error("Audio avcodec_alloc_context3 failed");

    audio_ctx_->codec_type = AVMEDIA_TYPE_AUDIO;

    // Audio sample rate must match the audio source conversion output
    audio_ctx_->sample_rate = cfg.samplerate; 

    // Streaming output uses stereo audio
    av_channel_layout_default(&audio_ctx_->ch_layout, 2);

    // AAC encoder expects planar floating-point samples
    audio_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;

    // Audio timestamps are measured in samples
    audio_ctx_->time_base = AVRational{1, cfg.samplerate};

    // Configure AAC bitrate
    audio_ctx_->bit_rate = cfg.a_bitrate;

    // Open AAC encoder
    int ret2 = avcodec_open2(audio_ctx_, audio_codec, nullptr);
    if (ret2 < 0) {
        char err[256];
        av_strerror(ret2, err, sizeof(err));
        throw std::runtime_error(std::string("Audio avcodec_open2 failed: ") + err);
    }

    std::cout << "[Encoder] opened audio AAC "
          << audio_ctx_->sample_rate << " Hz"
          << " channels=stereo"
          << " time_base=" << audio_ctx_->time_base.num << "/" << audio_ctx_->time_base.den
          << '\n';

}

// Releases encoder contexts and their allocated resources
Encoder::~Encoder() {
    if (video_ctx_)
        avcodec_free_context(&video_ctx_);
    
    if (audio_ctx_)
        avcodec_free_context(&audio_ctx_);
}

// Sends a raw video frame into the H.264 encoder
int Encoder::sendVideo(AVFrame* frame) {
    int ret = avcodec_send_frame(video_ctx_, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        std::cerr << "[Encoder] video send_frame error: " << err << '\n';
    }
    return ret;
}

// Retrieves all available encoded video packets
// The encoder may buffer frames internally, so multiple packets
// can sometimes be returned after one input frame
int Encoder::receiveVideo(const PacketCallback& cb) {
    AVPacket* pkt = av_packet_alloc();

    int ret = 0;

    while ((ret = avcodec_receive_packet(video_ctx_, pkt)) == 0) {
        // Pass encoded packet to the streamer
        cb(pkt);
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    // EAGAIN means the encoder needs more frames before producing output
    return (ret == AVERROR(EAGAIN)) ? 0 : ret;
}

// Sends raw audio samples into the AAC encoder
int Encoder::sendAudio(AVFrame* frame) {
    int ret = avcodec_send_frame(audio_ctx_, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        std::cerr << "[Encoder] audio send_frame error: " << err << '\n';
    }
    return ret;
}

// Retrieves all available encoded AAC packets
int Encoder::receiveAudio(const PacketCallback& cb) {
    AVPacket* pkt = av_packet_alloc();

    int ret = 0;

    while ((ret = avcodec_receive_packet(audio_ctx_, pkt)) == 0) {
        // Send encoded packet to the streamer
        cb(pkt);
        av_packet_unref(pkt);
    }
    
    av_packet_free(&pkt);

    return (ret == AVERROR(EAGAIN)) ? 0 : ret;
}

// Returns the timestamp scale used by video frames
AVRational Encoder::video_time_base() const {
    return video_ctx_->time_base;
}

// Returns the timestamp scale used by audio samples
AVRational Encoder::audio_time_base() const {
    return audio_ctx_->time_base;
}