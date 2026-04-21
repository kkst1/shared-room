#include "dsp_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include <pthread.h>

#include <QMetaObject>

#include "ui_bridge.hpp"

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kInt32Norm = 1.0f / 2147483648.0f;

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
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    dsp_thread_ = std::thread(&DspEngineWorker::dsp_loop, this);
}

void DspEngineWorker::stop() {
    running_.store(false, std::memory_order_release);
    if (dsp_thread_.joinable()) {
        dsp_thread_.join();
    }
}

void DspEngineWorker::dsp_loop() {
    set_realtime_priority();

    while (running_.load(std::memory_order_acquire)) {
        auto item = dsp_queue_.pop();
        if (!item.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        DspUiFrame frame = process_block(item.value());
        if (ui_bridge_ == nullptr) {
            continue;
        }

        QMetaObject::invokeMethod(
            ui_bridge_,
            "enqueue_frame",
            Qt::QueuedConnection,
            Q_ARG(DspUiFrame, frame));
    }
}

DspUiFrame DspEngineWorker::process_block(const SharedBlockView& block) {
    DspUiFrame frame {};
    frame.sequence = block.desc.sequence;
    frame.timestamp_ns = block.desc.timestamp_ns;

    const size_t channel_count = std::max<size_t>(1, static_cast<size_t>(block.desc.channels));
    const size_t samples_per_channel = block.samples.size() / channel_count;
    if (samples_per_channel < 2) {
        return frame;
    }

    ensure_workspace(samples_per_channel);

    for (size_t i = 0; i < samples_per_channel; ++i) {
        mono_buffer_[i] = static_cast<float>(block.samples[i * channel_count]) * kInt32Norm;
    }

    frame.waveform_envelope = build_ui_envelope(mono_buffer_);

    const size_t fft_size = floor_pow2(samples_per_channel);
    frame.spectrum_db = compute_fft_db(mono_buffer_, fft_size);
    return frame;
}

void DspEngineWorker::ensure_workspace(size_t sample_count) {
    if (mono_buffer_.size() != sample_count) {
        mono_buffer_.assign(sample_count, 0.0f);
    }

    const size_t fft_size = floor_pow2(sample_count);
    if (fft_buffer_.size() != fft_size) {
        fft_buffer_.assign(fft_size, std::complex<float>(0.0f, 0.0f));
    }

    if (window_cache_size_ != fft_size) {
        window_cache_.resize(fft_size);
        if (fft_size > 1) {
            for (size_t i = 0; i < fft_size; ++i) {
                window_cache_[i] =
                    0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(i)) / static_cast<float>(fft_size - 1));
            }
        } else if (fft_size == 1) {
            window_cache_[0] = 1.0f;
        }
        window_cache_size_ = fft_size;
    }
}

std::vector<float> DspEngineWorker::compute_fft_db(const std::vector<float>& samples, size_t fft_size) {
    if (fft_size < 2 || samples.size() < fft_size) {
        return {};
    }

    for (size_t i = 0; i < fft_size; ++i) {
        fft_buffer_[i] = std::complex<float>(samples[i] * window_cache_[i], 0.0f);
    }

    size_t j = 0;
    for (size_t i = 1; i < fft_size; ++i) {
        size_t bit = fft_size >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(fft_buffer_[i], fft_buffer_[j]);
        }
    }

    for (size_t len = 2; len <= fft_size; len <<= 1) {
        const float ang = -2.0f * kPi / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < fft_size; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = fft_buffer_[i + k];
                const auto v = fft_buffer_[i + k + len / 2] * w;
                fft_buffer_[i + k] = u + v;
                fft_buffer_[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    const size_t half = fft_size / 2;
    std::vector<float> out(half);
    for (size_t i = 0; i < half; ++i) {
        float mag = std::abs(fft_buffer_[i]) / static_cast<float>(fft_size);
        mag = std::max(mag, 1e-12f);
        out[i] = 20.0f * std::log10(mag);
    }
    return out;
}

std::vector<float> DspEngineWorker::build_ui_envelope(const std::vector<float>& samples) const {
    constexpr size_t kUiPoints = 1024;
    if (samples.empty()) {
        return {};
    }

    const size_t step = std::max<size_t>(1, samples.size() / kUiPoints);
    std::vector<float> envelope;
    envelope.reserve((samples.size() + step - 1) / step);
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
    sched_param param {};
    param.sched_priority = 60;
    ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param);
}
