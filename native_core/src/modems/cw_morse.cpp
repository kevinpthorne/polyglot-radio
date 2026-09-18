#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/fir_filter.h"
#include "dsp/morse_table.h"
#include "dsp/goertzel.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <cctype>

class CwMorseEngine : public IModemEngine {
public:
    const char* get_id() const override { return "cw_morse"; }
    const char* get_display_name() const override { return "CW Morse Code"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        tracked_pitch_ = 700.0f;
        wpm_ = 20.0f;
        dit_samples_ = static_cast<size_t>(sample_rate_ * 1.2f / wpm_);
        
        // Design 48-tap FIR bandpass around tracked pitch
        fir_.set_coefficients(dsp::FIRFilter::design_bandpass(49, tracked_pitch_, 150.0f, static_cast<float>(sample_rate_)));
        reset();
    }

    void reset() override {
        fir_.reset();
        envelope_peak_ = 0.05f;
        current_state_ = false;
        state_duration_ = 0;
        current_symbol_.clear();
        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        if (count == 0) return;

        for (size_t i = 0; i < count; ++i) {
            float in_sample = samples[i];
            float filtered = fir_.process_sample(in_sample);
            float rect = std::abs(filtered);

            // Smoothed envelope follower (time constant ~ 5ms)
            smoothed_env_ = 0.995f * smoothed_env_ + 0.005f * rect;

            // Adaptive peak envelope tracking
            if (smoothed_env_ > envelope_peak_) {
                envelope_peak_ = 0.999f * envelope_peak_ + 0.001f * smoothed_env_;
            } else {
                envelope_peak_ = 0.99995f * envelope_peak_;
            }
            if (envelope_peak_ < 0.005f) envelope_peak_ = 0.005f;

            // Schmitt-trigger thresholds
            float t_high = 0.35f * envelope_peak_;
            float t_low = 0.18f * envelope_peak_;

            bool tone_on = current_state_;
            if (!current_state_ && smoothed_env_ > t_high) {
                tone_on = true;
            } else if (current_state_ && smoothed_env_ < t_low) {
                tone_on = false;
            }

            if (tone_on == current_state_) {
                state_duration_++;
                // Check if silence has lasted long enough to flush the pending symbol
                if (!current_state_ && !current_symbol_.empty() && state_duration_ >= 2 * dit_samples_) {
                    char decoded = dsp::MorseTable::decode_morse(current_symbol_);
                    current_symbol_.clear();
                    if (decoded != '?' && callback_) {
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::TextStream);
                        ev.protocol_id = get_id();
                        ev.snr_db = 15.0f;
                        ev.center_freq = static_cast<int32_t>(tracked_pitch_);
                        ev.payload = reinterpret_cast<const uint8_t*>(&decoded);
                        ev.payload_len = 1;
                        ev.metadata_int = static_cast<int32_t>(wpm_);
                        callback_(&ev);
                    }
                }
            } else {
                // State changed
                handle_state_transition(current_state_, state_duration_);
                current_state_ = tone_on;
                state_duration_ = 1;
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 256) return 0.0f;
        // Check energy at 700 Hz
        float mag = dsp::Goertzel::compute_magnitude(samples, count, tracked_pitch_, static_cast<float>(sample_rate_));
        if (mag > 0.02f) {
            return std::min(1.0f, mag * 20.0f);
        }
        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;
        std::string text(reinterpret_cast<const char*>(payload), len);
        
        tx_samples_.clear();
        tx_playback_pos_ = 0;

        float pitch = 700.0f;
        float wpm = 20.0f; // 20 WPM standard
        float dit_duration = 1.2f / wpm; // seconds
        size_t dit_len = static_cast<size_t>(sample_rate_ * dit_duration);
        size_t dah_len = 3 * dit_len;
        size_t inter_element = dit_len;
        size_t inter_letter = 3 * dit_len;
        size_t inter_word = 7 * dit_len;

        const float two_pi = 6.28318530717958647692f;
        float phase = 0.0f;
        float phase_step = two_pi * pitch / static_cast<float>(sample_rate_);

        auto append_tone = [&](size_t num_samples) {
            size_t ramp_len = std::min(num_samples / 4, static_cast<size_t>(sample_rate_ * 0.005f)); // 5ms ramp
            for (size_t s = 0; s < num_samples; ++s) {
                float env = 1.0f;
                if (s < ramp_len) {
                    env = 0.5f * (1.0f - std::cos(3.14159265f * s / ramp_len));
                } else if (s > num_samples - ramp_len) {
                    size_t rem = num_samples - s;
                    env = 0.5f * (1.0f - std::cos(3.14159265f * rem / ramp_len));
                }
                tx_samples_.push_back(0.4f * env * std::sin(phase));
                phase += phase_step;
                if (phase > two_pi) phase -= two_pi;
            }
        };

        auto append_silence = [&](size_t num_samples) {
            tx_samples_.insert(tx_samples_.end(), num_samples, 0.0f);
        };

        // Lead-in silence
        append_silence(dit_len * 2);

        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == ' ') {
                append_silence(inter_word);
                continue;
            }
            std::string morse = dsp::MorseTable::encode_char(c);
            if (morse.empty()) continue;

            for (size_t m = 0; m < morse.size(); ++m) {
                if (morse[m] == '.') {
                    append_tone(dit_len);
                } else if (morse[m] == '-') {
                    append_tone(dah_len);
                }
                if (m + 1 < morse.size()) {
                    append_silence(inter_element);
                }
            }
            append_silence(inter_letter);
        }

        // Tail silence
        append_silence(dit_len * 2);
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

    bool is_tx_active() const override {
        return is_tx_active_;
    }

private:
    void handle_state_transition(bool was_on, size_t duration) {
        float duration_sec = static_cast<float>(duration) / static_cast<float>(sample_rate_);
        float dit_unit = 1.2f / wpm_;

        if (was_on) {
            // Tone ended: decide dot or dash
            if (duration_sec < 2.0f * dit_unit) {
                current_symbol_ += '.';
            } else {
                current_symbol_ += '-';
            }
        } else {
            // Silence ended: decide character or word break
            if (duration_sec >= 2.0f * dit_unit && !current_symbol_.empty()) {
                char decoded = dsp::MorseTable::decode_morse(current_symbol_);
                current_symbol_.clear();
                if (decoded != '?' && callback_) {
                    NativeModemEvent ev{};
                    ev.event_type = static_cast<int32_t>(EventType::TextStream);
                    ev.protocol_id = get_id();
                    ev.snr_db = 15.0f;
                    ev.center_freq = static_cast<int32_t>(tracked_pitch_);
                    ev.payload = reinterpret_cast<const uint8_t*>(&decoded);
                    ev.payload_len = 1;
                    ev.metadata_int = static_cast<int32_t>(wpm_);
                    callback_(&ev);
                }
            }
            if (duration_sec >= 5.0f * dit_unit && callback_) {
                char space = ' ';
                NativeModemEvent ev{};
                ev.event_type = static_cast<int32_t>(EventType::TextStream);
                ev.protocol_id = get_id();
                ev.snr_db = 15.0f;
                ev.center_freq = static_cast<int32_t>(tracked_pitch_);
                ev.payload = reinterpret_cast<const uint8_t*>(&space);
                ev.payload_len = 1;
                ev.metadata_int = static_cast<int32_t>(wpm_);
                callback_(&ev);
            }
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    float tracked_pitch_{700.0f};
    float wpm_{20.0f};
    size_t dit_samples_{2880};

    dsp::FIRFilter fir_;
    float envelope_peak_{0.05f};
    float smoothed_env_{0.0f};
    bool current_state_{false};
    size_t state_duration_{0};
    std::string current_symbol_;

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(CwMorseEngine, "cw_morse");
