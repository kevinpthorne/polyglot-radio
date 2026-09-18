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

    // 1. Audio Playback (TX)
    if (out_f32) {
        if (hal->is_tx_active()) {
            // Read from TX queue in HAL
            // Note: zero mutex lock in real-time callback; handled via SPSC or pre-filled buffer
        } else {
            std::fill(out_f32, out_f32 + frameCount, 0.0f);
        }
    }

    // 2. Audio Capture (RX)
    if (in_f32) {
        if (!hal->is_loopback_mode()) {
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

bool AudioHAL::init(int sample_rate, bool enable_loopback) {
    if (is_running_.load()) return true;

    loopback_mode_.store(enable_loopback);
    ring_buffer_.reset();

    if (!enable_loopback) {
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
        if (ma_device_init(NULL, &config, device) != MA_SUCCESS) {
            delete device;
            ma_device_handle_ = nullptr;
            // Fall back gracefully to virtual software mode if hardware is unavailable
            loopback_mode_.store(true);
        } else {
            ma_device_handle_ = device;
            if (ma_device_start(device) != MA_SUCCESS) {
                ma_device_uninit(device);
                delete device;
                ma_device_handle_ = nullptr;
                loopback_mode_.store(true);
            }
        }
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
    }

    return true;
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
    if (count >= 512) {
        std::vector<dsp::Complex> fft_in(1024, dsp::Complex(0.0f, 0.0f));
        for (size_t i = 0; i < 512; ++i) {
            fft_in[i] = dsp::Complex(samples[count - 512 + i], 0.0f);
        }
        dsp::FFT::forward(fft_in);
        std::vector<float> mags;
        dsp::FFT::compute_magnitudes(fft_in, mags);

        std::lock_guard<std::mutex> lock(fft_mutex_);
        latest_fft_magnitudes_ = mags;
    }

    // 4. Pass samples to DSP sentry / active demodulator callback
    if (rx_callback_) {
        rx_callback_(samples, count);
    }
}

void AudioHAL::get_latest_fft(float* out_magnitudes, size_t count) {
    std::lock_guard<std::mutex> lock(fft_mutex_);
    size_t copy_cnt = std::min(count, latest_fft_magnitudes_.size());
    std::copy(latest_fft_magnitudes_.begin(), latest_fft_magnitudes_.begin() + copy_cnt, out_magnitudes);
}

} // namespace hal
