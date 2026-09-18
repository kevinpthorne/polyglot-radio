#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include "audio_hal.h"
#include "dsp/fft.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace hal {

static void miniaudio_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioHAL* hal = static_cast<AudioHAL*>(pDevice->pUserData);
    if (!hal || !hal->is_running()) return;

    float* out_f32 = static_cast<float*>(pOutput);
    const float* in_f32 = static_cast<const float*>(pInput);

    // 1. Audio Playback (TX or Recorded Audio Playback)
    if (out_f32) {
        if (hal->is_output_muted()) {
            std::fill(out_f32, out_f32 + frameCount, 0.0f);
        } else if (hal->is_tx_active()) {
            hal->pull_tx_samples(out_f32, frameCount);
        } else if (hal->is_audio_playing()) {
            hal->pull_playback_samples(out_f32, frameCount);
        } else {
            std::fill(out_f32, out_f32 + frameCount, 0.0f);
        }
    }

    // 2. Audio Capture (RX)
    if (in_f32) {
        if (!hal->is_loopback_mode() && !hal->is_input_muted()) {
            hal->inject_rx_samples(in_f32, frameCount);
        }
    }
}

AudioHAL::AudioHAL() {
    latest_fft_magnitudes_.assign(512, 0.0f);
}

AudioHAL::~AudioHAL() {
    shutdown();
}

bool AudioHAL::start_hardware_device(int sample_rate) {
    if (ma_device_handle_) return true;

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.sampleRate = sample_rate;
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = miniaudio_data_callback;
    config.pUserData = this;

    ma_device* device = new ma_device();
    ma_result res = ma_device_init(NULL, &config, device);
    if (res != MA_SUCCESS) {
        std::cerr << "[AudioHAL] ma_device_init failed with error code: " << res << std::endl;
        delete device;
        ma_device_handle_ = nullptr;
        return false;
    }

    res = ma_device_start(device);
    if (res != MA_SUCCESS) {
        std::cerr << "[AudioHAL] ma_device_start failed with error code: " << res << std::endl;
        ma_device_uninit(device);
        delete device;
        ma_device_handle_ = nullptr;
        return false;
    }

    ma_device_handle_ = device;
    std::cout << "[AudioHAL] Hardware duplex audio device started at " << sample_rate << " Hz" << std::endl;
    return true;
}

void AudioHAL::set_loopback_mode(bool enabled) {
    loopback_mode_.store(enabled);
    if (!enabled && !ma_device_handle_ && is_running_.load()) {
        start_hardware_device(kSampleRate);
    }
}

bool AudioHAL::init(int sample_rate, bool enable_loopback) {
    if (is_running_.load()) {
        set_loopback_mode(enable_loopback);
        return true;
    }

    loopback_mode_.store(enable_loopback);
    ring_buffer_.reset();

    bool hw_ok = start_hardware_device(sample_rate);
    if (!hw_ok) {
        // Fallback to virtual loopback if hardware is not available (e.g. headless CI)
        loopback_mode_.store(true);
    }

    is_running_.store(true);
    return true;
}

void AudioHAL::shutdown() {
    if (!is_running_.load()) return;
    is_running_.store(false);

    if (ma_device_handle_) {
        ma_device* device = static_cast<ma_device*>(ma_device_handle_);
        ma_device_stop(device);
        ma_device_uninit(device);
        delete device;
        ma_device_handle_ = nullptr;
    }
    abort_tx();
    stop_audio_playback();
}

bool AudioHAL::queue_tx_samples(const float* samples, size_t count) {
    if (!samples || count == 0) return false;

    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        tx_buffer_.assign(samples, samples + count);
        tx_playback_index_ = 0;
        is_transmitting_.store(true);
    }

    // In loopback mode, immediately feed back into RX pipeline
    if (loopback_mode_.load()) {
        inject_rx_samples(samples, count);
        // If running in headless mode without physical audio device, finish TX immediately
        if (!ma_device_handle_) {
            is_transmitting_.store(false);
        }
    }

    return true;
}

size_t AudioHAL::pull_tx_samples(float* output, size_t count) {
    if (!output || count == 0) return 0;
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!is_transmitting_.load() || tx_playback_index_ >= tx_buffer_.size()) {
        std::fill(output, output + count, 0.0f);
        is_transmitting_.store(false);
        return 0;
    }

    size_t available = tx_buffer_.size() - tx_playback_index_;
    size_t to_copy = std::min(count, available);
    std::copy(tx_buffer_.data() + tx_playback_index_, tx_buffer_.data() + tx_playback_index_ + to_copy, output);
    tx_playback_index_ += to_copy;

    if (to_copy < count) {
        std::fill(output + to_copy, output + count, 0.0f);
    }

    if (tx_playback_index_ >= tx_buffer_.size()) {
        is_transmitting_.store(false);
    }
    return to_copy;
}

bool AudioHAL::is_tx_active() const {
    return is_transmitting_.load();
}

void AudioHAL::abort_tx() {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    is_transmitting_.store(false);
    tx_buffer_.clear();
    tx_playback_index_ = 0;
}

bool AudioHAL::play_audio_file(const std::string& path) {
    drwav wav;
    if (!drwav_init_file(&wav, path.c_str(), NULL)) {
        return false;
    }

    drwav_uint64 total_frames = wav.totalPCMFrameCount;
    if (total_frames == 0) {
        drwav_uninit(&wav);
        return false;
    }

    std::vector<float> file_samples(total_frames * wav.channels);
    drwav_uint64 frames_read = drwav_read_pcm_frames_f32(&wav, total_frames, file_samples.data());
    drwav_uninit(&wav);

    if (frames_read == 0) return false;

    // Downmix to mono if multi-channel
    std::vector<float> mono_samples(frames_read);
    if (wav.channels > 1) {
        for (drwav_uint64 i = 0; i < frames_read; ++i) {
            float sum = 0.0f;
            for (unsigned int c = 0; c < wav.channels; ++c) {
                sum += file_samples[i * wav.channels + c];
            }
            mono_samples[i] = sum / wav.channels;
        }
    } else {
        std::copy(file_samples.begin(), file_samples.begin() + frames_read, mono_samples.begin());
    }

    // Resample linearly to kSampleRate (48000) if file sample rate differs
    std::vector<float> final_samples;
    if (wav.sampleRate != static_cast<unsigned int>(kSampleRate) && wav.sampleRate > 0) {
        double ratio = static_cast<double>(kSampleRate) / wav.sampleRate;
        size_t out_len = static_cast<size_t>(frames_read * ratio);
        final_samples.resize(out_len);
        for (size_t i = 0; i < out_len; ++i) {
            double src_idx = i / ratio;
            size_t idx0 = static_cast<size_t>(src_idx);
            size_t idx1 = std::min(idx0 + 1, static_cast<size_t>(frames_read - 1));
            float frac = static_cast<float>(src_idx - idx0);
            final_samples[i] = (1.0f - frac) * mono_samples[idx0] + frac * mono_samples[idx1];
        }
    } else {
        final_samples = std::move(mono_samples);
    }

    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        playback_buffer_ = std::move(final_samples);
        playback_index_ = 0;
        is_playback_paused_.store(false);
        is_playback_active_.store(true);
    }

    return true;
}

void AudioHAL::pause_audio_playback() {
    is_playback_paused_.store(true);
}

void AudioHAL::resume_audio_playback() {
    is_playback_paused_.store(false);
}

void AudioHAL::stop_audio_playback() {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    is_playback_active_.store(false);
    is_playback_paused_.store(false);
    playback_buffer_.clear();
    playback_index_ = 0;
}

bool AudioHAL::is_audio_playing() const {
    return is_playback_active_.load();
}

float AudioHAL::get_audio_playback_position() const {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    if (!is_playback_active_.load()) return 0.0f;
    return static_cast<float>(playback_index_) / static_cast<float>(kSampleRate);
}

float AudioHAL::get_audio_playback_duration() const {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    if (playback_buffer_.empty()) return 0.0f;
    return static_cast<float>(playback_buffer_.size()) / static_cast<float>(kSampleRate);
}

void AudioHAL::seek_audio_playback(float seconds) {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    if (playback_buffer_.empty()) return;
    size_t target_idx = static_cast<size_t>(std::max(0.0f, seconds) * kSampleRate);
    playback_index_ = std::min(target_idx, playback_buffer_.size());
    if (playback_index_ >= playback_buffer_.size()) {
        is_playback_active_.store(false);
    }
}

size_t AudioHAL::pull_playback_samples(float* output, size_t count) {
    if (!output || count == 0) return 0;
    std::lock_guard<std::mutex> lock(playback_mutex_);
    if (!is_playback_active_.load() || playback_index_ >= playback_buffer_.size()) {
        std::fill(output, output + count, 0.0f);
        is_playback_active_.store(false);
        return 0;
    }

    if (is_playback_paused_.load()) {
        std::fill(output, output + count, 0.0f);
        return count;
    }

    size_t available = playback_buffer_.size() - playback_index_;
    size_t to_copy = std::min(count, available);
    std::copy(playback_buffer_.data() + playback_index_, playback_buffer_.data() + playback_index_ + to_copy, output);
    playback_index_ += to_copy;

    if (to_copy < count) {
        std::fill(output + to_copy, output + count, 0.0f);
    }

    if (playback_index_ >= playback_buffer_.size()) {
        is_playback_active_.store(false);
    }
    return to_copy;
}

void AudioHAL::inject_rx_samples(const float* samples, size_t count) {
    if (count == 0) return;

    // 1. Write to SPSC ring pre-buffer
    ring_buffer_.write(samples, count);

    // 2. Compute RMS and evaluate Squelch
    float sum_sq = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        sum_sq += samples[i] * samples[i];
    }
    float rms = std::sqrt(sum_sq / count);
    float rms_db = 20.0f * std::log10(rms + 1e-9f);
    current_rms_db_.store(rms_db);

    // Track rolling noise floor
    float floor = noise_floor_db_.load();
    if (rms_db < floor + 3.0f) {
        floor = 0.99f * floor + 0.01f * rms_db;
        noise_floor_db_.store(floor);
    }

    // Trip squelch if energy exceeds noise floor by +6 dB or threshold
    bool open = (rms_db > (floor + 6.0f)) || (rms_db > squelch_threshold_db_.load());
    squelch_open_.store(open);

    // 3. Compute 512-point FFT snapshot for live waterfall visualization
    {
        std::lock_guard<std::mutex> lock(fft_mutex_);
        fft_accum_buffer_.insert(fft_accum_buffer_.end(), samples, samples + count);
        if (fft_accum_buffer_.size() >= 512) {
            std::vector<dsp::Complex> fft_in(512);
            size_t offset = fft_accum_buffer_.size() - 512;
            for (size_t i = 0; i < 512; ++i) {
                fft_in[i] = dsp::Complex(fft_accum_buffer_[offset + i], 0.0f);
            }
            dsp::FFT::forward(fft_in);
            std::vector<float> mags;
            dsp::FFT::compute_magnitudes(fft_in, mags);
            latest_fft_magnitudes_ = std::move(mags);

            // Keep rolling history bounded
            if (fft_accum_buffer_.size() > 2048) {
                fft_accum_buffer_.erase(fft_accum_buffer_.begin(), fft_accum_buffer_.begin() + (fft_accum_buffer_.size() - 512));
            }
        }
    }

    // 4. Pass samples to DSP sentry / active demodulator callback
    // When playing back recorded audio through speakers, suppress sentry to prevent acoustic echo loops
    if (rx_callback_ && !is_audio_playing()) {
        rx_callback_(samples, count);
    }
}

void AudioHAL::get_latest_fft(float* out_magnitudes, size_t count) {
    std::lock_guard<std::mutex> lock(fft_mutex_);
    size_t copy_cnt = std::min(count, latest_fft_magnitudes_.size());
    std::copy(latest_fft_magnitudes_.begin(), latest_fft_magnitudes_.begin() + copy_cnt, out_magnitudes);
}

} // namespace hal
