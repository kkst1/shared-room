#include "dsp_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <pthread.h>
#include <thread>

#include <QMetaObject>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "ui_bridge.hpp"

namespace {
constexpr float kPi = 3.14159265358979323846f;

// 返回 <= n 的最大 2 的幂，用于 radix-2 FFT。
size_t floor_pow2(size_t n) {
    size_t p = 1;
    while ((p << 1) <= n) {
        p <<= 1;
    }
    return p;
}
} // namespace

DspEngineWorker::DspEngineWorker(const AppConfig& config,
                                 SpscRingBuffer<SharedBlockView>& dsp_queue,
                                 UiBridge* ui_bridge)
    : config_(config), dsp_queue_(dsp_queue), ui_bridge_(ui_bridge) {}

DspEngineWorker::~DspEngineWorker() { stop(); }

void DspEngineWorker::start() {
    if (running_.exchange(true)) {
        return;
    }
    dsp_thread_ = std::thread(&DspEngineWorker::dsp_loop, this);
}

void DspEngineWorker::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (dsp_thread_.joinable()) {
        dsp_thread_.join();
    }
}

void DspEngineWorker::dsp_loop() {
    set_realtime_priority();

    while (running_.load(std::memory_order_relaxed)) {
        auto item = dsp_queue_.pop();
        if (!item.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // 对每个采样块执行 DSP 计算并生成 UI 轻量帧。
        DspUiFrame frame = process_block(item.value());

        // 用 QueuedConnection 投递到 UI 主线程，避免跨线程直接操作图表。
        QMetaObject::invokeMethod(
            ui_bridge_,
            "enqueue_frame",
            Qt::QueuedConnection,
            Q_ARG(DspUiFrame, frame));
    }
}

DspUiFrame DspEngineWorker::process_block(const SharedBlockView& block) const {
    DspUiFrame frame {};
    frame.sequence = block.desc.sequence;
    frame.timestamp_ns = block.desc.timestamp_ns;

    // int32 原始采样归一化到 float。
    std::vector<float> normalized(block.samples.size());
    constexpr float kNorm = 1.0f / 2147483648.0f;
    for (size_t i = 0; i < block.samples.size(); ++i) {
        normalized[i] = static_cast<float>(block.samples[i]) * kNorm;
    }

    // 频域前先乘窗，降低泄漏。
    apply_hann_window(normalized);

    frame.spectrum_db = compute_fft_db(normalized);
    frame.octave_db = compute_octave(frame.spectrum_db);
    frame.waveform_envelope = build_ui_envelope(normalized);
    return frame;
}

void DspEngineWorker::apply_hann_window(std::vector<float>& inout) const {
    const size_t n = inout.size();
    if (n < 2) {
        return;
    }

#if defined(__aarch64__)
    // NEON 向量化：每次处理 4 个样点，窗口系数仍按标量公式计算。
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        float w0 = 0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(i + 0)) / static_cast<float>(n - 1));
        float w1 = 0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(i + 1)) / static_cast<float>(n - 1));
        float w2 = 0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(i + 2)) / static_cast<float>(n - 1));
        float w3 = 0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(i + 3)) / static_cast<float>(n - 1));

        float32x4_t x = vld1q_f32(&inout[i]);
        float32x4_t w = {w0, w1, w2, w3};
        float32x4_t y = vmulq_f32(x, w);
        vst1q_f32(&inout[i], y);
    }
    for (; i < n; ++i) {
        float w = 0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(i)) / static_cast<float>(n - 1));
        inout[i] *= w;
    }
#else
    // 非 AArch64 平台走标量版本，便于本地联调。
    for (size_t i = 0; i < n; ++i) {
        float w = 0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(i)) / static_cast<float>(n - 1));
        inout[i] *= w;
    }
#endif
}

std::vector<float> DspEngineWorker::compute_fft_db(const std::vector<float>& samples) const {
    // 为了保持示例可读，这里实现简化 radix-2 FFT。
    // 生产中建议替换为你已验证的高性能 FFT 库/自研实现。
    const size_t n = floor_pow2(samples.size());
    if (n < 2) {
        return {};
    }

    std::vector<std::complex<float>> data(n);
    for (size_t i = 0; i < n; ++i) {
        data[i] = std::complex<float>(samples[i], 0.0f);
    }

    // bit-reversal 排序。
    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // Cooley-Tukey 迭代蝶形。
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * kPi / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = data[i + k];
                const auto v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    // 只保留半谱并转 dB。
    const size_t half = n / 2;
    std::vector<float> out(half);
    for (size_t i = 0; i < half; ++i) {
        float mag = std::abs(data[i]) / static_cast<float>(n);
        mag = std::max(mag, 1e-12f);
        out[i] = 20.0f * std::log10(mag);
    }
    return out;
}

std::array<float, 10> DspEngineWorker::compute_octave(const std::vector<float>& spectrum_db) const {
    // 简化倍频程：按频谱索引均分为 10 段求均值。
    // 实战可替换成基于中心频率的标准 1/1 或 1/3 倍频程实现。
    std::array<float, 10> out {};
    if (spectrum_db.empty()) {
        return out;
    }
    const size_t band_size = std::max<size_t>(1, spectrum_db.size() / out.size());
    for (size_t b = 0; b < out.size(); ++b) {
        const size_t begin = b * band_size;
        if (begin >= spectrum_db.size()) {
            out[b] = out[b == 0 ? 0 : b - 1];
            continue;
        }
        const size_t end = std::min(spectrum_db.size(), begin + band_size);
        float sum = 0.0f;
        for (size_t i = begin; i < end; ++i) {
            sum += spectrum_db[i];
        }
        out[b] = sum / static_cast<float>(end - begin);
    }
    return out;
}

std::vector<float> DspEngineWorker::build_ui_envelope(const std::vector<float>& samples) const {
    // UI 只需要稳定视觉，不需要全量点，做 max-abs 降采样包络。
    constexpr size_t kUiPoints = 1024;
    if (samples.empty()) {
        return {};
    }

    const size_t step = std::max<size_t>(1, samples.size() / kUiPoints);
    std::vector<float> envelope;
    envelope.reserve(kUiPoints);
    for (size_t i = 0; i < samples.size(); i += step) {
        const size_t end = std::min(samples.size(), i + step);
        float max_abs = 0.0f;
        for (size_t j = i; j < end; ++j) {
            max_abs = std::max(max_abs, std::fabs(samples[j]));
        }
        envelope.push_back(max_abs);
    }
    return envelope;
}

void DspEngineWorker::set_realtime_priority() const {
    // DSP 线程优先级低于线程1，避免抢占搬运线程。
    sched_param param {};
    param.sched_priority = 60;
    ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param);
}
