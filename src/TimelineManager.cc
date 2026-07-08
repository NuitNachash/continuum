#include "TimelineManager.h"

int64_t TimelineManager::getPts(bool isVideo) const {
    if (isVideo)
        return video_offset_.load() + video_pts_.load();
    else
        return audio_offset_.load() + audio_pts_.load();
}

void TimelineManager::advance(bool isVideo, int64_t amount) {
    if (isVideo) video_pts_ += amount;
    else audio_pts_ += amount;
}

void TimelineManager::setOffset(bool isVideo, int64_t offset) {
    if (isVideo) video_offset_ = offset;
    else audio_offset_ = offset;
}

int64_t TimelineManager::getOffset(bool isVideo) const {
    return isVideo ? video_offset_ : audio_offset_;
}

int TimelineManager::compare(AVRational video_tb, AVRational audio_tb) const {
    return av_compare_ts(getPts(true), video_tb, getPts(false), audio_tb);
}