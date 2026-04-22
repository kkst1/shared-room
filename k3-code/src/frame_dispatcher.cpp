#include "frame_dispatcher.hpp"

#include <chrono>
#include <thread>

FrameDispatcher::FrameDispatcher(SpscRingBuffer<AudioFramePtr>& ingress_queue,
                                 SpscRingBuffer<AudioFramePtr>& dsp_queue,
                                 SpscRingBuffer<AudioFramePtr>& persist_queue)
    : ingress_queue_(ingress_queue), dsp_queue_(dsp_queue), persist_queue_(persist_queue) {}

FrameDispatcher::~FrameDispatcher() { stop(); }

void FrameDispatcher::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    dispatch_thread_ = std::thread(&FrameDispatcher::dispatch_loop, this);
}

void FrameDispatcher::stop() {
    running_.store(false, std::memory_order_release);
    if (dispatch_thread_.joinable()) {
        dispatch_thread_.join();
    }
}

FrameDispatcher::Stats FrameDispatcher::stats() const {
    Stats s {};
    s.dispatched_frames = dispatched_frames_.load(std::memory_order_relaxed);
    s.dropped_for_dsp_queue_full = dropped_for_dsp_queue_full_.load(std::memory_order_relaxed);
    s.dropped_for_persist_queue_full = dropped_for_persist_queue_full_.load(std::memory_order_relaxed);
    s.sequence_gap = sequence_gap_.load(std::memory_order_relaxed);
    return s;
}

void FrameDispatcher::dispatch_loop() {
    while (running_.load(std::memory_order_acquire) || ingress_queue_.approx_size() > 0) {
        auto item = ingress_queue_.pop();
        if (!item.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const AudioFramePtr& frame = item.value();
        if (!frame) {
            continue;
        }

        if (have_last_sequence_) {
            const uint32_t expected = last_sequence_ + 1;
            if (frame->desc.sequence != expected) {
                sequence_gap_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        last_sequence_ = frame->desc.sequence;
        have_last_sequence_ = true;

        if (!dsp_queue_.push(frame)) {
            dropped_for_dsp_queue_full_.fetch_add(1, std::memory_order_relaxed);
        }
        if (!persist_queue_.push(frame)) {
            dropped_for_persist_queue_full_.fetch_add(1, std::memory_order_relaxed);
        }
        dispatched_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}
