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

struct EngineStatus {
    std::string current_path;
    bool paused;
    bool running;
    int64_t video_pts;
    int64_t audio_pts;
    int64_t video_pts_since_switch;
    int64_t current_duration;
};

class ContinuumEngine {
public:
    explicit ContinuumEngine(const config& cfg);
    
    void loadPlaylist(const std::vector<std::string>& paths);
    void addMedia(const std::string& path);
    void start();
    void stop();
    void pause();
    void resume();
    void skip();
    
    EngineStatus getStatus() const;

    void setOnceMode(bool once) {
        once_mode_ = once;
    }

private:
    bool sendOneVideoFrame();
    bool sendOneAudioFrame(AVFrame* aframe);

    config cfg_;
    Encoder encoder_;
    Streamer streamer_;
    VideoFrameSource source_;
    AudioFrameSource audioSource_;
    TimelineManager timeline_;
    PlaylistManager playlist_;
    

    std::chrono::steady_clock::time_point stream_start_;
    bool running_ = false;
    
    mutable std::mutex path_mutex_;
    std::string current_path_;

    std::atomic<bool> paused_ = false;
    std::atomic<bool> skip_requested_ = false;

    std::atomic<bool> once_mode_ = false;
    std::atomic<int64_t> video_pts_at_switch = 0;
};