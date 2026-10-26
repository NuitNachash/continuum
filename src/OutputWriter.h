#pragma once 
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
extern "C" {
    #include <libavcodec/avcodec.h>
}
#include "Streamer.h"
#include "ClockSync.h"

class OutputWriter {
public:
    OutputWriter(Streamer& streamer, ClockSync& clock);
    ~OutputWriter();

    void start();
    void stop();
    bool push(AVPacket* pkt, AVRational tb, bool isVideo);

    size_t depth() {
        std::lock_guard<std::mutex> l(writer_mutex);
        return q_.size();
    }

private:
    struct Item{
        AVPacket* pkt;
        AVRational tb;
        bool video;
    };
    void run();

    Streamer& streamer_;
    ClockSync& clock_;

    std::deque<Item> q_;
    std::mutex writer_mutex;
    std::condition_variable not_full_, not_empty_;
    static constexpr size_t MAX_Q = 150;

    std::atomic<bool> running_{false};
    std::thread writer_thread;
    int64_t last_dts_[2] = {
        INT64_MIN,  //[0] = audio
        INT64_MIN   //[1] = video
    };

};