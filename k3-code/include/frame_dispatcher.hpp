#pragma once

#include <atomic>
#include <thread>

#include "lockfree_ring_buffer.hpp"
#include "types.hpp"

class FrameDispatcher {
public:
    FrameDispatcher(SpscRingBuffer<AudioFramePtr>& ingress_queue,
                    SpscRingBuffer<AudioFramePtr>& dsp_queue,
                    SpscRingBuffer<AudioFramePtr>& persist_queue);
    ~FrameDispatcher();

    void start();
    void stop();

    struct Stats {
        uint64_t dispatched_frames = 0;
        uint64_t dropped_for_dsp_queue_full = 0;
        uint64_t dropped_for_persist_queue_full = 0;
        uint64_t sequence_gap = 0;
    };

    Stats stats() const;

private:
    void dispatch_loop();

private:
    SpscRingBuffer<AudioFramePtr>& ingress_queue_;
    SpscRingBuffer<AudioFramePtr>& dsp_queue_;
    SpscRingBuffer<AudioFramePtr>& persist_queue_;

    std::atomic<bool> running_ {false};
    std::thread dispatch_thread_;

    uint32_t last_sequence_ = 0;
    bool have_last_sequence_ = false;

    std::atomic<uint64_t> dispatched_frames_ {0};
    std::atomic<uint64_t> dropped_for_dsp_queue_full_ {0};
    std::atomic<uint64_t> dropped_for_persist_queue_full_ {0};
    std::atomic<uint64_t> sequence_gap_ {0};
};
