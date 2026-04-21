#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "lockfree_ring_buffer.hpp"
#include "types.hpp"

class SharedMemoryTransport {
public:
    SharedMemoryTransport(const AppConfig& config,
                          SpscRingBuffer<SharedBlockView>& dsp_queue,
                          SpscRingBuffer<SharedBlockView>& persist_queue);
    ~SharedMemoryTransport();

    bool initialize();
    void start();
    void stop();

    struct Stats {
        uint64_t received_descriptors = 0;
        uint64_t dropped_for_dsp_queue_full = 0;
        uint64_t dropped_for_persist_queue_full = 0;
        uint64_t malformed_descriptor = 0;
    };

    Stats stats() const;

private:
    void ingest_loop();
    bool read_descriptor(RpmsgDataDescriptor& desc);
    bool build_block(const RpmsgDataDescriptor& desc, SharedBlockView& out_block) const;
    void set_realtime_priority() const;
    void close_resources();

private:
    AppConfig config_;
    SpscRingBuffer<SharedBlockView>& dsp_queue_;
    SpscRingBuffer<SharedBlockView>& persist_queue_;

    int rpmsg_fd_ = -1;
    int shm_fd_ = -1;
    void* mapped_shm_ptr_ = nullptr;
    const std::byte* shm_data_ptr_ = nullptr;
    size_t mapped_shm_size_ = 0;

    std::atomic<bool> running_ {false};
    std::thread ingest_thread_;

    mutable std::atomic<uint64_t> received_descriptors_ {0};
    mutable std::atomic<uint64_t> dropped_for_dsp_queue_full_ {0};
    mutable std::atomic<uint64_t> dropped_for_persist_queue_full_ {0};
    mutable std::atomic<uint64_t> malformed_descriptor_ {0};
};
