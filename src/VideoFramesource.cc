#include "VideoFramesource.h"
#include "logger.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
    #include <libavfilter/avfilter.h>
    #include <libavfilter/buffersrc.h>
    #include <libavfilter/buffersink.h>
}

/*  Creates a video frame source

    Responsible for:
     - Opening the input media file
     - Decoding compressed video packets
     - Converting frames into the encoder's required format
*/

// Create video filter graph
AVFilterGraph* filter_graph = avfilter_graph_alloc();
AVFilterContext* buffersrc_ctx = nullptr;
AVFilterContext* buffersink_ctx = nullptr;
AVFrame* filter_frame = nullptr;
void initFilterGraph();
void freeFilterGraph();
    
VideoFrameSource::VideoFrameSource(const config& cfg) : cfg_(cfg) {
    fmt_ = nullptr;

    // Allocate reusable FFmpeg packet and frame objects
    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();

    if(!pkt_ || !frame_)
        throw std::runtime_error("av_frame_alloc failed");

    // Open the first media file and initialize decoder
    openFile(cfg.mp4Path);

    LOG_INFO("[FrameSource] MP4 loaded: " + cfg.mp4Path);

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

    filter_frame = av_frame_alloc();
}

// Opens a media file and initializes the video decoder
void VideoFrameSource::openFile(const std::string& path){


    // Open input container
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "timeout", "200000", 0);
    if (avformat_open_input(&fmt_, path.c_str(), nullptr, &opts) < 0) {
        av_dict_free(&opts);
        throw std::runtime_error("Failed to open file: " + path);
    }
    av_dict_free(&opts);

    // Read stream metadata
    int ret = avformat_find_stream_info(fmt_, nullptr);
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
    /*av_read_frame(fmt_, pkt_);
    first_pts_ = pkt_->pts;
    av_packet_unref(pkt_);*/



    while (av_read_frame(fmt_, pkt_) >= 0) {
        if (pkt_->stream_index == video_stream_index_) {
            first_pts_ = pkt_->pts;
            av_packet_unref(pkt_);
            break;
        }
        av_packet_unref(pkt_);
    }
    av_seek_frame(fmt_, video_stream_index_, first_pts_, AVSEEK_FLAG_BACKWARD);
    
    

    AVStream* stream = fmt_->streams[video_stream_index_];
    first_pts_ = AV_NOPTS_VALUE;

    // Store video timebase
    src_time_base_ = fmt_->streams[video_stream_index_]->time_base;

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

    dec_ctx_->err_recognition = AV_EF_IGNORE_ERR;
    avcodec_flush_buffers(dec_ctx_);
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

       
    if (scaled_frame_) av_frame_free(&scaled_frame_);
    scaled_frame_ = av_frame_alloc();
    scaled_frame_->format = AV_PIX_FMT_YUV420P;
    scaled_frame_->width = cfg_.width;
    scaled_frame_->height = cfg_.height;
    av_frame_get_buffer(scaled_frame_, 32);

    initFilterGraph();

}

// Releases decoder resources for the current file
void VideoFrameSource::closeFile(){
    if (dec_ctx_) avcodec_free_context(&dec_ctx_);
    if (fmt_) avformat_close_input(&fmt_);
}

// Switches playback to another media file
void VideoFrameSource::switchFile(const std::string& path){
    flushing_ = true;
    flushBuffer();
    avcodec_flush_buffers(dec_ctx_);
    closeFile();
    openFile(path);
    flushing_ = false;
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
    //LOG_DEBUG("[VideoFrameSource] next() called");
    while (true) {
        int ret = av_read_frame(fmt_, pkt_);

        /* Trying new initial read packet source */
        
        if (ret < 0) {

            // Tell decoder there are no more packets.
            avcodec_send_packet(dec_ctx_, nullptr);

            // Drain all remaining decoded frames.
            while (avcodec_receive_frame(dec_ctx_, frame_) == 0) {

                // Validate frame
                if (frame_->width <= 0 ||
                    frame_->height <= 0 ||
                    !frame_->data[0] ||
                    !frame_->data[1] ||
                    !frame_->data[2] ||
                    frame_->linesize[0] <= 0) {

                    LOG_WARN(
                        "[VideoFrameSource] invalid drained frame, skipping"
                    );
                    continue;
                }
                int64_t source_pts = frame_->best_effort_timestamp;

                if (source_pts == AV_NOPTS_VALUE)
                    source_pts = frame_->pts;

                if (source_pts == AV_NOPTS_VALUE) {
                    LOG_WARN(
                        "[VideoFrameSource] drained frame has no valid PTS"
                    );
                    continue;
                }

                // First actual decoded frame establishes first_pts_.
                if (first_pts_ == AV_NOPTS_VALUE) {
                    first_pts_ = source_pts;

                    LOG_DEBUG(
                        "[VideoFrameSource] first decoded PTS: " +
                        std::to_string(first_pts_)
                    );
                }

                frame_->pts = source_pts;

                int ret2 = 0;

                try {
                    ret2 = sws_scale(
                        sws_,
                        frame_->data,
                        frame_->linesize,
                        0,
                        dec_ctx_->height,
                        scaled_frame_->data,
                        scaled_frame_->linesize
                    );
                } catch (...) {
                    LOG_WARN(
                        "[VideoFrameSource] sws_scale exception on "
                        "drained frame, skipping"
                    );
                    continue;
                }

                if (ret2 <= 0) {
                    LOG_WARN(
                        "[VideoFrameSource] sws_scale failed on "
                        "drained frame, skipping"
                    );
                    continue;
                }

                // We return scaled_frame_, not frame_, so preserve PTS.
                scaled_frame_->pts = frame_->pts;

                return scaled_frame_;
            }

            // No buffered frames remain.
            return nullptr;
        }
        if (pkt_->stream_index != video_stream_index_) {
            av_packet_unref(pkt_);
            continue;
        }

        ret = avcodec_send_packet(dec_ctx_, pkt_);

        av_packet_unref(pkt_);

        if (ret < 0) {
            LOG_WARN(
                "[VideoFrameSource] avcodec_send_packet failed"
            );
            continue;
        }

        ret = avcodec_receive_frame(dec_ctx_, frame_);

        // Decoder needs more packets.
        if (ret == AVERROR(EAGAIN)) {
            LOG_DEBUG(
                "[VideoFrameSource] EAGAIN - needs more packets"
            );
            continue;
        }

        // Actual decoder error.
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));

            LOG_WARN(
                "[VideoFrameSource] receive_frame failed: " +
                std::string(err)
            );

            return nullptr;
        }

        if (ret == 0) {

            // Validate frame
            if (frame_->width <= 0 ||
                frame_->height <= 0 ||
                !frame_->data[0] ||
                !frame_->data[1] ||
                !frame_->data[2] ||
                frame_->linesize[0] <= 0) {

                LOG_WARN(
                    "[VideoFrameSource] invalid frame data, skipping"
                );
                continue;
            }

            int64_t source_pts = frame_->best_effort_timestamp;

            if (source_pts == AV_NOPTS_VALUE)
                source_pts = frame_->pts;

            if (source_pts == AV_NOPTS_VALUE) {
                LOG_WARN(
                    "[VideoFrameSource] frame has no valid PTS, skipping"
                );
                continue;
            }

            if (first_pts_ == AV_NOPTS_VALUE) {
                first_pts_ = source_pts;

                LOG_DEBUG(
                    "[VideoFrameSource] first decoded PTS: " +
                    std::to_string(first_pts_)
                );
            }

            frame_->pts = source_pts;

            int ret2 = 0;

            try {
                ret2 = sws_scale(
                    sws_,
                    frame_->data,
                    frame_->linesize,
                    0,
                    dec_ctx_->height,
                    scaled_frame_->data,
                    scaled_frame_->linesize
                );
            } catch (...) {
                LOG_WARN(
                    "[VideoFrameSource] sws_scale exception on "
                    "corrupted frame, skipping"
                );
                continue;
            }

            if (ret2 <= 0) {
                LOG_WARN(
                    "[VideoFrameSource] sws_scale failed, skipping"
                );
                continue;
            }

            // We return scaled_frame_, so copy the source PTS to it.
            scaled_frame_->pts = frame_->pts;

            return scaled_frame_;
        }
    }
}

AVFrame* VideoFrameSource::nextBuffered() {
    //LOG_DEBUG("[nextBuffered] buffer size: " + std::to_string(frame_buffer_.size()));
    if (flushing_)
        return nullptr;
    
    while ((int)frame_buffer_.size() < BUFFER_SIZE) {
        AVFrame* f = next();
        if (!f)
            break;

        AVFrame* copy = av_frame_clone(f);
        frame_buffer_.push(copy);
    }

    if (frame_buffer_.empty())
        return nullptr;

    AVFrame* out = frame_buffer_.front();
    frame_buffer_.pop();
    return out;
}

void VideoFrameSource::initFilterGraph(){
    freeFilterGraph();
    AVStream* st = fmt_->streams[video_stream_index_];
    AVRational tb = st->tiem_base;
    AVRational sar = dec_ctx_->sample_aspect_ratio;
    if (sar.num == 0) sar = {1, 1};

    char args[512];
    snprintf(args, sizeof(args),
        "video_size=%d%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        dec_ctx_->width, dec_ctx_->height, dec_ctx_->pix_fmt,
        tb.num, tb.den, sar.num, sar.den);
    
    filter_graph_ = avfilter_graph_alloc();
    if (!filter_graph_){
        LOG_ERROR("[VideoFrameSource] Failed to allocate filter graph");
        return;
    }
    if(avfilter_graph_create_filter(&buffersrc_ctx_, avfilter_get_by_name("buffer"), "in", args, nullptr, filter_graph_) < 0){
        LOG_ERROR("[VideoFrameSource] Failed to create buffer source");
        return;
    }
    if(avfilter_graph_create_filter(&buffersink_ctx_, avfilter_get_by_name("buffersink"), "out", nullptr, nullptr, filter_graph_) < 0){
        LOG_ERROR("[VideoFrameSource] Failed to create buffer sink");
        return;
    }

    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};
    av_opt_set_int_list(buffersink_ctx_, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    std::string desc = "setpts=PTS-STARTPTS,fps=" + std::to_string(cfg_.fps) + ",scale" + std::to_string(cfg_.width) + ":" + std::to_string(cfg_.height);

    int ret = avfilter_graph_parse_ptr(filter_graph_, desc.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0){
        LOG_ERROR("[VideoFrameSource] Filter parse failed")
    }

    if (avfilter_graph_config(filter_graph_, nullptr) < 0){
        LOG_ERROR("[VideoFrameSource] Filter config failed")
    }
    AVRational stb = av_buffersink_get_time_base(buffersink_ctx_);
    LOG_INFO("[VideoFrameSource] Filter sink time_base=" + std::to_string(stb.num) + "/" + std::to_string(stb.den));
}

void VideoFrameSource::freeFilterGraph(){
    if (filter_graph_){
        avfilter_graph_free(&filter_graph_);
        buffersrc_ctx_ = buffersink_ctx_ = nullptr;
    }
}
void VideoFrameSource::flushBuffer() {
    while(!frame_buffer_.empty()) {
        av_frame_free(&frame_buffer_.front());
        frame_buffer_.pop();
    }
}
