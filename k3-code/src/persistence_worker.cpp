#include "persistence_worker.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

PersistenceWorker::PersistenceWorker(const AppConfig& config,
                                     SpscRingBuffer<SharedBlockView>& persist_queue)
    : config_(config), persist_queue_(persist_queue) {}

PersistenceWorker::~PersistenceWorker() { stop(); }

bool PersistenceWorker::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    output_fd_ = ::open(config_.persist_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
    if (output_fd_ < 0) {
        running_.store(false, std::memory_order_release);
        return false;
    }

    write_cache_.clear();
    const size_t reserve_samples =
        (std::max<size_t>(config_.persist_flush_bytes, sizeof(int32_t)) / sizeof(int32_t)) +
        std::max<size_t>(config_.samples_per_block, 1);
    write_cache_.reserve(reserve_samples);

    persist_thread_ = std::thread(&PersistenceWorker::persist_loop, this);
    return true;
}

void PersistenceWorker::stop() {
    running_.store(false, std::memory_order_release);

    if (persist_thread_.joinable()) {
        persist_thread_.join();
    }

    drain_pending_blocks();
    flush_cache();

    if (output_fd_ >= 0) {
        ::close(output_fd_);
        output_fd_ = -1;
    }
}

void PersistenceWorker::persist_loop() {
    set_low_priority();

    while (running_.load(std::memory_order_acquire) || persist_queue_.approx_size() > 0) {
        auto item = persist_queue_.pop();
        if (!item.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        append_block(item.value());
        if (write_cache_.size() * sizeof(int32_t) >= config_.persist_flush_bytes) {
            if (!flush_cache()) {
                running_.store(false, std::memory_order_release);
                break;
            }
        }
    }
}

void PersistenceWorker::append_block(const SharedBlockView& block) {
    write_cache_.insert(write_cache_.end(), block.samples.begin(), block.samples.end());
}

bool PersistenceWorker::flush_cache() {
    if (output_fd_ < 0 || write_cache_.empty()) {
        return true;
    }

    const auto* bytes = reinterpret_cast<const std::byte*>(write_cache_.data());
    size_t remaining = write_cache_.size() * sizeof(int32_t);
    while (remaining > 0) {
        const ssize_t written = ::write(output_fd_, bytes + (write_cache_.size() * sizeof(int32_t) - remaining), remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        remaining -= static_cast<size_t>(written);
    }

    if (config_.persist_fsync_on_flush && ::fdatasync(output_fd_) != 0) {
        return false;
    }

    write_cache_.clear();
    return true;
}

void PersistenceWorker::set_low_priority() const {
    sched_param param {};
    param.sched_priority = 0;
    ::pthread_setschedparam(::pthread_self(), SCHED_IDLE, &param);
    ::setpriority(PRIO_PROCESS, 0, 19);
}

bool PersistenceWorker::drain_pending_blocks() {
    bool ok = true;
    while (true) {
        auto item = persist_queue_.pop();
        if (!item.has_value()) {
            break;
        }
        append_block(item.value());
        if (write_cache_.size() * sizeof(int32_t) >= config_.persist_flush_bytes) {
            ok = flush_cache() && ok;
        }
    }
    return ok;
}
