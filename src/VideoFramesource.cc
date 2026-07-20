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

/*  Creates a video frame source

    Responsible for:
     - Opening the input media file
     - Decoding compressed video packets
     - Converting frames into the encoder's required format
*/

VideoFrameSource::VideoFrameSource(const config& cfg) : cfg_(cfg) {
    fmt_ = nullptr;

    // Allocate reusable FFmpeg packet and frame objects
    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();

    if(!pkt_ || !frame_)
        throw std::runtime_error("av_frame_alloc failed");

    // Open the first media file and initialize decoder
    openFile(cfg.mp4Path);

    std::cout << "[FrameSource] MP4 loaded: " << cfg.mp4Path << '\n';

    // Allocate the output frame used after scaling/conversion
    // The encoder expects YUV420P frames at the configured
    // streaming resolution
    scaled_frame_ = av_frame_alloc();
    scaled_frame_->format = AV_PIX_FMT_YUV420P;
    scaled_frame_->width = cfg_.width;
    scaled_frame_->height = cfg_.height;

    int ret2 = av_frame_get_buffer(scaled_frame_, 32);
    if (ret2 < 0)
        throw std::runtime_error("Failed to allocate scaled frame buffer");
}

// Opens a media file and initializes the video decoder
void VideoFrameSource::openFile(const std::string& path){
    // Open input container
    int ret = avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr);
    if (ret < 0)
        throw std::runtime_error("Failed to open mp4 file");

    // Read stream metadata
    ret = avformat_find_stream_info(fmt_, nullptr);
    if (ret < 0)
        throw std::runtime_error("Failed to find stream info");

    // Locate the video stream inside the container
    video_stream_index_ = -1;

    for (unsigned i = 0; i < fmt_->nb_streams; i++) {
        if (fmt_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index_ = i;
            break;
        }
    }

    if (video_stream_index_ < 0)
        throw std::runtime_error("No video stream found");
        
    // Read the first packet to capture the original starting PTS
    // Used the preserve timing information when needed
    av_read_frame(fmt_, pkt_);
    first_pts_ = pkt_->pts;
    av_packet_unref(pkt_);

    // Return decoder back to the beginning of the file
    av_seek_frame(fmt_, video_stream_index_, 0, AVSEEK_FLAG_BACKWARD);

    AVStream* stream = fmt_->streams[video_stream_index_];

    // Find the decoder matching the input codec
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        throw std::runtime_error("Decoder not found");
    
    // Create decoder context
    dec_ctx_ = avcodec_alloc_context3(codec);
    if (!dec_ctx_)
        throw std::runtime_error("Failed to alloc decoder context");
    
    // Copy stream parameters into decoder context
    ret = avcodec_parameters_to_context(dec_ctx_, stream->codecpar);
    if (ret < 0)
        throw std::runtime_error("Failed to copy codec params");

    // Open the decoder
    ret = avcodec_open2(dec_ctx_, codec, nullptr);
    if (ret < 0)
        throw std::runtime_error("Failed to open decoder");

    // Recreate scaling context when switching files
    if (sws_){
        sws_freeContext(sws_);
        sws_ = nullptr;
    }

    // Create image container
    // Input:
    //  Original video resolution and pixel format
    // Output:
    //  Configured streaming resolution in YUV420P
    sws_ = sws_getContext(
        dec_ctx_->width, dec_ctx_->height, dec_ctx_->pix_fmt,
        cfg_.width, cfg_.height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if(!sws_)
        throw std::runtime_error("Failed to create sws_context");
}

// Releases decoder resources for the current file
void VideoFrameSource::closeFile(){
    if (dec_ctx_) avcodec_free_context(&dec_ctx_);
    if (fmt_) avformat_close_input(&fmt_);
}

// Switches playback to another media file
void VideoFrameSource::switchFile(const std::string& path){
    closeFile();
    openFile(path);
}

// Cleanup all FFmpeg resources
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

// Returns the next decoded video frame

// Workflow:
// 1. Read compressed packet
// 2. Send packet to decoder
// 3. Receive decoded frame
// 4. Convert frame to encoder format
AVFrame* VideoFrameSource::next() {
    while (true) {
        int ret = av_read_frame(fmt_, pkt_);

        // End of file
        if (ret < 0) {
            return nullptr;
        }

        // Ignore audio packets
        if (pkt_->stream_index != video_stream_index_) {
            av_packet_unref(pkt_);
            continue;
        }

        // Send compressed packet to decoder
        ret = avcodec_send_packet(dec_ctx_, pkt_);
        av_packet_unref(pkt_);

        if (ret < 0)
            continue;

        // Retrieve decoded video frame
        ret = avcodec_receive_frame(dec_ctx_, frame_);
        if (ret == 0) {
            frame_->pts -= first_pts_;
            // Convert decoded frame to the format expected
            // by the encoder
            sws_scale(
                sws_,
                frame_->data, frame_->linesize,
                0, dec_ctx_->height,
                scaled_frame_->data, scaled_frame_->linesize
            );
            return scaled_frame_;
        }
    }
}
