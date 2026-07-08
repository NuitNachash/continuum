#include "PlaylistManager.h"

void PlaylistManager::add(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    playlist_.push_back(path);
}

void PlaylistManager::remove(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < playlist_.size())
        playlist_.erase(playlist_.begin() + index);
}

std::string PlaylistManager::getNext() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (playlist_.empty()) return "";
    index_ = (index_ + 1) % playlist_.size();
    return playlist_[index_];
}

bool PlaylistManager::hasNext() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !playlist_.empty();
}

std::string PlaylistManager::getCurrent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playlist_.empty() ? "" : playlist_[index_];
}