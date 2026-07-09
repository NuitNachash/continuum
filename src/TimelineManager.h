#pragma once
extern "C" {
    #include <libavutil/rational.h>
    #include <libavutil/mathematics.h>
}
#include <cstdint>
#include <atomic>

/*  Maintains separate audio and video presentation timelines

    FFmpeg streams can use different time bases, so audio and video
    timestamps are tracked independently and compared only after 
    converting them using their respective time bases
    
    Atomic variables allow the timeline to be safely accessed or 
    modified from different threads
*/

class TimelineManager {
public:
    // Returns the current presentation timestamp for either
    // the video or audio timeline
    int64_t getPts(bool isVideo) const;

    // Advances the selected timeline by the given ammount
    // Video advancement is typically measured in frames
    // Audio advancement is typically measured in samples
    void advance(bool isVideo, int64_t amount);

    // Sets an offset applied to the selected timeline
    // Used to keep timestamps continuous when switching media files
    void setOffset(bool isVideo, int64_t offset);

    // Returns the current offset for the selected timeline
    int64_t getOffset(bool isVideo) const;

    // Compares audio and video timestamps using their respective
    // FFmpeg time bases

    // Returns:
    //  Negative -> video timeline is behind audio
    //  Positive -> video timeline is ahead of audio
    //  Zero     -> timelines are sync
    int compare(AVRational video_tb, AVRational audio_tb) const;


private:
    // Current video presentation timestamp
    std::atomic<int64_t> video_pts_ = 0;

    // Current audio presentation timestamp
    std::atomic<int64_t> audio_pts_ = 0;

    // Offset added to video timestamps
    // Allows continuous playback across media switches
    std::atomic<int64_t> video_offset_ = 0;

    // Offset added to audio timestamps
    // Allows continuous playback across media switches
    std::atomic<int64_t> audio_offset_ = 0;
};