#pragma once
#include <string>
#include <vector>
#include <mutex>

class PlaylistManager {
public:
    void add(const std::string& path);
    void remove(size_t index);
    std::string getNext();
    bool hasNext() const;
    std::string getCurrent() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> playlist_;
    size_t index_ = 0;
};