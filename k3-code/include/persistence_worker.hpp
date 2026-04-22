#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "lockfree_ring_buffer.hpp"
#include "types.hpp"

class PersistenceWorker {
public:
    PersistenceWorker(const AppConfig& config,
                      SpscRingBuffer<AudioFramePtr>& persist_queue);
    ~PersistenceWorker();

    bool start();
    void stop();

private:
    void persist_loop();
    void append_block(const AudioFrame& block);
    bool flush_cache();
    void set_low_priority() const;
    bool drain_pending_blocks();

private:
    AppConfig config_;
    SpscRingBuffer<AudioFramePtr>& persist_queue_;

    int output_fd_ = -1;
    std::vector<int32_t> write_cache_;

    std::atomic<bool> running_ {false};
    std::thread persist_thread_;
};
