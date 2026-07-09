#include "PlaylistManager.h"

/*  Adds a new media file path to the playlist
    
    A mutex is used because playlist modificiations may happen
    from a control thread while the streaming thread is reading
    from the playlist
*/

void PlaylistManager::add(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    playlist_.push_back(path);
}

// Removes the media item from the playlist by its index
void PlaylistManager::remove(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Only remove valid playlist entries
    if (index < playlist_.size())
        playlist_.erase(playlist_.begin() + index);
}

// Returns the next media file in the playlist
// Playback wraps around to the beginning whent he end of the
// playlist is reached, allowing continuous looping playback
std::string PlaylistManager::getNext() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (playlist_.empty()) return "";

    // Advance to the next item and wrap around at the end
    index_ = (index_ + 1) % playlist_.size();
    return playlist_[index_];
}

// Checks whether the playlist contains any media files
bool PlaylistManager::hasNext() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !playlist_.empty();
}

// Returns the currently selected playlist item
std::string PlaylistManager::getCurrent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playlist_.empty() ? "" : playlist_[index_];
}