#pragma once

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/audio_fifo.h>
    #include <libswscale/swscale.h>
}
#include "Config.h"

/*  Provides decoded video frames from a media file

    Responsibilities:
     - Open and manage the input video file
     - Decode compressed video packets
     - Convert frames inot the format required by the encoder
     - Support switching between media files during playback
*/
class VideoFrameSource {
public:
    // Creates a video source using the configured media file
    explicit VideoFrameSource(const config& cfg);

    // Releases FFmpeg decoding resources
    virtual ~VideoFrameSource();

    // Copying is disabled because FFmpeg contexts contain
    // owned resources that cannot safely be duplicated
    VideoFrameSource(const VideoFrameSource&) = delete;
    VideoFrameSource& operator=(const VideoFrameSource&) = delete;

    // Returns the next decoded and converted video frame
    // Returns nullptr when the end of the current file is reached
    AVFrame* next();

    // Returns the number of frames processed so far
    int64_t frame_count() const {
        return frame_count_;
    }

    // Switches decoding to another media file
    // The current decoder state is released and recreated
    // for the new file
    void switchFile(const std::string& path);

    // Returns the duration of the currently opened media file
    // FFmpeg stores duration in AV_TIME_BASE units
    // Returns zero when duration information is unavailable
    int64_t getDuration() const {
        if (!fmt_ || fmt_->duration == AV_NOPTS_VALUE)
            return 0;
        return fmt_->duration;
    }

private:
    // Generates a blank frame
    // Used as a helper for fallback frame generation
    virtual void fill();

    // Converts decoded frames into the required output format
    // Handles resolution changes and pixel foramt conversion
    // using FFmpeg's libswscale
    SwsContext* sws_ = nullptr;

    // Frame after scaling/conversion
    // This is the frame returned to the encoder
    AVFrame* scaled_frame_ = nullptr;

    // Raw decoded frame received from FFmpeg decoder
    AVFrame* frame_ = nullptr;

    // Input media container context
    AVFormatContext* fmt_ = nullptr;

    // Video decoder context
    AVCodecContext* dec_ctx_ = nullptr;

    // Temporary compressed packet read from the input file
    AVPacket* pkt_ = nullptr;

    // Index of the video stream inside the input container
    int video_stream_index_ = -1;

    // Runtime configuration values
    config cfg_;

    // Number of frames processed by its source
    int64_t frame_count_ = 0;

    // Orginal starting presentation timestamp of the file
    // Used when preserving timing information during playback
    int64_t first_pts_ = 0;

    // Opens a media file and initializes the decoder
    void openFile(const std::string& path);

    // Releases decoder and input file resources
    void closeFile();
};