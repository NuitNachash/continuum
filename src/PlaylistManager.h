#pragma once
#include <string>
#include <vector>
#include <mutex>

/*  Thread-safe manager for storing and navigating through
    a list of media files
    
    Used by the streaming engine to support playlist playback
    and dynamic media additions while streaming is active
*/

class PlaylistManager {
public:
    // Adds a media file path to the end of the playlist
    void add(const std::string& path);

    // Removes a media file from the playlist using its index
    void remove(size_t index);

    // Returns the next media file in the playlist
    // Wraps around to the beginning whent he end is reached
    std::string getNext();

    // Checks whether the playlist contains any media items
    bool hasNext() const;

    // Returns the currently selected media file
    std::string getCurrent() const;

private:
    // Protects playlist data from concurrent access
    // The playlist can be modified while streaming is running
    mutable std::mutex mutex_;

    // Collection of media file paths to play
    std::vector<std::string> playlist_;

    // Current position in the playlist
    // Used to determine which item should be return next
    size_t index_ = 0;
};