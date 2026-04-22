#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct RpmsgDataDescriptor {
    uint32_t sequence = 0;
    uint32_t shm_offset = 0;
    uint32_t sample_count = 0;
    uint32_t channels = 1;
    uint64_t timestamp_ns = 0;
    uint32_t flags = 0;
    uint32_t reserved = 0;
};

struct SampleView {
    const int32_t* data = nullptr;
    size_t count = 0;

    const int32_t* begin() const noexcept { return data; }
    const int32_t* end() const noexcept { return data + count; }
    const int32_t& operator[](size_t index) const noexcept { return data[index]; }
    size_t size() const noexcept { return count; }
    bool empty() const noexcept { return count == 0; }
};

struct AudioFrame {
    RpmsgDataDescriptor desc {};
    std::shared_ptr<std::vector<int32_t>> owned_samples;
    SampleView samples {};
};

using AudioFramePtr = std::shared_ptr<const AudioFrame>;

struct DspUiFrame {
    uint32_t sequence = 0;
    uint64_t timestamp_ns = 0;
    std::vector<float> waveform_envelope;
    std::vector<float> spectrum_db;
};

struct AppConfig {
    std::string rpmsg_device = "/dev/rpmsg0";
    std::string shm_device = "/dev/mem";
    uint64_t shm_base = 0xA0000000;
    size_t shm_size = 16 * 1024 * 1024;
    size_t ring_capacity = 128;
    size_t samples_per_block = 4096;
    uint32_t capture_rate = 512000;
    std::string persist_path = "/tmp/k3_capture.raw";
    size_t persist_flush_bytes = 4 * 1024 * 1024;
    bool persist_fsync_on_flush = false;
    size_t ingress_capacity = 128;
    std::string alsa_device = "hw:0,0";
    uint32_t playback_rate = 48000;
    uint32_t playback_channels = 1;
};
