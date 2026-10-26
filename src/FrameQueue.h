#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
extern "C"{
    #include <libavutil/frame.h>
}

class FrameQueue {
public:
    explicit FrameQueue(size_t max) : max_(max) {}
    ~FrameQueue() {
        abort();
        flush();
    }

    int serial() {
        std::lock_guard<std::mutex> lk(m_);
        return serial_;
    }

    bool push(AVFrame* f, int serial){
        std::unique_lock<std::mutex> lk(m_);
        not_full_.wait(lk, [&] {
            return q_.size() < max_ || aborted_ || serial != serial_; 
        });
        if (aborted_ || serial != serial_){
            av_frame_free(&f);
            return false;
        }
        q_.push_back(f);
        return true;
    }

    AVFrame* tryPop(){
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()){ return nullptr; }
        AVFrame* f = q_.front();
        q_.pop_front();
        not_full_.notify_one();
        return f;
    }

    size_t size(){
        std::lock_guard<std::mutex> lk(m_);
        return q_.size();
    }

    int flush() {
        std::lock_guard<std::mutex> lk(m_);
        for (auto* f : q_) { av_frame_free(&f); }
        q_.clear();
        int s = ++serial_;
        not_full_.notify_all();
        return s;
    }

    void abort() {
        std::lock_guard<std::mutex> lk(m_);
        aborted_ = true;
        not_full_.notify_all();
    }

    void resume() {
        std::lock_guard<std::mutex> lk(m_);
        aborted_ = false;
    }

private:
    std::deque<AVFrame*> q_;
    std::mutex m_;
    std::condition_variable not_full_;
    int serial_ = 0;
    size_t max_;
    bool aborted_ = false;
};