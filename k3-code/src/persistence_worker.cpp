#include "persistence_worker.hpp"

#include <chrono>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <thread>

PersistenceWorker::PersistenceWorker(const AppConfig& config,
                                     SpscRingBuffer<SharedBlockView>& persist_queue)
    : config_(config), persist_queue_(persist_queue) {}

PersistenceWorker::~PersistenceWorker() { stop(); }

bool PersistenceWorker::start() {
    output_.open(config_.persist_path, std::ios::binary | std::ios::trunc);
    if (!output_.is_open()) {
        return false;
    }

    write_cache_.clear();
    write_cache_.reserve(config_.persist_flush_bytes / sizeof(int32_t) + config_.samples_per_block);

    if (running_.exchange(true)) {
        return true;
    }

    persist_thread_ = std::thread(&PersistenceWorker::persist_loop, this);
    return true;
}

void PersistenceWorker::stop() {
    if (!running_.exchange(false)) {
        if (output_.is_open()) {
            output_.close();
        }
        return;
    }

    if (persist_thread_.joinable()) {
        persist_thread_.join();
    }

    // 停止前强制刷盘，避免尾部数据丢失。
    flush_cache();
    if (output_.is_open()) {
        output_.close();
    }
}

void PersistenceWorker::persist_loop() {
    // 线程4明确设为低优先级，不参与实时路径抢占。
    set_low_priority();

    while (running_.load(std::memory_order_relaxed)) {
        auto item = persist_queue_.pop();
        if (!item.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        append_block(item.value());
        const size_t cached_bytes = write_cache_.size() * sizeof(int32_t);
        if (cached_bytes >= config_.persist_flush_bytes) {
            flush_cache();
        }
    }
}

void PersistenceWorker::append_block(const SharedBlockView& block) {
    // 在低优先级线程内把共享视图拷贝到持久化缓存，避免线程1做重活。
    write_cache_.insert(write_cache_.end(), block.samples.begin(), block.samples.end());
}

void PersistenceWorker::flush_cache() {
    if (!output_.is_open() || write_cache_.empty()) {
        return;
    }

    const auto* bytes = reinterpret_cast<const char*>(write_cache_.data());
    const std::streamsize count = static_cast<std::streamsize>(write_cache_.size() * sizeof(int32_t));
    output_.write(bytes, count);
    output_.flush();
    write_cache_.clear();
}

void PersistenceWorker::set_low_priority() const {
    // 优先尝试 Linux 的 SCHED_IDLE，确保“能写就写、不能写不影响主链路”。
    sched_param param {};
    param.sched_priority = 0;
    ::pthread_setschedparam(::pthread_self(), SCHED_IDLE, &param);

    // 进一步调低 nice 值，减少对 CPU 的竞争。
    ::setpriority(PRIO_PROCESS, 0, 19);
}
