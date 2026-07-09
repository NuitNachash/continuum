#include "AudioFramesource.h"
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <iostream>

extern "C" {
    #include <libavutil/audio_fifo.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/samplefmt.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswresample/swresample.h>
}

AudioFrameSource::AudioFrameSource(const config& cfg, AVRational tb) : cfg_(cfg), audio_time_base_(tb) {
    pkt_ = av_packet_alloc();
    decoded_frame_ = av_frame_alloc();
    converted_frame_ = av_frame_alloc();

    if (!pkt_ || !decoded_frame_ || !converted_frame_)
        throw std::runtime_error("AudioFrameSource: allocation failed");

    av_frame_get_buffer(converted_frame_, 32);
    openFile(cfg.mp4Path);

    std::cout << "[AudioFrameSource] Audio stream loaded\n";

    initResampler();

    if (!initAudioFifo())
        throw std::runtime_error("Failed to initialize audio FIFO");
}

void AudioFrameSource::initResampler() {
    av_channel_layout_default(&out_ch_layout_, 2);
    
    swr_ = swr_alloc();
    if (!swr_)
        throw std::runtime_error("swr_alloc failed");

    av_opt_set_chlayout(swr_, "out_chlayout", &out_ch_layout_, 0);
    av_opt_set_int(swr_, "out_sample_rate", cfg_.samplerate, 0);
    av_opt_set_sample_fmt(swr_, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    av_opt_set_chlayout(swr_, "in_chlayout", &dec_ctx_->ch_layout, 0);
    av_opt_set_int(swr_, "in_sample_rate", dec_ctx_->sample_rate, 0);
    av_opt_set_sample_fmt(swr_, "in_sample_fmt", dec_ctx_->sample_fmt, 0);

    if (swr_init(swr_) < 0)
        throw std::runtime_error("swr_init failed");
}

AVFrame* AudioFrameSource::next() {
    if (av_audio_fifo_size(audio_fifo_) >= 1024) {
        AVFrame* out = popFifoFrame1024();
        if (out) {
            return out;
        }
    }
    while(true) {
        if (av_read_frame(fmt_, pkt_) < 0) {
            return nullptr;
        }

        if (pkt_->stream_index != audio_stream_index_) {
            av_packet_unref(pkt_);
            continue;
        }

        if (avcodec_send_packet(dec_ctx_, pkt_) < 0) {
            av_packet_unref(pkt_);
            continue;
        }

        av_packet_unref(pkt_);


        int ret = avcodec_receive_frame(dec_ctx_, decoded_frame_);
        if (ret < 0)
            continue;

        const uint8_t * const *in_data = (const uint8_t * const *)decoded_frame_->extended_data;

        av_frame_unref(converted_frame_);
        converted_frame_->nb_samples = av_rescale_rnd(
            swr_get_delay(swr_, dec_ctx_->sample_rate) + decoded_frame_->nb_samples,
            cfg_.samplerate,
            dec_ctx_->sample_rate,
            AV_ROUND_UP
        );
        
        converted_frame_->format = AV_SAMPLE_FMT_FLTP;
        converted_frame_->ch_layout = out_ch_layout_;
        converted_frame_->sample_rate = cfg_.samplerate;

        av_frame_get_buffer(converted_frame_, 0);
        
        av_frame_make_writable(converted_frame_);

        int samples = swr_convert(
            swr_,
            converted_frame_->data,
            converted_frame_->nb_samples,
            decoded_frame_->extended_data,
            decoded_frame_->nb_samples
        );
        if (samples < 0)
            continue;
        converted_frame_->nb_samples = samples;
        
        if (!pushToFifo(converted_frame_))
            continue;

        int samples_per_call = cfg_.samplerate / cfg_.fps;
        AVFrame* out = popFifoFrame1024();

        if(!out) 
            continue;

        return out;
    }
}

bool AudioFrameSource::initAudioFifo() {
    audio_fifo_ = av_audio_fifo_alloc(
            fifo_format_,
            fifo_channels_,
            1
        );
        if (!audio_fifo_) {
            throw std::runtime_error("Failed to allocate AVAudioFifo");
            return false;
        }
        return true;
}

bool AudioFrameSource::pushToFifo(AVFrame* frame) {
    if (!frame || frame->nb_samples <= 0)
                return false;

    int ret = av_audio_fifo_realloc(
        audio_fifo_,
        av_audio_fifo_size(audio_fifo_) + frame->nb_samples
    );
    if (ret < 0) {
        throw std::runtime_error("FIFO realloc failed");
        return false;
    }
    ret = av_audio_fifo_write(
        audio_fifo_,
        (void**)frame->data,
        frame->nb_samples
    );

    if (ret < frame->nb_samples) {
        throw std::runtime_error("Fifo write incomplete");
        return false;
    }
    return true;
}

AVFrame* AudioFrameSource::popFifoFrame1024()
{
    constexpr int AAC_FRAME_SIZE = 1024;

    if (av_audio_fifo_size(audio_fifo_) < AAC_FRAME_SIZE)
        return nullptr;

    AVFrame* frame = av_frame_alloc();
    if (!frame)
        return nullptr;

    frame->nb_samples = AAC_FRAME_SIZE;
    frame->format = fifo_format_;
    frame->ch_layout = out_ch_layout_;
    frame->sample_rate = cfg_.samplerate;

    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    int ret = av_audio_fifo_read(
        audio_fifo_,
        (void**)frame->data,
        AAC_FRAME_SIZE
    );

    if (ret != AAC_FRAME_SIZE) {
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
}

void AudioFrameSource::openFile(const std::string& path){
    if (avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr) < 0)
        throw std::runtime_error("Failed to open mp4");

    if (avformat_find_stream_info(fmt_, nullptr) < 0)
        throw std::runtime_error("Failed to read stream info");

    audio_stream_index_ = -1;
    for (unsigned i = 0; i < fmt_->nb_streams; i++) {
        if (fmt_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index_ = i;
            break;
        }
    }
    
    if (audio_stream_index_ < 0)
        throw std::runtime_error("No audio stream found");

    av_packet_unref(pkt_);
    av_seek_frame(fmt_, audio_stream_index_, 0, AVSEEK_FLAG_BACKWARD);
    
    AVStream* stream = fmt_->streams[audio_stream_index_];

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) 
        throw std::runtime_error("Decoder not found");

    dec_ctx_ = avcodec_alloc_context3(codec);
    if (!dec_ctx_)
        throw std::runtime_error("Failed to alloc decoder context");
    
    if(avcodec_parameters_to_context(dec_ctx_, stream->codecpar) < 0) {
        throw std::runtime_error("Failed to copy codec params");
    }
        

    if (avcodec_open2(dec_ctx_, codec, nullptr) < 0) {
        throw std::runtime_error("Failed to open decoder");
    }
}

void AudioFrameSource::closeFile() {
    if(dec_ctx_) avcodec_free_context(&dec_ctx_);
    if (fmt_) avformat_close_input(&fmt_);
}

void AudioFrameSource::switchFile(const std::string& path) {
    closeFile();
    openFile(path);
    if (swr_) {
        swr_free(&swr_);
    }

    initResampler();
    av_audio_fifo_reset(audio_fifo_);
}



AudioFrameSource::~AudioFrameSource() {
    if (swr_)
        swr_free(&swr_);

    if (dec_ctx_)
        avcodec_free_context(&dec_ctx_);

    if (fmt_)
        avformat_close_input(&fmt_);

    if (pkt_)
        av_packet_free(&pkt_);

    if (decoded_frame_)
        av_frame_free(&decoded_frame_);

    if (converted_frame_)
        av_frame_free(&converted_frame_);
    
    if (audio_fifo_)
        av_audio_fifo_free(audio_fifo_);

}