#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "lockfree_ring_buffer.hpp"
#include "types.hpp"

// SharedMemoryTransport 负责：
// 1) 打开 rpmsg_char 设备并等待“数据就绪”中断消息；
// 2) 映射共享内存并构造零拷贝样本视图；
// 3) 把视图分发到 DSP 队列与落盘队列。
class SharedMemoryTransport {
public:
    SharedMemoryTransport(const AppConfig& config,
                          SpscRingBuffer<SharedBlockView>& dsp_queue,
                          SpscRingBuffer<SharedBlockView>& persist_queue);
    ~SharedMemoryTransport();

    // 初始化设备资源（rpmsg fd + shm mmap）。
    bool initialize();

    // 启动线程1：极速搬运线程。
    void start();

    // 停止线程并释放资源。
    void stop();

    // 监控统计，可用于 UI 或日志系统。
    struct Stats {
        uint64_t received_descriptors = 0;
        uint64_t dropped_for_dsp_queue_full = 0;
        uint64_t dropped_for_persist_queue_full = 0;
        uint64_t malformed_descriptor = 0;
    };
    Stats stats() const;

private:
    // 线程主循环：只做搬运，不做计算，不做频繁日志。
    void ingest_loop();

    // 从 rpmsg 设备读取一个描述符。
    bool read_descriptor(RpmsgDataDescriptor& desc);

    // 校验并把描述符映射为共享内存视图。
    bool build_view(const RpmsgDataDescriptor& desc, SharedBlockView& out_view) const;

    // 设置线程实时优先级（失败不致命，但会影响实时性）。
    void set_realtime_priority() const;

private:
    AppConfig config_;
    SpscRingBuffer<SharedBlockView>& dsp_queue_;
    SpscRingBuffer<SharedBlockView>& persist_queue_;

    int rpmsg_fd_ = -1;
    int shm_fd_ = -1;
    void* shm_base_ptr_ = nullptr;

    std::atomic<bool> running_ {false};
    std::thread ingest_thread_;

    mutable std::atomic<uint64_t> received_descriptors_ {0};
    mutable std::atomic<uint64_t> dropped_for_dsp_queue_full_ {0};
    mutable std::atomic<uint64_t> dropped_for_persist_queue_full_ {0};
    mutable std::atomic<uint64_t> malformed_descriptor_ {0};
};

