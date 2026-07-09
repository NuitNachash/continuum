#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include "Config.h"
#include "Encoder.h"

/*  Handles output of encoded audio/video packets to a streaming destination

    The Streamer is responsible for:
     - Creating the output container (FLV or RTMP)
     - Creating output audio/video streams
     - Writing encoded packets to the network
     
    It does not perform encoding; it receives already encoded packets
    from the Encoder class
*/

class Streamer {
public:
    // Initializes the RTMP output stream using encoder parameters
    Streamer(const config& cfg, const Encoder& encoder);

    // Finalizes the stream and releases FFmpeg resources
    ~Streamer();

    // Writes an encoded packet to the output stream
    // Pakcets should already be encoded (H.264/AAC) before being passed here
    int write(AVPacket* pkt);

    // Returns the FFmpeg video stream used by the output container
    // Used by the engine to assign packet stream indexes
    AVStream* video_stream() const { return video_stream_; }

    // Returns the FFmpeg audio steream used by the output container
    AVStream* audio_stream() const { return audio_stream_; }

    // Returns the timestamp scale used for video packets
    AVRational time_base_video() const { return video_stream_->time_base; }

    // Returns the timestamp scale used for audio packets
    AVRational time_base_audio() const { return audio_stream_->time_base; }

private:
    // FFmpeg output context respresenting the RTMP/FLV stream
    AVFormatContext* fmt_ = nullptr;

    // Output video stream containing H.264 metadata
    AVStream* video_stream_ = nullptr;

    // Output audio stream containing AAC metadata
    AVStream* audio_stream_ = nullptr;
};