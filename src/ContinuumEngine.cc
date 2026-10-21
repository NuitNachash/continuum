#include "ContinuumEngine.h"
#include "logger.h"
#include <thread>
#include <iostream>
#include <mutex>

/* Initializes the complete streaming pipeline:
  - Encoder for video/audio compression
  - Streamer for sending encoded packets
  - Video source for reading video frames
  - Audio source for reading and converting audio frames
  0*/
ContinuumEngine::ContinuumEngine(const config& cfg)
    : cfg_(cfg),
      encoder_(cfg_),
      streamer_(cfg_, encoder_),
      source_(cfg_),
      audioSource_(cfg_, encoder_.audio_time_base()),
      clock_(),
      writer_(streamer_, clock_),
      current_path_(cfg.mp4Path)
{}

// Adds multiple media files to the playback playlist
void ContinuumEngine::loadPlaylist(const std::vector<std::string>& paths) {
    for (size_t i = 1; i < paths.size(); i++)
        playlist_.add(paths[i]);
}

// Adds a single media file to the end of the playlist
void ContinuumEngine::addMedia(const std::string& path) {
    playlist_.add(path);
}

// Reads, timestamps, encodes, and streams one video frame
bool ContinuumEngine::sendOneVideoFrame() {
    AVFrame* frame = source_.nextBuffered();

    if (!frame) {
        if (once_mode_) { stop(); return false; }
        std::string nextPath = playlist_.getNext();
        if (nextPath.empty()) {
            std::string fallbackPath;
            {
                std::lock_guard<std::mutex> lock(path_mutex_);
                fallbackPath = current_path_;
            }
            performSwitch(fallbackPath);
            frame = source_.nextBuffered();
            if (!frame) return false;
        } else {
            performSwitch(nextPath);
            for (int i = 0; i < 10; i++) {
                frame = source_.nextBuffered();
                if (frame) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!frame) return false;
        }
    }

    if (encoder_.sendVideo(frame) < 0){
        LOG_ERROR("[Engine] sendVideo failed for frame pts=" + std::to_string(frame->pts));
        return false;
    }
        

    encoder_.receiveVideo([&](AVPacket* pkt) {
        LOG_DEBUG("[Engine] received video packet pts=" + std::to_string(pkt->pts) + " dts=" + std::to_string(pkt->dts));
        last_video_pts_ = pkt->pts;
        writer_.writeVideo(pkt, encoder_.video_time_base());
    });
    return true;
}

// Encodes and streams one audio frame
bool ContinuumEngine::sendOneAudioFrame(AVFrame* aframe) {
    encoder_.sendAudio(aframe);
    av_frame_free(&aframe);
    encoder_.receiveAudio([&](AVPacket* pkt) {
        last_audio_pts_ = pkt->pts;
        writer_.writeAudio(pkt, encoder_.audio_time_base());
    });
    LOG_DEBUG("[Engine] audio frame nb_samples: " + std::to_string(aframe->nb_samples));
    return true;
}

// Main streaming loop
// Continuously decides whether the next packet should be audio or video
// based on the current timeline position
void ContinuumEngine::start() {
    running_ = true;
    clock_.start();
    LOG_INFO("[Engine] streaming - Ctrl+C to stop");

    audioThreadRunning_ = true;
    audioDecodeThread_ = std::thread([this]() {
        while (audioThreadRunning_) {
            if (!audioSource_.eof_ && audioSource_.fifoSize() < 8192) {
                audioSource_.decodeIntoFifo();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    while (audioSource_.fifoSize() < 8192)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    LOG_INFO("[Engine] FIFO pre-filled: " + std::to_string(audioSource_.fifoSize()) + " samples");

    
    LOG_INFO("[Engine] waiting for audio clock...");
    while (running_ && clock_.audioClockUs() == 0) {
        AVFrame* aframe = audioSource_.next();
        if (aframe) sendOneAudioFrame(aframe);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    LOG_INFO("[Engine] audio clock initialized: " + std::to_string(clock_.audioClockUs()) + "us");

    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (audioSource_.eof_ && audioSource_.fifoSize() == 0) {
            LOG_INFO("[Engine] Audio EOF - triggering switch");
            std::string nextPath = playlist_.getNext();
            if (!nextPath.empty()) {
                performSwitch(nextPath);
            } else {
                std::lock_guard<std::mutex> lock(path_mutex_);
                performSwitch(current_path_);
            }
            continue;
        }

        if (skip_requested_) {
            skip_requested_ = false;
            std::string nextPath = playlist_.getNext();
            if (!nextPath.empty()) performSwitch(nextPath);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        AVFrame* aframe = audioSource_.next();
        if (aframe) {
            sendOneAudioFrame(aframe);
            sendOneVideoFrame();
        } else {
            sendOneVideoFrame();
        }
    }
    audioThreadRunning_ = false;
    audioDecodeThread_.join();
}

// Stops the streaming loop
void ContinuumEngine::stop() {
    running_ = false;
}

// Temp pauses frame output
void ContinuumEngine::pause() {
    paused_ = true;
}

// Resumes frame output
void ContinuumEngine::resume() {
    paused_ = false;
}

// Requests that playback move to the next media item
void ContinuumEngine::skip() {
    skip_requested_ = true;
}

void ContinuumEngine::forceClose(){
    streamer_.forceClose();
}

// Performs a better switch that carries updated media information
void ContinuumEngine::performSwitch(const std::string& nextPath) {
    LOG_INFO("[Engine] performSwitch start: " + nextPath);
    audioThreadRunning_ = false;

    try {
        if (audioDecodeThread_.joinable()) {
            audioDecodeThread_.join();
        }

        {
            std::lock_guard<std::mutex> lock(path_mutex_);
            current_path_ = nextPath;
        }

        // Accumulate pts offset for continuous streaming
        // offset += last_pts - first_pts + one_frame_duration
        int64_t video_frame_dur = 1; // 1 tick at 1/60
        int64_t audio_frame_dur = av_rescale_q(
            1024, {1, cfg_.samplerate}, encoder_.audio_time_base()
        );

        writer_.setVideoOffset(writer_.videoOffset() + last_video_pts_ + video_frame_dur);
        writer_.setAudioOffset(writer_.audioOffset() + last_audio_pts_ + audio_frame_dur);

        video_pts_at_switch_ = writer_.videoOffset();

        LOG_INFO("[Engine] pts offset - video: " + std::to_string(writer_.videoOffset()) +
                 " audio: " + std::to_string(writer_.audioOffset()));

        audioSource_.flushFifo();
        source_.flushBuffer();
        source_.switchFile(nextPath);
        audioSource_.switchFile(nextPath);

        LOG_INFO("[Engine] performSwitch complete: " + nextPath);

        audioThreadRunning_ = true;
        audioDecodeThread_ = std::thread([this]() {
            while (audioThreadRunning_) {
                if (!audioSource_.eof_ && audioSource_.fifoSize() < 8192) {
                    audioSource_.decodeIntoFifo();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        while (audioSource_.fifoSize() < 8192)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        LOG_INFO("[Engine] FIFO refilled after switch: " + std::to_string(audioSource_.fifoSize()));

    } catch (const std::exception& e) {
        LOG_ERROR("[Engine] Switch failed: " + std::string(e.what()));
        audioThreadRunning_ = true;
        audioDecodeThread_ = std::thread([this]() {
            while (audioThreadRunning_) {
                if (!audioSource_.eof_ && audioSource_.fifoSize() < 8192)
                    audioSource_.decodeIntoFifo();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        std::string next = playlist_.getNext();
        performSwitch(!next.empty() ? next : current_path_);
    }
}

// Returns current engine state for monitoring/control
EngineStatus ContinuumEngine::getStatus() const {
    EngineStatus s;
    {
        std::lock_guard<std::mutex> lock(path_mutex_);
        s.current_path = current_path_;
    }
    s.paused = paused_;
    s.running = running_;
    s.video_pts = writer_.videoOffset() + last_video_pts_;
    s.audio_pts = clock_.audioClockUs();
    s.video_pts_since_switch = last_video_pts_;
    s.current_duration = source_.getDuration();
    return s;
}