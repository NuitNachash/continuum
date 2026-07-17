#include "ContinuumEngine.h"
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
      current_path_(cfg.mp4Path)
{}

// Adds multiple media files to the playback playlist
void ContinuumEngine::loadPlaylist(const std::vector<std::string>& paths) {
    for (const auto& p : paths)
        playlist_.add(p);
}

// Adds a single media file to the end of the playlist
void ContinuumEngine::addMedia(const std::string& path) {
    playlist_.add(path);
}

// Reads, timestamps, encodes, and streams one video frame
bool ContinuumEngine::sendOneVideoFrame() {
    // Calculate when this frame should be displayed based on the
    // current video timeline position
    double video_time_sec = timeline_.getPts(true) * av_q2d(encoder_.video_time_base());
    auto target = stream_start_ + std::chrono::duration<double>(video_time_sec);
    
    // Sync playback speed with real time
    std::this_thread::sleep_until(target);

    AVFrame* frame = source_.next();

    // End of current media file
    // Move to the next playlist item if continuous playback is enabled
    if (!frame) {
        if (once_mode_) {
            stop();
            return false;
        }
        std::string nextPath = playlist_.getNext();
        if (nextPath.empty()) {

            // If nothing queued; replay current file rather than stopping stream
            std::string fallbackPath;
            {
                std::lock_guard<std::mutex> lock(path_mutex_);
                fallbackPath = current_path_;
            }
            // Switch to same file again creating a loop
            performSwitch(fallbackPath);
            frame = source_.next();

            // If it still can't recover, then end stream
            if (!frame) return false;
        } else {
            // Continue through queue
            performSwitch(nextPath);
            frame = source_.next();
            if(!frame) return false;
        }
    }

    // Assign the next video timestamp and advance the timeline
    frame->pts = timeline_.getPts(true);
    timeline_.advance(true, 1);

    // Send the raw frame to the encoder
    if (encoder_.sendVideo(frame) < 0) return false;

    // Receive encoded packets and send them to the streamer
    encoder_.receiveVideo([&](AVPacket* pkt) {
        // Set the correct stream index for the output container
        pkt->stream_index = streamer_.video_stream()->index;

        // Convert encoder timestamps into the streamer's time base
        av_packet_rescale_ts(
            pkt, 
            encoder_.video_time_base(), 
            streamer_.time_base_video()
        );

        streamer_.write(pkt);
    });
    return true;
}

// Encodes and streams one audio frame
bool ContinuumEngine::sendOneAudioFrame(AVFrame* aframe) {
    // Assign the current audio timestamp
    aframe->pts = timeline_.getPts(false);

    // Calculate how much the audio timeline should advance based
    // on the number of samples in the frame
    int64_t duration = av_rescale_q(
        aframe->nb_samples, 
        AVRational{1, cfg_.samplerate}, 
        encoder_.audio_time_base()
    );
    timeline_.advance(false, duration);

    // Send audio samples to the encoder
    encoder_.sendAudio(aframe);

    // The encoder no longer needs this frame
    av_frame_free(&aframe);

    // Receive encoded audio packets and stream them
    encoder_.receiveAudio([&](AVPacket* pkt_) {
        pkt_->stream_index = streamer_.audio_stream()->index;

        // Convert timestamps from encoder time base to output stream time base
        av_packet_rescale_ts(
            pkt_, 
            encoder_.audio_time_base(), 
            streamer_.time_base_audio()
        );
        streamer_.write(pkt_);
    });
    return true;
}

// Main streaming loop
// Continuously decides whether the next packet should be audio or video
// based on the current timeline position
void ContinuumEngine::start() {
    running_ = true;
    int64_t frame_count = 0;
    stream_start_ = std::chrono::steady_clock::now();
    std::cout << "[Engine] streaming - Ctrl+C to stop\n";

    while (running_) {
        // Pause playback while keeping the thread alive
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Handle requested skip
        if (skip_requested_) {
            skip_requested_ = false;
            std::string nextPath = playlist_.getNext();
            if(!nextPath.empty()) {
                performSwitch(nextPath);
            }
        }

        // Prevent this loop from consuming excessive CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // Compare audio and video timestamps
        // Send whichever stream is currently behind
        if (timeline_.compare(encoder_.video_time_base(), encoder_.audio_time_base()) <= 0) {
            if (!sendOneVideoFrame()) break;
        } else {
            AVFrame* aframe = audioSource_.next();
            if (aframe) {
                sendOneAudioFrame(aframe);
            } else {
                // If no audio is available, continue advancing video
                if (!sendOneVideoFrame()) break;
            }
        }
        if (++frame_count % 60 == 0) {
          int64_t video_us = av_rescale_q(timeline_.getPts(true), encoder_.video_time_base(), {1, 1000000});
          int64_t audio_us = av_rescale_q(timeline_.getPts(false), endocer_.audio_time_base(), {1, 1000000});
          int64_t drift_us = video_us - audio_us;

          if (std::abs(drift_us) > 10000) {
            int64_t correction = av_rescale_q(drift_us, {1, 1000000}, encoder_.audio_time_base());
            timeline_.nudgeAudioPts(correction);
          }
        }
    }
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

// Performs a better switch that carries updated media information
void ContinuumEngine::performSwitch(const std::string& nextPath) {
    {
        std::lock_guard<std::mutex> lock(path_mutex_);
        current_path_ = nextPath;
    }
    video_pts_at_switch = timeline_.getPts(true);
    source_.switchFile(nextPath);
    audioSource_.switchFile(nextPath);
}

// Returns current engine state for monitoring/control
EngineStatus ContinuumEngine::getStatus() const {
    EngineStatus s;
    {
        // Protect current_path_ because it can be modified by another thread
        std::lock_guard<std::mutex> lock(path_mutex_);
        s.current_path = current_path_;
    }
    s.paused = paused_;
    s.running = running_;

    // Current audio/video timeline position
    s.video_pts = timeline_.getPts(true);
    s.audio_pts = timeline_.getPts(false);

    // How far playback has progressed since the last media switch
    // Useful for external programs to track switch points
    s.video_pts_since_switch = s.video_pts - video_pts_at_switch.load();

    // Duration of the currently playing media file
    s.current_duration = source_.getDuration();
    return s;
}
