#include "VideoFramesource.h"

#include <cstring>
#include <stdexcept>
#include <iostream>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

VideoFrameSource::VideoFrameSource(const config& cfg) : cfg_(cfg) {
    fmt_ = nullptr;
    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();

    if(!pkt_ || !frame_)
        throw std::runtime_error("av_frame_alloc failed");

    
    openFile(cfg.mp4Path);



    std::cout << "[FrameSource] MP4 loaded: " << cfg.mp4Path << '\n';

    //sws_ = sws_getContext(
    //   dec_ctx_->width,
    //    dec_ctx_->height,
    //    dec_ctx_->pix_fmt,

    //    cfg.width,
    //    cfg.height,
    //    AV_PIX_FMT_YUV420P,

    //    SWS_BILINEAR,
    //    nullptr,
    //    nullptr,
    //    nullptr
    //);

    //if (!sws_)
    //    throw std::runtime_error("Failed to create sws_context");

    scaled_frame_ = av_frame_alloc();
    scaled_frame_->format = AV_PIX_FMT_YUV420P;
    scaled_frame_->width = cfg_.width;
    scaled_frame_->height = cfg_.height;

    int ret2 = av_frame_get_buffer(scaled_frame_, 32);
    if (ret2 < 0)
        throw std::runtime_error("Failed to allocate scaled frame buffer");
    
}

void VideoFrameSource::openFile(const std::string& path){
    int ret = avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr);
    if (ret < 0)
        throw std::runtime_error("Failed to open mp4 file");

    ret = avformat_find_stream_info(fmt_, nullptr);
    if (ret < 0)
        throw std::runtime_error("Failed to find stream info");

    video_stream_index_ = -1;

    for (unsigned i = 0; i < fmt_->nb_streams; i++) {
        if (fmt_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index_ = i;
            break;
        }
    }

    if (video_stream_index_ < 0)
        throw std::runtime_error("No video stream found");
        
    av_read_frame(fmt_, pkt_);
    first_pts_ = pkt_->pts;
    av_packet_unref(pkt_);
    av_seek_frame(fmt_, video_stream_index_, 0, AVSEEK_FLAG_BACKWARD);

    
    AVStream* stream = fmt_->streams[video_stream_index_];

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        throw std::runtime_error("Decoder not found");
    
    dec_ctx_ = avcodec_alloc_context3(codec);
    if (!dec_ctx_)
        throw std::runtime_error("Failed to alloc decoder context");
    
    ret = avcodec_parameters_to_context(dec_ctx_, stream->codecpar);
    if (ret < 0)
        throw std::runtime_error("Failed to copy codec params");

    ret = avcodec_open2(dec_ctx_, codec, nullptr);
    if (ret < 0)
        throw std::runtime_error("Failed to open decoder");

    if (sws_){
        sws_freeContext(sws_);
        sws_ = nullptr;
    }

    sws_ = sws_getContext(
        dec_ctx_->width, dec_ctx_->height, dec_ctx_->pix_fmt,
        cfg_.width, cfg_.height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if(!sws_)
        throw std::runtime_error("Failed to create sws_context");
}

void VideoFrameSource::closeFile(){
    if (dec_ctx_) avcodec_free_context(&dec_ctx_);
    if (fmt_) avformat_close_input(&fmt_);
}

void VideoFrameSource::switchFile(const std::string& path){
    //int64_t prev_end_pts = pts_offset_ + frame_count_;
    closeFile();
    openFile(path);
    //frame_count_ = 0;
    //pts_offset_ = prev_end_pts;
}

VideoFrameSource::~VideoFrameSource() {
    if (dec_ctx_)
        avcodec_free_context(&dec_ctx_);
    if(fmt_)
        avformat_close_input(&fmt_);
    if(pkt_)
        av_packet_free(&pkt_);
    if(frame_)
        av_frame_free(&frame_);
    if(scaled_frame_)
        av_frame_free(&scaled_frame_);
    if(sws_)
        sws_freeContext(sws_);
}

AVFrame* VideoFrameSource::next() {
    while (true) {
        int ret = av_read_frame(fmt_, pkt_);
        if (ret < 0) {
            return nullptr;
            //av_seek_frame(fmt_, video_stream_index_, 0, AVSEEK_FLAG_BACKWARD);
            //avcodec_flush_buffers(dec_ctx_);
            //continue;
        }

        if (pkt_->stream_index != video_stream_index_) {
            av_packet_unref(pkt_);
            continue;
        }

        ret = avcodec_send_packet(dec_ctx_, pkt_);
        av_packet_unref(pkt_);

        if (ret < 0)
            continue;

        ret = avcodec_receive_frame(dec_ctx_, frame_);
        if (ret == 0) {
            sws_scale(
                sws_,
                frame_->data, frame_->linesize,
                0, dec_ctx_->height,
                scaled_frame_->data, scaled_frame_->linesize
            );
            //scaled_frame_->pts = pts_offset_ + frame_count_++;
            return scaled_frame_;
        }
    }
}

void VideoFrameSource::fill() {
    for (int y = 0; y < frame_->height; y++) {
		memset(frame_->data[0] + y * frame_->linesize[0], 16, frame_->width);
	}

	for (int y = 0; y < frame_->height / 2; y++) {
		memset(frame_->data[1] + y * frame_->linesize[1], 128, frame_->width / 2);
		memset(frame_->data[2] + y * frame_->linesize[2], 128, frame_->width / 2);
	}

}