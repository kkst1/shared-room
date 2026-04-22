#pragma once

#include <atomic>
#include <complex>
#include <thread>
#include <vector>

#include <QObject>

#include "lockfree_ring_buffer.hpp"
#include "types.hpp"

class UiBridge;

class DspEngineWorker {
public:
    DspEngineWorker(const AppConfig& config,
                    SpscRingBuffer<AudioFramePtr>& dsp_queue,
                    UiBridge* ui_bridge);
    ~DspEngineWorker();

    void start();
    void stop();

private:
    void dsp_loop();
    DspUiFrame process_block(const AudioFrame& block);
    void ensure_workspace(size_t sample_count);
    std::vector<float> compute_fft_db(const std::vector<float>& samples, size_t fft_size);
    std::vector<float> build_ui_envelope(const std::vector<float>& samples) const;
    void set_realtime_priority() const;

private:
    AppConfig config_;
    SpscRingBuffer<AudioFramePtr>& dsp_queue_;
    UiBridge* ui_bridge_ = nullptr;

    std::vector<float> mono_buffer_;
    std::vector<float> window_cache_;
    size_t window_cache_size_ = 0;
    std::vector<std::complex<float>> fft_buffer_;

    std::atomic<bool> running_ {false};
    std::thread dsp_thread_;
};
