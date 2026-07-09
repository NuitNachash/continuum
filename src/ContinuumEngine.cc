#include "ContinuumEngine.h"
#include <thread>
#include <iostream>
#include <mutex>

ContinuumEngine::ContinuumEngine(const config& cfg)
    : cfg_(cfg),
      encoder_(cfg_),
      streamer_(cfg_, encoder_),
      source_(cfg_),
      audioSource_(cfg_, encoder_.audio_time_base()),
      current_path_(cfg.mp4Path)
{}

void ContinuumEngine::loadPlaylist(const std::vector<std::string>& paths) {
    for (const auto& p : paths)
        playlist_.add(p);
}

void ContinuumEngine::addMedia(const std::string& path) {
    playlist_.add(path);
}

bool ContinuumEngine::sendOneVideoFrame() {
    double video_time_sec = timeline_.getPts(true) * av_q2d(encoder_.video_time_base());
    auto target = stream_start_ + std::chrono::duration<double>(video_time_sec);
    std::this_thread::sleep_until(target);

    AVFrame* frame = source_.next();
    if (!frame) {
        if (once_mode_) {
            stop();
            return false;
        }
        std::string nextPath = playlist_.getNext();
        if (nextPath.empty()) return false;
        {
            std::lock_guard<std::mutex> lock(path_mutex_);
            current_path_ = nextPath;
        }
        
        video_pts_at_switch = timeline_.getPts(true);

        source_.switchFile(nextPath);
        audioSource_.switchFile(nextPath);
        frame = source_.next();
        if (!frame) return false;
    }

    frame->pts = timeline_.getPts(true);
    timeline_.advance(true, 1);
    if (encoder_.sendVideo(frame) < 0) return false;

    encoder_.receiveVideo([&](AVPacket* pkt) {
        pkt->stream_index = streamer_.video_stream()->index;
        av_packet_rescale_ts(pkt, encoder_.video_time_base(), streamer_.time_base_video());
        streamer_.write(pkt);
    });
    return true;
}

bool ContinuumEngine::sendOneAudioFrame(AVFrame* aframe) {
    aframe->pts = timeline_.getPts(false);
    int64_t duration = av_rescale_q(aframe->nb_samples, AVRational{1, cfg_.samplerate}, encoder_.audio_time_base());
    timeline_.advance(false, duration);

    encoder_.sendAudio(aframe);
    av_frame_free(&aframe);
    encoder_.receiveAudio([&](AVPacket* pkt_) {
        pkt_->stream_index = streamer_.audio_stream()->index;
        av_packet_rescale_ts(pkt_, encoder_.audio_time_base(), streamer_.time_base_audio());
        streamer_.write(pkt_);
    });
    return true;
}

void ContinuumEngine::start() {
    running_ = true;
    stream_start_ = std::chrono::steady_clock::now();
    std::cout << "[Engine] streaming - Ctrl+C to stop\n";

    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (skip_requested_) {
            video_pts_at_switch = timeline_.getPts(true);
            skip_requested_ = false;
            std::string nextPath = playlist_.getNext();
            if(!nextPath.empty()) {
                source_.switchFile(nextPath);
                audioSource_.switchFile(nextPath);
            }
            current_path_ = nextPath;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (timeline_.compare(encoder_.video_time_base(), encoder_.audio_time_base()) <= 0) {
            if (!sendOneVideoFrame()) break;
        } else {
            AVFrame* aframe = audioSource_.next();
            if (aframe) {
                sendOneAudioFrame(aframe);
            } else {
                if (!sendOneVideoFrame()) break;
            }
        }
    }
}

void ContinuumEngine::stop() {
    running_ = false;
}

void ContinuumEngine::pause() {
    paused_ = true;
}

void ContinuumEngine::resume() {
    paused_ = false;
}

void ContinuumEngine::skip() {
    skip_requested_ = true;
}

EngineStatus ContinuumEngine::getStatus() const {
    EngineStatus s;
    {
        std::lock_guard<std::mutex> lock(path_mutex_);
        s.current_path = current_path_;
    }
    s.paused = paused_;
    s.running = running_;
    s.video_pts = timeline_.getPts(true);
    s.audio_pts = timeline_.getPts(false);
    s.video_pts_since_switch = s.video_pts - video_pts_at_switch.load();
    s.current_duration = source_.getDuration();
    return s;
}