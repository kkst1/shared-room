#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types.hpp"

// 旁路回放：
// eMMC WAV -> libsamplerate 重采样到 48k -> ALSA snd_pcm 播放到 I2S/PCM5122。
class OfflineBypassPlayer {
public:
    explicit OfflineBypassPlayer(const AppConfig& config);

    // 一次性执行旁路回放流程。
    bool play_wav_file(const std::string& wav_path);

private:
    struct WavData {
        uint32_t sample_rate = 0;
        uint16_t channels = 0;
        std::vector<float> samples_f32; // 归一化到 [-1,1] 便于重采样。
    };

    // 读取 PCM WAV（16/24/32bit 简化支持）。
    bool read_wav(const std::string& wav_path, WavData& out) const;

    // 使用 libsamplerate 转到目标采样率。
    std::vector<float> resample_to_target(const WavData& in) const;

    // 使用 ALSA PCM 接口播放 float 数据（内部转 s16）。
    bool play_with_alsa(const std::vector<float>& pcm_f32, uint32_t sample_rate, uint16_t channels) const;

private:
    AppConfig config_;
};

