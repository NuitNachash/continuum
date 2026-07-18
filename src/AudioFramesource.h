#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
#include <libavutil/audio_fifo.h>
}

#include "Config.h"

// Responsible for reading audio from a media file, decoding it,
// converting it into a format suitable for encoding, and supplying
// fixed-size audio frames (1024 samples) on demand

class AudioFrameSource {
public:
    // Creates the audio source using the provided config
    // and the encoder's audio time base
    explicit AudioFrameSource(const config& cfg, AVRational audio_time_base);

    // Releases all FFmpeg resources
    ~AudioFrameSource();

    // Time base used by the audio encoder
    AVRational audio_time_base_;

    // Prevent copying since this class owns FFmpeg resources
    AudioFrameSource(const AudioFrameSource&) = delete;
    AudioFrameSource& operator=(const AudioFrameSource&) = delete;

    // Returns the next audio frame ready for encoding
    // Each returned frame contains 1024 resampled audio samples
    AVFrame* next();

    // Switches to a different audio file while reusing same object
    void switchFile(const std::string& path);

    // Returns the number of samples currently bufferd in FIFO
    int fifoSize() const {
        return av_audio_fifo_size(audio_fifo_);
    }

    // Flush left over stale audio 
    void flushFifo();

private:
    // Initializes the decoder 
    void initDecoder();

    // Configures the resampler to convert decoded audio into the
    // encoder's requred format
    void initResampler();

    // Converts a decoded rame into the encoder format
    void convertToEncoderFormat(AVFrame* decoded);

    // Opens a media file, finds the audio stream,
    // and initializes the encoder
    void openFile(const std::string& path);

    // Closes the current media file and frees decoder resources
    void closeFile();

private:
    // User-provided encoding configuration
    config cfg_;

    // Media container 
    AVFormatContext* fmt_ = nullptr;

    // Audio decoder context
    AVCodecContext* dec_ctx_ = nullptr;

    // Converts decoded audio into the desired sample format,
    // channel layout, and sample rate
    SwrContext* swr_ = nullptr;

    // Reusable frame for decoded PCM audio
    AVFrame* decoded_frame_ = nullptr;

    // Reusable frame fro converted/resampled audio
    AVFrame* converted_frame_ = nullptr;

    // Reusable compressed packet read from the media file
    AVPacket* pkt_ = nullptr;

    // Desired output channel layout (stereo)
    AVChannelLayout out_ch_layout_;

    // Index of the selected auio stream inside the media container
    int audio_stream_index_ = -1;

    // Creates the FIFO used to accumulate converted samples
    bool initAudioFifo();

    // Appends converted samples into the FIFO
    bool pushToFifo(AVFrame* frame);

    // Removes one encoder-sized frame (1024 samples) from the FIFO
    AVFrame* popFifoFrame1024();

    // FIFO storing converted audio until enough smaples exist
    // to produce a complete encoder frame
    AVAudioFifo* audio_fifo_ = nullptr;

    // FIFO audio format 
    int fifo_channels_ = 2;
    AVSampleFormat fifo_format_ = AV_SAMPLE_FMT_FLTP;

    // Path of the currently opened audio file
    std::string current_path_;
};
