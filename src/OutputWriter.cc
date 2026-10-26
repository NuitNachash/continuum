#include "OutputWriter.h"
#include "logger.h"
#include <chrono>
extern "C" {
    #include <libavutil/mathematics.h>
}

OutputWriter::OutputWriter(Streamer& streamer, ClockSync& clock) : 
    streamer_(streamer), clock_(clock) {}

OutputWriter::~OutputWriter() {
    stop();
}

void OutputWriter::start() {
    if (running_.exchange(true)){
        return;
    }
    writer_thread = std::thread(&OutputWriter::run, this);
}

void OutputWriter::stop() {
    if (!running_.exchange(false)){
        return;
    }
    not_empty_.notify_all();
    not_full_.notify_all();

    if (writer_thread.joinable()){
        writer_thread.join();
    }

    std::lock_guard<std::mutex> lock(writer_mutex);
    for (auto& it : q_) {
        av_packet_free(&it.pkt);
    }
    q_.clear();
}

bool OutputWriter::push(AVPacket* pkt, AVRational tb, bool isVideo) {
    AVPacket* copy = av_packet_clone(pkt);
    if (!copy){
        return false;
    }

    std::unique_lock<std::mutex> lock(writer_mutex);
    not_full_.wait(lock, [&] {
        return q_.size() < MAX_Q || !running_;
    });
    
    if(!running_){
        av_packet_free(&copy);
        return false;
    }

    q_.push_back({
        copy,
        tb,
        isVideo
    });
    lock.unlock();
    not_empty_.notify_one();
    return true;
}

void OutputWriter::run(){
    while(true){
        Item item;
        {
            std::unique_lock<std::mutex> lock(writer_mutex);
            not_empty_.wait(lock, [&] {
                return !q_.empty() || !running_;
            });

            if(q_.empty()){
                break;
            }
            item = q_.front();
            q_.pop_front();
        }
        not_full_.notify_one();

        AVPacket* pkt = item.pkt;
        const int idx = item.video ? 1 : 0;

        int64_t dts = (pkt->dts != AV_NOPTS_VALUE) ? pkt->dts : pkt->pts;
        if (dts <= last_dts_[idx] && last_dts_[idx] != INT64_MIN) {
            LOG_ERROR(std::string("[Writer] non-monotonic dts on ") 
                + (item.video ? "video" : "audio") + ": " 
                + std::to_string(dts) + " <= " + std::to_string(last_dts_[idx]));
                av_packet_free(&pkt);
                continue;
        }
        last_dts_[idx] = dts;

        int64_t wait_us = clock_.untilReady(dts, item.tb);
        if (wait_us > 0) {
            while (wait_us > 0 && running_){
                int64_t slice = std::min<int64_t>(wait_us, 20000);
                std::this_thread::sleep_for(std::chrono::microseconds(slice));
                wait_us = clock_.untilReady(dts, item.tb);
            }
        }

        AVStream* st = item.video ? streamer_.video_stream() : streamer_.audio_stream();
        pkt->stream_index = st->index;
        av_packet_rescale_ts(pkt, item.tb, st->time_base);

        streamer_.write(pkt);
        av_packet_free(&pkt);
    }
}