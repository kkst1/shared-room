#pragma once

#include <atomic>
#include <thread>

#include <QObject>

#include "lockfree_ring_buffer.hpp"
#include "types.hpp"

class UiBridge;

// DspEngineWorker 负责线程2：
// - 从 raw ring buffer 取 512k（或配置值）数据块；
// - NEON 加速前处理 + FFT + 倍频程；
// - 通过 Qt::QueuedConnection 把轻量结果发给 UI 主线程。
class DspEngineWorker {
public:
    DspEngineWorker(const AppConfig& config,
                    SpscRingBuffer<SharedBlockView>& dsp_queue,
                    UiBridge* ui_bridge);
    ~DspEngineWorker();

    // 启动 DSP 线程。
    void start();

    // 停止 DSP 线程。
    void stop();

private:
    // DSP 主循环。
    void dsp_loop();

    // 计算一个数据块并输出 UI 帧。
    DspUiFrame process_block(const SharedBlockView& block) const;

    // 应用 Hann 窗；AArch64 下用 NEON 做轻量向量化。
    void apply_hann_window(std::vector<float>& inout) const;

    // 计算半谱 dB（简化实现，便于你替换为 kissfft/fftw/自研 FFT）。
    std::vector<float> compute_fft_db(const std::vector<float>& samples) const;

    // 根据频谱计算 10 个倍频程能量（简化模型）。
    std::array<float, 10> compute_octave(const std::vector<float>& spectrum_db) const;

    // 为 UI 构建包络，降低重绘点数。
    std::vector<float> build_ui_envelope(const std::vector<float>& samples) const;

    // 设置线程优先级（低于线程1，高于线程4）。
    void set_realtime_priority() const;

private:
    AppConfig config_;
    SpscRingBuffer<SharedBlockView>& dsp_queue_;
    UiBridge* ui_bridge_ = nullptr;

    std::atomic<bool> running_ {false};
    std::thread dsp_thread_;
};

