#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/goertzel.h"
#include "native_bridge.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

class WefaxEngine : public IModemEngine {
public:
    const char* get_id() const override { return "hf_wefax"; }
    const char* get_display_name() const override { return "HF WEFAX (120 LPM Fax)"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        samples_per_line_ = sample_rate_ / 2; // 0.5s per line at 120 LPM (24,000 samples)
        reset();
    }

    void reset() override {
        state_ = State::WaitStartTone;
        current_line_ = 0;
        line_sample_idx_ = 0;
        start_tone_detected_ = false;

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    bool is_rx_active() const override {
        return state_ == State::ActiveDemodulation;
    }

    size_t get_max_burst_samples() const override {
        return static_cast<size_t>(sample_rate_ * 300); // 5 minutes for WEFAX
    }

    float detect_instantaneous_frequency(float s) {
        sample_idx_++;
        if ((prev_s_ <= 0.0f && s > 0.0f) || (prev_s_ >= 0.0f && s < 0.0f)) {
            float frac = (s != prev_s_) ? (-prev_s_) / (s - prev_s_) : 0.5f;
            float cross_idx = static_cast<float>(sample_idx_ - 1) + frac;
            if (last_cross_idx_ > 0.0f) {
                float half_samples = cross_idx - last_cross_idx_;
                if (half_samples >= 8.0f && half_samples <= 48.0f) {
                    float f = static_cast<float>(sample_rate_) / (2.0f * half_samples);
                    current_freq_ = 0.70f * current_freq_ + 0.30f * f;
                }
            }
            last_cross_idx_ = cross_idx;
        }
        prev_s_ = s;
        return current_freq_;
    }

    void process_rx(const float* samples, size_t count) override {
        uint8_t* canvas = Native_GetSharedCanvasPtr();

        for (size_t i = 0; i < count; ++i) {
            float s = samples[i];
            analysis_window_.push_back(s);
            float inst_freq = detect_instantaneous_frequency(s);

            if (analysis_window_.size() >= 512) {
                if (state_ == State::WaitStartTone) {
                    float m1200 = dsp::Goertzel::compute_magnitude(analysis_window_.data(), analysis_window_.size(), 1200.0f, static_cast<float>(sample_rate_));
                    if (m1200 > 0.04f) {
                        state_ = State::ActiveDemodulation;
                        current_line_ = 0;
                        line_sample_idx_ = 0;
                    }
                } else if (state_ == State::ActiveDemodulation) {
                    float m450 = dsp::Goertzel::compute_magnitude(analysis_window_.data(), analysis_window_.size(), 450.0f, static_cast<float>(sample_rate_));
                    if (m450 > 0.05f) {
                        // Stop tone detected
                        state_ = State::WaitStartTone;
                        if (callback_) {
                            NativeModemEvent ev{};
                            ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                            ev.protocol_id = get_id();
                            ev.snr_db = 21.0f;
                            ev.center_freq = 1900;
                            ev.payload = nullptr;
                            ev.payload_len = 0;
                            ev.metadata_int = current_line_;
                            callback_(&ev);
                        }
                    }
                }
                analysis_window_.clear();
            }

            if (state_ == State::ActiveDemodulation) {
                // FM Demodulation: 1500 Hz (Black) to 2300 Hz (White)
                float norm_lum = (inst_freq - 1500.0f) / 800.0f;
                norm_lum = std::clamp(norm_lum, 0.0f, 1.0f);
                uint8_t pixel_val = static_cast<uint8_t>(norm_lum * 255.0f);

                int x = static_cast<int>((line_sample_idx_ * 640) / samples_per_line_);
                int y = current_line_;

                if (canvas && x < 640 && y < 496) {
                    size_t idx = (y * 640 + x) * 4;
                    canvas[idx + 0] = pixel_val;
                    canvas[idx + 1] = pixel_val;
                    canvas[idx + 2] = pixel_val;
                    canvas[idx + 3] = 255;
                }

                line_sample_idx_++;
                if (line_sample_idx_ >= samples_per_line_) {
                    line_sample_idx_ = 0;
                    current_line_++;

                    if (callback_ && (current_line_ % 2 == 0)) {
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::RasterLineReady);
                        ev.protocol_id = get_id();
                        ev.snr_db = 20.0f;
                        ev.center_freq = 1900;
                        ev.payload = nullptr;
                        ev.payload_len = 0;
                        ev.metadata_int = current_line_;
                        callback_(&ev);
                    }

                    if (current_line_ >= 496) {
                        current_line_ = 0; // Wrap or complete
                    }
                }
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 512) return 0.0f;
        float start_mag = dsp::Goertzel::compute_magnitude(samples, count, 1200.0f, static_cast<float>(sample_rate_));
        return (start_mag > 0.04f) ? std::min(1.0f, start_mag * 18.0f) : 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        tx_samples_.clear();
        tx_playback_pos_ = 0;

        const float two_pi = 6.28318530717958647692f;
        float phase = 0.0f;

        auto append_tone = [&](float freq, float duration_sec) {
            size_t num_s = static_cast<size_t>(sample_rate_ * duration_sec);
            float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
            for (size_t s = 0; s < num_s; ++s) {
                tx_samples_.push_back(0.4f * std::sin(phase));
                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
            }
        };

        // 1. Start Tone: 1200 Hz for 1.0s (shortened for testing responsiveness)
        append_tone(1200.0f, 1.0f);

        // 2. Phasing Lines: 5 lines of 5ms black (1500 Hz) + 495ms white (2300 Hz)
        for (int p = 0; p < 5; ++p) {
            append_tone(1500.0f, 0.005f);
            append_tone(2300.0f, 0.495f);
        }

        // 3. Image scanlines: 16 lines test sweep or user payload
        for (int l = 0; l < 16; ++l) {
            size_t half_line = samples_per_line_ / 2;
            for (size_t s = 0; s < samples_per_line_; ++s) {
                float freq;
                if (payload && len > 1) {
                    size_t p_idx = (l * 16 + (s * 16 / samples_per_line_)) % len;
                    uint8_t byte_val = payload[p_idx];
                    freq = 1500.0f + 800.0f * (static_cast<float>(byte_val) / 255.0f);
                } else {
                    freq = (s < half_line) ? 1500.0f : 2300.0f; // Half black, half white
                }
                float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                tx_samples_.push_back(0.4f * std::sin(phase));
                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
            }
        }

        // 4. Stop Tone: 450 Hz for 0.5s
        append_tone(450.0f, 0.5f);

        is_tx_active_ = true;
        return true;
    }

    size_t pull_tx(float* output, size_t max_samples) override {
        if (!is_tx_active_ || tx_playback_pos_ >= tx_samples_.size()) {
            is_tx_active_ = false;
            return 0;
        }
        size_t available = tx_samples_.size() - tx_playback_pos_;
        size_t count = std::min(max_samples, available);
        std::copy(tx_samples_.data() + tx_playback_pos_, tx_samples_.data() + tx_playback_pos_ + count, output);
        tx_playback_pos_ += count;
        if (tx_playback_pos_ >= tx_samples_.size()) {
            is_tx_active_ = false;
        }
        return count;
    }

    bool is_tx_active() const override { return is_tx_active_; }

private:
    enum class State {
        WaitStartTone,
        ActiveDemodulation
    };

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    size_t samples_per_line_{24000};

    State state_{State::WaitStartTone};
    int current_line_{0};
    size_t line_sample_idx_{0};
    bool start_tone_detected_{false};
    std::vector<float> analysis_window_;
    float prev_s_{0.0f};
    size_t sample_idx_{0};
    float last_cross_idx_{0.0f};
    float current_freq_{1900.0f};

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(WefaxEngine, "hf_wefax");
