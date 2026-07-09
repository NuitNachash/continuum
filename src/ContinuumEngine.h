#pragma once
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include "Config.h"
#include "Encoder.h"
#include "Streamer.h"
#include "VideoFramesource.h"
#include "AudioFramesource.h"
#include "TimelineManager.h"
#include "PlaylistManager.h"

// Represents the current state of the streaming engine
// Used by external controllers or monitoring systems
struct EngineStatus {
    // Currently playing media file
    std::string current_path;

    // Playback control states
    bool paused;
    bool running;

    // Currently audio/video timeline positions
    int64_t video_pts;
    int64_t audio_pts;

    // Video timeline offset since the last media switch
    int64_t video_pts_since_switch;

    // Duration of the currently playing media
    int64_t current_duration;
};

/*Controls the complete media streaming pipeline

  Responsible for:
   - Managing video and audio sources
   - Keeping audio/video timestamps synced
   - Encoding raw frames
   - Sending encoded packets to the streamer
   - Handling playlist changes and playback controls
*/
class ContinuumEngine {
public:

    // Creates the engine and initializes all pipeline components
    explicit ContinuumEngine(const config& cfg);
    
    // Adds multiple files to the playback playlist
    void loadPlaylist(const std::vector<std::string>& paths);

    // Adds a single file to the playback playlist
    void addMedia(const std::string& path);

    // Starts the main streaming loop
    void start();

    // Stops streaming
    void stop();

    // Pauses frame output while keeping the engine active
    void pause();

    // Resumes playback after pause
    void resume();

    // Requests switching to next item in playlist
    void skip();
    
    // Returns current playback and engine information
    EngineStatus getStatus() const;

    // Enables/disables single-file playback mode
    // When enabled, playback stops after the current file ends
    void setOnceMode(bool once) {
        once_mode_ = once;
    }

private:
    // Reads, timestamps, encodes, and streams one video frame
    bool sendOneVideoFrame();

    // Timestamps, encodes, and streams one audio frame
    bool sendOneAudioFrame(AVFrame* aframe);

    // Runtime config settings
    config cfg_;

    // Video/audio encoding pipeline
    Encoder encoder_;

    // Output stream manager responsible for sending packets
    Streamer streamer_;

    // Sources that provide raw decoded frames
    VideoFrameSource source_;
    AudioFrameSource audioSource_;

    // Keeps audio and video timestamps synced
    TimelineManager timeline_;

    // Manages the list of media files to play
    PlaylistManager playlist_;
    
    // Real-world clock time when streaming started
    // Used to sync output timing
    std::chrono::steady_clock::time_point stream_start_;

    // Indicates whether the main streaming loop should continue
    bool running_ = false;
    
    // Protects current_path_ from simultaneous access by
    // the streaming thread and status queries
    mutable std::mutex path_mutex_;

    // Path of the currently playing media file
    std::string current_path_;

    // Atomic flags allow safe communicaiton between control threads
    // and the streaming loop

    // Temp pauses frame output
    std::atomic<bool> paused_ = false;

    // Requests switching to the next media item
    std::atomic<bool> skip_requested_ = false;

    // When enabled, playback stops after one media item finishes
    std::atomic<bool> once_mode_ = false;

    // Stores the video timeline position when a media switch occurs
    // Used for reporting playback relative to current file
    std::atomic<int64_t> video_pts_at_switch = 0;
};