#include "playback_bypass.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <alsa/asoundlib.h>
#include <samplerate.h>

namespace {
template <typename T>
bool read_exact(std::ifstream& ifs, T& out) {
    ifs.read(reinterpret_cast<char*>(&out), sizeof(T));
    return static_cast<bool>(ifs);
}
} // namespace

OfflineBypassPlayer::OfflineBypassPlayer(const AppConfig& config) : config_(config) {}

bool OfflineBypassPlayer::play_wav_file(const std::string& wav_path) {
    WavData wav {};
    if (!read_wav(wav_path, wav)) {
        return false;
    }

    auto resampled = resample_to_target(wav);
    if (resampled.empty()) {
        return false;
    }

    return play_with_alsa(resampled, config_.playback_rate, wav.channels);
}

bool OfflineBypassPlayer::read_wav(const std::string& wav_path, WavData& out) const {
    std::ifstream ifs(wav_path, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }

    char riff[4] {};
    char wave[4] {};
    uint32_t riff_size = 0;
    ifs.read(riff, 4);
    read_exact(ifs, riff_size);
    ifs.read(wave, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    std::vector<uint8_t> data_bytes;

    // 逐 chunk 解析，兼容 fmt/data 之间有附加 chunk 的情况。
    while (ifs.good()) {
        char chunk_id[4] {};
        uint32_t chunk_size = 0;
        ifs.read(chunk_id, 4);
        if (!read_exact(ifs, chunk_size)) {
            break;
        }

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t block_align = 0;
            uint32_t byte_rate = 0;
            read_exact(ifs, audio_format);
            read_exact(ifs, channels);
            read_exact(ifs, sample_rate);
            read_exact(ifs, byte_rate);
            read_exact(ifs, block_align);
            read_exact(ifs, bits_per_sample);
            // 跳过 fmt 扩展字段。
            if (chunk_size > 16) {
                ifs.seekg(chunk_size - 16, std::ios::cur);
            }
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            data_bytes.resize(chunk_size);
            ifs.read(reinterpret_cast<char*>(data_bytes.data()), chunk_size);
        } else {
            // 非关键 chunk 直接跳过。
            ifs.seekg(chunk_size, std::ios::cur);
        }
    }

    if (audio_format != 1 || channels == 0 || sample_rate == 0 || data_bytes.empty()) {
        return false;
    }

    out.sample_rate = sample_rate;
    out.channels = channels;

    // PCM 整数转 float [-1, 1]。
    if (bits_per_sample == 16) {
        const int16_t* p = reinterpret_cast<const int16_t*>(data_bytes.data());
        const size_t count = data_bytes.size() / sizeof(int16_t);
        out.samples_f32.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.samples_f32[i] = static_cast<float>(p[i]) / 32768.0f;
        }
    } else if (bits_per_sample == 32) {
        const int32_t* p = reinterpret_cast<const int32_t*>(data_bytes.data());
        const size_t count = data_bytes.size() / sizeof(int32_t);
        out.samples_f32.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.samples_f32[i] = static_cast<float>(p[i]) / 2147483648.0f;
        }
    } else {
        // 若你使用 24bit，可在这里补充解包逻辑。
        return false;
    }

    return true;
}

std::vector<float> OfflineBypassPlayer::resample_to_target(const WavData& in) const {
    if (in.sample_rate == config_.playback_rate) {
        return in.samples_f32;
    }

    const double ratio = static_cast<double>(config_.playback_rate) / static_cast<double>(in.sample_rate);
    const long in_frames = static_cast<long>(in.samples_f32.size() / in.channels);
    const long out_frames = static_cast<long>(in_frames * ratio) + 16;

    std::vector<float> out(static_cast<size_t>(out_frames * in.channels), 0.0f);

    SRC_DATA src {};
    src.data_in = in.samples_f32.data();
    src.input_frames = in_frames;
    src.data_out = out.data();
    src.output_frames = out_frames;
    src.src_ratio = ratio;

    // 使用高质量 sinc 模式重采样。
    const int err = src_simple(&src, SRC_SINC_BEST_QUALITY, in.channels);
    if (err != 0) {
        return {};
    }

    out.resize(static_cast<size_t>(src.output_frames_gen * in.channels));
    return out;
}

bool OfflineBypassPlayer::play_with_alsa(const std::vector<float>& pcm_f32,
                                         uint32_t sample_rate,
                                         uint16_t channels) const {
    snd_pcm_t* pcm = nullptr;
    int rc = snd_pcm_open(&pcm, config_.alsa_device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        return false;
    }

    // 这里用 S16_LE 播放，兼容性最好；若硬件支持也可切到 S32_LE。
    rc = snd_pcm_set_params(pcm,
                            SND_PCM_FORMAT_S16_LE,
                            SND_PCM_ACCESS_RW_INTERLEAVED,
                            channels,
                            sample_rate,
                            1,
                            20000);
    if (rc < 0) {
        snd_pcm_close(pcm);
        return false;
    }

    // float -> int16。
    std::vector<int16_t> pcm_i16(pcm_f32.size());
    for (size_t i = 0; i < pcm_f32.size(); ++i) {
        float v = std::clamp(pcm_f32[i], -1.0f, 1.0f);
        pcm_i16[i] = static_cast<int16_t>(v * 32767.0f);
    }

    const snd_pcm_uframes_t total_frames = pcm_i16.size() / channels;
    snd_pcm_uframes_t written = 0;
    while (written < total_frames) {
        const int16_t* ptr = pcm_i16.data() + written * channels;
        snd_pcm_sframes_t n = snd_pcm_writei(pcm, ptr, total_frames - written);
        if (n < 0) {
            // 发生 underrun 等错误时交给 ALSA 恢复。
            n = snd_pcm_recover(pcm, static_cast<int>(n), 1);
            if (n < 0) {
                snd_pcm_close(pcm);
                return false;
            }
            continue;
        }
        written += static_cast<snd_pcm_uframes_t>(n);
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    return true;
}

