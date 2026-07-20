#include "TimelineManager.h"

/*  Returns the current presentation timestamp (PTS) for either
    the audio or video timeline
    
    The final timestamp is calculated as:
        current offset + current stream position
    
    Offsets allow the timeline to continue correctly when switching
    between different media files
*/

int64_t TimelineManager::getPts(bool isVideo) const {
    if (isVideo)
        return video_offset_.load() + video_pts_.load();
    else
        return audio_offset_.load() + audio_pts_.load();
}

// Advances either the audio or video timeline by the given amount
// For video, amount usually represents the number of frames
// For audio, ammount represents the number of samples
void TimelineManager::advance(bool isVideo, int64_t amount) {
    if (isVideo) video_pts_ += amount;
    else audio_pts_ += amount;
}

// Sets a timeline offset for either audio or video
// Used when restarting or switching media so timestamps remain
// continuous instead of starting back at zero
void TimelineManager::setOffset(bool isVideo, int64_t offset) {
    if (isVideo) video_offset_ = offset;
    else audio_offset_ = offset;
}

// Returns the current offset applied to the selected timeline
int64_t TimelineManager::getOffset(bool isVideo) const {
    return isVideo ? video_offset_ : audio_offset_;
}

// Compares the current audio and video timestamps

// FFmpeg timestamps use differnt time bases for different streams
// so av_compare_ts() converts them internally before comparing

// Returns:
//  < 0 : video is behind audio
//  > 0 : video is ahead of audio
//  = 0 : audio and video are sync
int TimelineManager::compare(AVRational video_tb, AVRational audio_tb) const {
    return av_compare_ts(
        getPts(true), 
        video_tb, 
        getPts(false), 
        audio_tb
    );
}

// Used to nudge the audio back closer to video to prevent desync
void TimelineManager::nudgeAudioPts(int64_t delta) {
    audio_pts_ -= delta;
}

// Resets audio PTS to match video on switch to prevent desync
void TimelineManager::setAudioPts(int64_t pts) {
    audio_pts_ = pts;
}

// Resets audio PTS to match video on switch to prevent desync
void TimelineManager::setVideoPts(int64_t pts) {
    video_pts_ = pts;
}

