#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include <functional>
#include <string>
#include "Config.h"

/* Handles encoding of raw audio and video frames into compressed formats

	Video:
   		Raw frames -> H.264 encoded packets
	
	Audio:
		Raw PCM samples -> AAC encoded packets
	
	The encoded packets are passed back through callbacks so another
	component (such as streamer) can send them to the output
*/
class Encoder {
public:

	// Function type used to return encoded packets to the caller
	// The callback does not take ownership of the packet
	using PacketCallback = std::function<void(AVPacket*)>;

	// Creates and initializes the audio and video encoders
	explicit Encoder(const config& cfg);

	// Releases encoder contexts and allocated FFmpeg resources
	~Encoder();

	// Encoder owns FFmpeg contexts, so copying is disabled
	Encoder(const Encoder&) = delete;
	Encoder& operator=(const Encoder&) = delete;

	// Sends an uncompressed video frame into the encoder
	// Encoded packets can later be retrieved with receiveVideo()
	int sendVideo(AVFrame* frame);

	// Sends an uncompressed audio frame into the encoder
	// Encoded packets can later be retrieved with receiveAudio()
	int sendAudio(AVFrame* frame);

	// Retrieves all currently available encoded video packts
	// Each packet is passed to the provided callback
	int receiveVideo(const PacketCallback& cb);

	// Retrieves all currently available encoded audio packts
	// Each packet is passed to the provided callback
	int receiveAudio(const PacketCallback& cb);

	// Returns the timestamp scale used for video encoding
	AVRational video_time_base() const;

	// Returns the timestamp scale used for audio encoding
	AVRational audio_time_base() const;
	
	// Provides access to the underlying FFmpeg video encoder context
	// Used when other components need encoder metadata
	AVCodecContext* video_context() const { return video_ctx_; }

	// Provides access to the underlying FFmpeg audio encoder context
	// Used when other components need encoder metadata
	AVCodecContext* audio_context() const { return audio_ctx_; }

private:
	// FFmpeg context responsible for H.264 video encoding
	AVCodecContext* video_ctx_ = nullptr;

	// FFmpeg context responsible for AAC audio encoding
	AVCodecContext* audio_ctx_ = nullptr;
};