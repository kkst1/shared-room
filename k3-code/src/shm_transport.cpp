#include "shm_transport.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

SharedMemoryTransport::SharedMemoryTransport(const AppConfig& config,
                                             SpscRingBuffer<SharedBlockView>& dsp_queue,
                                             SpscRingBuffer<SharedBlockView>& persist_queue)
    : config_(config), dsp_queue_(dsp_queue), persist_queue_(persist_queue) {}

SharedMemoryTransport::~SharedMemoryTransport() { stop(); }

bool SharedMemoryTransport::initialize() {
    // 打开 rpmsg_char 设备，用于接收 R5F 发来的“数据描述符就绪”通知。
    rpmsg_fd_ = ::open(config_.rpmsg_device.c_str(), O_RDONLY | O_NONBLOCK);
    if (rpmsg_fd_ < 0) {
        return false;
    }

    // 打开共享内存设备，常见是 /dev/mem + reserved-memory 物理区映射。
    shm_fd_ = ::open(config_.shm_device.c_str(), O_RDWR | O_SYNC);
    if (shm_fd_ < 0) {
        return false;
    }

    // 把共享区映射到用户态，供线程1构造零拷贝样本视图。
    shm_base_ptr_ = ::mmap(nullptr,
                           config_.shm_size,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED,
                           shm_fd_,
                           static_cast<off_t>(config_.shm_base));
    if (shm_base_ptr_ == MAP_FAILED) {
        shm_base_ptr_ = nullptr;
        return false;
    }

    return true;
}

void SharedMemoryTransport::start() {
    if (running_.exchange(true)) {
        return;
    }
    ingest_thread_ = std::thread(&SharedMemoryTransport::ingest_loop, this);
}

void SharedMemoryTransport::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (ingest_thread_.joinable()) {
        ingest_thread_.join();
    }

    // 释放映射资源。
    if (shm_base_ptr_) {
        ::munmap(shm_base_ptr_, config_.shm_size);
        shm_base_ptr_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
    if (rpmsg_fd_ >= 0) {
        ::close(rpmsg_fd_);
        rpmsg_fd_ = -1;
    }
}

SharedMemoryTransport::Stats SharedMemoryTransport::stats() const {
    Stats s {};
    s.received_descriptors = received_descriptors_.load(std::memory_order_relaxed);
    s.dropped_for_dsp_queue_full = dropped_for_dsp_queue_full_.load(std::memory_order_relaxed);
    s.dropped_for_persist_queue_full = dropped_for_persist_queue_full_.load(std::memory_order_relaxed);
    s.malformed_descriptor = malformed_descriptor_.load(std::memory_order_relaxed);
    return s;
}

void SharedMemoryTransport::ingest_loop() {
    // 线程1必须最高实时优先级，确保只做“极速搬运”。
    set_realtime_priority();

    pollfd pfd {};
    pfd.fd = rpmsg_fd_;
    pfd.events = POLLIN;

    while (running_.load(std::memory_order_relaxed)) {
        // 使用 poll 等待 rpmsg 中断信号，避免空转占用 CPU。
        const int ret = ::poll(&pfd, 1, 20);
        if (ret <= 0) {
            continue;
        }
        if ((pfd.revents & POLLIN) == 0) {
            continue;
        }

        RpmsgDataDescriptor desc {};
        if (!read_descriptor(desc)) {
            continue;
        }
        received_descriptors_.fetch_add(1, std::memory_order_relaxed);

        SharedBlockView view {};
        if (!build_view(desc, view)) {
            malformed_descriptor_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // 只做转发，不做计算与日志，保持线程1最短路径。
        if (!dsp_queue_.push(view)) {
            dropped_for_dsp_queue_full_.fetch_add(1, std::memory_order_relaxed);
        }
        if (!persist_queue_.push(view)) {
            dropped_for_persist_queue_full_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool SharedMemoryTransport::read_descriptor(RpmsgDataDescriptor& desc) {
    // rpmsg_char 这里按“固定大小描述符”读取；
    // 若你的协议是带头长度字段，请替换为协议解析逻辑。
    const auto n = ::read(rpmsg_fd_, &desc, sizeof(desc));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }
        return false;
    }
    if (static_cast<size_t>(n) != sizeof(desc)) {
        return false;
    }
    return true;
}

bool SharedMemoryTransport::build_view(const RpmsgDataDescriptor& desc, SharedBlockView& out_view) const {
    if (!shm_base_ptr_) {
        return false;
    }
    if (desc.sample_count == 0 || desc.channels == 0) {
        return false;
    }

    const size_t total_samples = static_cast<size_t>(desc.sample_count) * desc.channels;
    const size_t bytes = total_samples * sizeof(int32_t);
    const size_t offset = desc.shm_offset;

    // 严格边界校验，避免越界访问共享内存。
    if (offset > config_.shm_size || bytes > config_.shm_size || (offset + bytes) > config_.shm_size) {
        return false;
    }

    auto* base = reinterpret_cast<const std::byte*>(shm_base_ptr_);
    auto* sample_ptr = reinterpret_cast<const int32_t*>(base + offset);
    out_view.desc = desc;
    out_view.samples = std::span<const int32_t>(sample_ptr, total_samples);
    return true;
}

void SharedMemoryTransport::set_realtime_priority() const {
    // FIFO 80：让线程1优先处理数据搬运，降低中断到入队延迟。
    sched_param param {};
    param.sched_priority = 80;
    ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param);
}
