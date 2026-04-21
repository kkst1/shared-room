#include "playback_bypass.hpp"

#include <algorithm>
#include <array>
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

bool skip_bytes(std::ifstream& ifs, std::streamoff count) {
    ifs.seekg(count, std::ios::cur);
    return static_cast<bool>(ifs);
}

float decode_pcm24_le(const uint8_t* p) {
    int32_t value = static_cast<int32_t>(p[0]) |
                    (static_cast<int32_t>(p[1]) << 8) |
                    (static_cast<int32_t>(p[2]) << 16);
    if ((value & 0x00800000) != 0) {
        value |= ~0x00FFFFFF;
    }
    return static_cast<float>(value) / 8388608.0f;
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

    std::array<char, 4> riff {};
    std::array<char, 4> wave {};
    uint32_t riff_size = 0;
    ifs.read(riff.data(), static_cast<std::streamsize>(riff.size()));
    if (!read_exact(ifs, riff_size)) {
        return false;
    }
    ifs.read(wave.data(), static_cast<std::streamsize>(wave.size()));
    if (!ifs || std::strncmp(riff.data(), "RIFF", 4) != 0 || std::strncmp(wave.data(), "WAVE", 4) != 0) {
        return false;
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    std::vector<uint8_t> data_bytes;

    while (ifs.good()) {
        std::array<char, 4> chunk_id {};
        uint32_t chunk_size = 0;
        ifs.read(chunk_id.data(), static_cast<std::streamsize>(chunk_id.size()));
        if (!read_exact(ifs, chunk_size)) {
            break;
        }

        if (std::strncmp(chunk_id.data(), "fmt ", 4) == 0) {
            uint16_t block_align = 0;
            uint32_t byte_rate = 0;
            if (!read_exact(ifs, audio_format) ||
                !read_exact(ifs, channels) ||
                !read_exact(ifs, sample_rate) ||
                !read_exact(ifs, byte_rate) ||
                !read_exact(ifs, block_align) ||
                !read_exact(ifs, bits_per_sample)) {
                return false;
            }

            if (chunk_size > 16 && !skip_bytes(ifs, static_cast<std::streamoff>(chunk_size - 16))) {
                return false;
            }
        } else if (std::strncmp(chunk_id.data(), "data", 4) == 0) {
            data_bytes.resize(chunk_size);
            ifs.read(reinterpret_cast<char*>(data_bytes.data()), static_cast<std::streamsize>(chunk_size));
            if (!ifs) {
                return false;
            }
        } else if (!skip_bytes(ifs, chunk_size)) {
            return false;
        }

        if ((chunk_size & 0x1U) != 0U && !skip_bytes(ifs, 1)) {
            return false;
        }
    }

    if (audio_format != 1 || channels == 0 || sample_rate == 0 || data_bytes.empty()) {
        return false;
    }

    out.sample_rate = sample_rate;
    out.channels = channels;

    if (bits_per_sample == 16) {
        const int16_t* p = reinterpret_cast<const int16_t*>(data_bytes.data());
        const size_t count = data_bytes.size() / sizeof(int16_t);
        out.samples_f32.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.samples_f32[i] = static_cast<float>(p[i]) / 32768.0f;
        }
    } else if (bits_per_sample == 24) {
        const size_t count = data_bytes.size() / 3;
        out.samples_f32.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.samples_f32[i] = decode_pcm24_le(&data_bytes[i * 3]);
        }
    } else if (bits_per_sample == 32) {
        const int32_t* p = reinterpret_cast<const int32_t*>(data_bytes.data());
        const size_t count = data_bytes.size() / sizeof(int32_t);
        out.samples_f32.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.samples_f32[i] = static_cast<float>(p[i]) / 2147483648.0f;
        }
    } else {
        return false;
    }

    return !out.samples_f32.empty();
}

std::vector<float> OfflineBypassPlayer::resample_to_target(const WavData& in) const {
    if (in.sample_rate == config_.playback_rate) {
        return in.samples_f32;
    }

    const double ratio = static_cast<double>(config_.playback_rate) / static_cast<double>(in.sample_rate);
    const long in_frames = static_cast<long>(in.samples_f32.size() / in.channels);
    const long out_frames = static_cast<long>(in_frames * ratio) + 32;

    std::vector<float> out(static_cast<size_t>(out_frames * in.channels), 0.0f);

    SRC_DATA src {};
    src.data_in = in.samples_f32.data();
    src.input_frames = in_frames;
    src.data_out = out.data();
    src.output_frames = out_frames;
    src.src_ratio = ratio;
    src.end_of_input = 1;

    const int err = src_simple(&src, SRC_SINC_MEDIUM_QUALITY, in.channels);
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

    std::vector<int16_t> pcm_i16(pcm_f32.size());
    for (size_t i = 0; i < pcm_f32.size(); ++i) {
        const float clamped = std::clamp(pcm_f32[i], -1.0f, 1.0f);
        pcm_i16[i] = static_cast<int16_t>(clamped * 32767.0f);
    }

    const snd_pcm_uframes_t total_frames = static_cast<snd_pcm_uframes_t>(pcm_i16.size() / channels);
    snd_pcm_uframes_t written = 0;
    while (written < total_frames) {
        const int16_t* ptr = pcm_i16.data() + (written * channels);
        snd_pcm_sframes_t n = snd_pcm_writei(pcm, ptr, total_frames - written);
        if (n < 0) {
            n = snd_pcm_recover(pcm, static_cast<int>(n), 1);
            if (n < 0) {
                snd_pcm_close(pcm);
                return false;
            }
            continue;
        }
        written += static_cast<snd_pcm_uframes_t>(n);
    }

    const int drain_rc = snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    return drain_rc >= 0;
}
