#include "Encoder.h"
extern "C" {
    #include <libswresample/swresample.h>
}
#include <iostream>
#include <stdexcept>

Encoder::Encoder(const config& cfg) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        throw std::runtime_error("H.264 encoder not found");

    video_ctx_ = avcodec_alloc_context3(codec);
    if (!video_ctx_)
        throw std::runtime_error("video avcodec_alloc_context3 failed");

	video_ctx_->codec_id = AV_CODEC_ID_H264;
	video_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
	video_ctx_->width = cfg.width;
	video_ctx_->height = cfg.height;
	video_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
	video_ctx_->time_base = AVRational{1, cfg.fps};

	video_ctx_->framerate = AVRational{cfg.fps, 1};
	video_ctx_->gop_size = cfg.fps * 2;
	video_ctx_->max_b_frames = 0;
	video_ctx_->bit_rate = 6000000;     

    video_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_opt_set(video_ctx_->priv_data, "preset", "veryfast",     0);
    av_opt_set(video_ctx_->priv_data, "tune",   "zerolatency",  0);

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

    const AVCodec* audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audio_codec)
        throw std::runtime_error("AAC encoder not found");
    
    audio_ctx_ = avcodec_alloc_context3(audio_codec);
    if (!audio_ctx_)
        throw std::runtime_error("Audio avcodec_alloc_context3 failed");

    audio_ctx_->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_ctx_->sample_rate = cfg.samplerate;               // 48000
    av_channel_layout_default(&audio_ctx_->ch_layout, 2);   // stereo
    audio_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audio_ctx_->time_base = AVRational{1, cfg.samplerate};  // create time_base to match video
    audio_ctx_->bit_rate = cfg.a_bitrate;                   // 160000

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

Encoder::~Encoder() {
    if (video_ctx_)
        avcodec_free_context(&video_ctx_);
    
    if (audio_ctx_)
        avcodec_free_context(&audio_ctx_);
}

int Encoder::sendVideo(AVFrame* frame) {
    int ret = avcodec_send_frame(video_ctx_, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        std::cerr << "[Encoder] video send_frame error: " << err << '\n';
    }
    return ret;
}

int Encoder::receiveVideo(const PacketCallback& cb) {
    AVPacket* pkt = av_packet_alloc();

    int ret = 0;

    while ((ret = avcodec_receive_packet(video_ctx_, pkt)) == 0) {
        cb(pkt);
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    return (ret == AVERROR(EAGAIN)) ? 0 : ret;
}

int Encoder::sendAudio(AVFrame* frame) {
    int ret = avcodec_send_frame(audio_ctx_, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        std::cerr << "[Encoder] audio send_frame error: " << err << '\n';
    }
    return ret;
}

int Encoder::receiveAudio(const PacketCallback& cb) {
    AVPacket* pkt = av_packet_alloc();

    int ret = 0;

    while ((ret = avcodec_receive_packet(audio_ctx_, pkt)) == 0) {
        cb(pkt);
        av_packet_unref(pkt);
    }
    
    av_packet_free(&pkt);

    return (ret == AVERROR(EAGAIN)) ? 0 : ret;
}

AVRational Encoder::video_time_base() const {
    return video_ctx_->time_base;
}

AVRational Encoder::audio_time_base() const {
    return audio_ctx_->time_base;
}