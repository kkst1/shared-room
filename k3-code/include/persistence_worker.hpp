#pragma once

#include <atomic>
#include <fstream>
#include <thread>
#include <vector>

#include "lockfree_ring_buffer.hpp"
#include "types.hpp"

// 线程4：异步落盘线程（低优先级）。
// 从共享内存视图读取全量原始数据，批量刷入 eMMC。
class PersistenceWorker {
public:
    PersistenceWorker(const AppConfig& config,
                      SpscRingBuffer<SharedBlockView>& persist_queue);
    ~PersistenceWorker();

    // 打开文件并启动线程。
    bool start();

    // 停止线程并刷新残留数据。
    void stop();

private:
    // 低优先级主循环。
    void persist_loop();

    // 把 int32 采样块追加到待刷缓存。
    void append_block(const SharedBlockView& block);

    // 把缓存刷盘到 eMMC。
    void flush_cache();

    // 设置低优先级策略，确保不抢占实时线程。
    void set_low_priority() const;

private:
    AppConfig config_;
    SpscRingBuffer<SharedBlockView>& persist_queue_;

    std::ofstream output_;
    std::vector<int32_t> write_cache_;

    std::atomic<bool> running_ {false};
    std::thread persist_thread_;
};

