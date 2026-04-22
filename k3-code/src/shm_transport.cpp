#include "shm_transport.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
constexpr int kPollTimeoutMs = 20;
}

SharedMemoryTransport::SharedMemoryTransport(const AppConfig& config, SpscRingBuffer<AudioFramePtr>& ingress_queue)
    : config_(config), ingress_queue_(ingress_queue) {}

SharedMemoryTransport::~SharedMemoryTransport() { stop(); }

bool SharedMemoryTransport::initialize() {
    close_resources();

    rpmsg_fd_ = ::open(config_.rpmsg_device.c_str(), O_RDONLY | O_NONBLOCK);
    if (rpmsg_fd_ < 0) {
        close_resources();
        return false;
    }

    shm_fd_ = ::open(config_.shm_device.c_str(), O_RDONLY | O_SYNC);
    if (shm_fd_ < 0) {
        close_resources();
        return false;
    }

    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        close_resources();
        return false;
    }

    const uint64_t page_mask = static_cast<uint64_t>(page_size - 1);
    const uint64_t map_offset = config_.shm_base & ~page_mask;
    const size_t delta = static_cast<size_t>(config_.shm_base - map_offset);
    mapped_shm_size_ = config_.shm_size + delta;

    mapped_shm_ptr_ = ::mmap(nullptr,
                             mapped_shm_size_,
                             PROT_READ,
                             MAP_SHARED,
                             shm_fd_,
                             static_cast<off_t>(map_offset));
    if (mapped_shm_ptr_ == MAP_FAILED) {
        mapped_shm_ptr_ = nullptr;
        mapped_shm_size_ = 0;
        close_resources();
        return false;
    }

    shm_data_ptr_ = reinterpret_cast<const std::byte*>(mapped_shm_ptr_) + delta;
    return true;
}

void SharedMemoryTransport::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    ingest_thread_ = std::thread(&SharedMemoryTransport::ingest_loop, this);
}

void SharedMemoryTransport::stop() {
    running_.store(false, std::memory_order_release);

    if (ingest_thread_.joinable()) {
        ingest_thread_.join();
    }

    close_resources();
}

SharedMemoryTransport::Stats SharedMemoryTransport::stats() const {
    Stats s {};
    s.received_descriptors = received_descriptors_.load(std::memory_order_relaxed);
    s.dropped_for_ingress_queue_full = dropped_for_ingress_queue_full_.load(std::memory_order_relaxed);
    s.malformed_descriptor = malformed_descriptor_.load(std::memory_order_relaxed);
    return s;
}

void SharedMemoryTransport::ingest_loop() {
    set_realtime_priority();

    pollfd pfd {};
    pfd.fd = rpmsg_fd_;
    pfd.events = POLLIN;

    while (running_.load(std::memory_order_acquire)) {
        const int ret = ::poll(&pfd, 1, kPollTimeoutMs);
        if (ret <= 0) {
            continue;
        }

        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            running_.store(false, std::memory_order_release);
            break;
        }

        if ((pfd.revents & POLLIN) == 0) {
            continue;
        }

        for (;;) {
            RpmsgDataDescriptor desc {};
            if (!read_descriptor(desc)) {
                break;
            }

            received_descriptors_.fetch_add(1, std::memory_order_relaxed);

            AudioFramePtr frame;
            if (!build_frame(desc, frame)) {
                malformed_descriptor_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            if (!ingress_queue_.push(std::move(frame))) {
                dropped_for_ingress_queue_full_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

bool SharedMemoryTransport::read_descriptor(RpmsgDataDescriptor& desc) {
    const auto n = ::read(rpmsg_fd_, &desc, sizeof(desc));
    if (n < 0) {
        return false;
    }
    if (n == 0) {
        return false;
    }
    return static_cast<size_t>(n) == sizeof(desc);
}

bool SharedMemoryTransport::build_frame(const RpmsgDataDescriptor& desc, AudioFramePtr& out_frame) const {
    if (shm_data_ptr_ == nullptr || desc.sample_count == 0 || desc.channels == 0) {
        return false;
    }

    const size_t channels = static_cast<size_t>(desc.channels);
    const size_t total_samples = static_cast<size_t>(desc.sample_count) * channels;
    if (total_samples == 0) {
        return false;
    }

    if (total_samples > (std::numeric_limits<size_t>::max() / sizeof(int32_t))) {
        return false;
    }

    const size_t byte_count = total_samples * sizeof(int32_t);
    const size_t offset = static_cast<size_t>(desc.shm_offset);
    if (offset > config_.shm_size || byte_count > config_.shm_size || offset + byte_count > config_.shm_size) {
        return false;
    }

    const auto* sample_ptr = reinterpret_cast<const int32_t*>(shm_data_ptr_ + offset);
    auto owned_samples = std::make_shared<std::vector<int32_t>>(total_samples);
    std::memcpy(owned_samples->data(), sample_ptr, byte_count);

    auto frame = std::make_shared<AudioFrame>();
    frame->desc = desc;
    frame->owned_samples = std::move(owned_samples);
    frame->samples = SampleView {frame->owned_samples->data(), frame->owned_samples->size()};
    out_frame = std::move(frame);
    return true;
}

void SharedMemoryTransport::set_realtime_priority() const {
    sched_param param {};
    param.sched_priority = 80;
    ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param);
}

void SharedMemoryTransport::close_resources() {
    shm_data_ptr_ = nullptr;

    if (mapped_shm_ptr_ != nullptr) {
        ::munmap(mapped_shm_ptr_, mapped_shm_size_);
        mapped_shm_ptr_ = nullptr;
        mapped_shm_size_ = 0;
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
