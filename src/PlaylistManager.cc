#include "PlaylistManager.h"

/*  Adds a new media file path to the playlist
    Treats the playlist like a queue and only loops current video
    if there is not a next video in queue
    
    A mutex is used because playlist modificiations may happen
    from a control thread while the streaming thread is reading
    from the playlist
*/

void PlaylistManager::add(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(path);
}

// Removes the media item from the playlist by its index
void PlaylistManager::remove(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove items from queue
    if (index < queue_.size())
        queue_.erase(queue_.begin() + index);
}

// Returns the next media file in the queue
// Playback will loop on current video if no next video in queue
// playlist is reached, allowing continuous looping playback
std::string PlaylistManager::getNext() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return "";

    std::string next = queue_.front();
    queue_.pop_front();
    return next;
}

// Checks whether the playlist contains any media files
bool PlaylistManager::hasNext() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !queue_.empty();
}

// Returns the currently selected playlist item
std::string PlaylistManager::getCurrent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty() ? "" : queue_.front();
}