#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// 与 R5F 约定的 rpmsg 描述符（控制面消息里的“数据描述符”）。
// 实际项目中请保证两端结构体对齐和大小一致。
struct RpmsgDataDescriptor {
    uint32_t sequence = 0;          // 数据块序号，便于检测丢块/乱序。
    uint32_t shm_offset = 0;        // 数据块在共享内存中的偏移。
    uint32_t sample_count = 0;      // 该块样本数（单通道样本数）。
    uint32_t channels = 1;          // 通道数。
    uint64_t timestamp_ns = 0;      // R5F 采样完成时间戳（单调时钟）。
    uint32_t flags = 0;             // 标志位（例如首包/尾包/异常）。
    uint32_t reserved = 0;
};

// 线程间传递的“零拷贝视图”。
// 注意：该结构只持有共享内存视图，不拥有底层内存。
struct SharedBlockView {
    RpmsgDataDescriptor desc {};
    std::span<const int32_t> samples; // 假设 ADC 输出 32bit 对齐样本（可按实际改）。
};

// DSP 输出给 UI 的轻量帧。
// 为减少主线程压力，这里只放 UI 需要的数据（降采样波形 + 频域结果）。
struct DspUiFrame {
    uint32_t sequence = 0;
    uint64_t timestamp_ns = 0;
    std::vector<float> waveform_envelope; // 时间域包络（已降采样）。
    std::vector<float> spectrum_db;       // 频谱 dB（例如半谱）。
    std::array<float, 10> octave_db {};   // 倍频程结果。
};

// 应用运行参数，集中管理，便于从命令行或配置文件注入。
struct AppConfig {
    std::string rpmsg_device = "/dev/rpmsg0";
    std::string shm_device = "/dev/mem";
    uint64_t shm_base = 0xA0000000;       // 示例地址，需改为 DTS reserved-memory 基址。
    size_t shm_size = 16 * 1024 * 1024;   // 示例大小。

    size_t ring_capacity = 128;           // SPSC 队列深度。
    size_t samples_per_block = 4096;      // 每个数据块样本数（示例值，可改 512*1024）。
    uint32_t capture_rate = 512000;       // 原始采样率 512ksps。

    std::string persist_path = "/tmp/k3_capture.raw";
    size_t persist_flush_bytes = 4 * 1024 * 1024; // 满多少字节刷一次盘。

    std::string alsa_device = "hw:0,0";
    uint32_t playback_rate = 48000;       // 旁路回放目标采样率。
    uint32_t playback_channels = 1;
};

