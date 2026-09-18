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
        adaptive_dit_samples_ = static_cast<size_t>(sample_rate_ * 0.080f); // ~15 WPM initial
        
        update_fir_filter();
        reset();
    }

    void reset() override {
        fir_.reset();
        envelope_peak_ = 0.05f;
        noise_floor_ = 0.005f;
        smoothed_env_ = 0.0f;
        current_state_ = false;
        state_duration_ = 0;
        current_symbol_.clear();
        analysis_window_.clear();
        has_emitted_char_ = false;
        last_was_space_ = true;
        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void update_fir_filter() {
        fir_.set_coefficients(dsp::FIRFilter::design_bandpass(49, tracked_pitch_, 180.0f, static_cast<float>(sample_rate_)));
    }

    void process_rx(const float* samples, size_t count) override {
        if (count == 0) return;

        for (size_t i = 0; i < count; ++i) {
            float in_sample = samples[i];

            // Continuous audio spectrum scan to lock onto any third-party CW pitch
            analysis_window_.push_back(in_sample);
            if (analysis_window_.size() >= 512) {
                static const float kCwCandidates[] = {
                    450.0f, 500.0f, 550.0f, 600.0f, 650.0f, 700.0f, 750.0f, 800.0f, 850.0f
                };
                float best_freq = tracked_pitch_;
                float max_mag = 0.0f;
                float sum_mag = 0.0f;

                for (float f : kCwCandidates) {
                    float mag = dsp::Goertzel::compute_magnitude(analysis_window_.data(), analysis_window_.size(), f, static_cast<float>(sample_rate_));
                    sum_mag += mag;
                    if (mag > max_mag) {
                        max_mag = mag;
                        best_freq = f;
                    }
                }

                float avg_other = (sum_mag - max_mag) / 8.0f;
                if (max_mag > 0.024f && max_mag > 2.0f * (avg_other + 0.005f)) {
                    if (std::abs(best_freq - tracked_pitch_) > 20.0f) {
                        tracked_pitch_ = best_freq;
                        update_fir_filter();
                    }
                }
                analysis_window_.clear();
            }

            float filtered = fir_.process_sample(in_sample);
            float rect = std::abs(filtered);

            // Fast attack, smooth decay envelope follower (time constant ~ 4ms)
            if (rect > smoothed_env_) {
                smoothed_env_ = 0.85f * smoothed_env_ + 0.15f * rect;
            } else {
                smoothed_env_ = 0.996f * smoothed_env_ + 0.004f * rect;
            }

            // Adaptive peak and noise floor tracking
            if (current_state_) {
                envelope_peak_ = 0.999f * envelope_peak_ + 0.001f * smoothed_env_;
            } else {
                noise_floor_ = 0.999f * noise_floor_ + 0.001f * smoothed_env_;
                envelope_peak_ = 0.99995f * envelope_peak_;
            }
            if (envelope_peak_ < 0.008f) envelope_peak_ = 0.008f;
            if (noise_floor_ < 0.001f) noise_floor_ = 0.001f;
            if (noise_floor_ > envelope_peak_ * 0.7f) noise_floor_ = envelope_peak_ * 0.7f;

            // Schmitt-trigger thresholds relative to dynamic range
            float dynamic_range = std::max(0.004f, envelope_peak_ - noise_floor_);
            float t_high = noise_floor_ + 0.38f * dynamic_range;
            float t_low  = noise_floor_ + 0.18f * dynamic_range;

            bool tone_on = current_state_;
            if (!current_state_ && smoothed_env_ > t_high) {
                tone_on = true;
            } else if (current_state_ && smoothed_env_ < t_low) {
                tone_on = false;
            }

            if (tone_on == current_state_) {
                state_duration_++;
                // Character decode break: silence lasted >= 2.2 dit lengths
                if (!current_state_ && !current_symbol_.empty() && state_duration_ >= static_cast<size_t>(2.2f * adaptive_dit_samples_)) {
                    char decoded = dsp::MorseTable::decode_morse(current_symbol_);
                    current_symbol_.clear();
                    if (decoded != '?' && callback_) {
                        has_emitted_char_ = true;
                        last_was_space_ = false;
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::TextStream);
                        ev.protocol_id = get_id();
                        ev.snr_db = 15.0f;
                        ev.center_freq = static_cast<int32_t>(tracked_pitch_);
                        ev.payload = reinterpret_cast<const uint8_t*>(&decoded);
                        ev.payload_len = 1;
                        float cur_wpm = 1.2f / (static_cast<float>(adaptive_dit_samples_) / sample_rate_);
                        ev.metadata_int = static_cast<int32_t>(cur_wpm);
                        callback_(&ev);
                    }
                }
                // Word space break: silence lasted >= 5.0 dit lengths
                if (!current_state_ && has_emitted_char_ && !last_was_space_ && state_duration_ >= static_cast<size_t>(5.0f * adaptive_dit_samples_)) {
                    last_was_space_ = true;
                    if (callback_) {
                        char space = ' ';
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::TextStream);
                        ev.protocol_id = get_id();
                        ev.snr_db = 15.0f;
                        ev.center_freq = static_cast<int32_t>(tracked_pitch_);
                        ev.payload = reinterpret_cast<const uint8_t*>(&space);
                        ev.payload_len = 1;
                        float cur_wpm = 1.2f / (static_cast<float>(adaptive_dit_samples_) / sample_rate_);
                        ev.metadata_int = static_cast<int32_t>(cur_wpm);
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

        // Compute total RMS energy of the window
        float energy = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            energy += samples[i] * samples[i];
        }
        float rms = std::sqrt(energy / count);
        if (rms < 0.012f) return 0.0f;

        // Reject signals with significant high-frequency digital energy (Rattlegram, EAS, Bell 202, SSTV)
        float mag_1500 = dsp::Goertzel::compute_magnitude(samples, count, 1500.0f, static_cast<float>(sample_rate_));
        float mag_1700 = dsp::Goertzel::compute_magnitude(samples, count, 1700.0f, static_cast<float>(sample_rate_));
        float mag_2000 = dsp::Goertzel::compute_magnitude(samples, count, 2000.0f, static_cast<float>(sample_rate_));
        float max_digital = std::max({mag_1500, mag_1700, mag_2000});

        // Feld-Hell calling frequency check (980 Hz)
        float mag_hell = dsp::Goertzel::compute_magnitude(samples, count, 980.0f, static_cast<float>(sample_rate_));

        static const float kCwCandidates[] = {
            450.0f, 500.0f, 550.0f, 600.0f, 650.0f, 700.0f, 750.0f, 800.0f, 850.0f
        };
        float best_freq = tracked_pitch_;
        float max_mag = 0.0f;
        float sum_mag = 0.0f;

        for (float f : kCwCandidates) {
            float mag = dsp::Goertzel::compute_magnitude(samples, count, f, static_cast<float>(sample_rate_));
            sum_mag += mag;
            if (mag > max_mag) {
                max_mag = mag;
                best_freq = f;
            }
        }

        // If Feld-Hell carrier is dominant, yield to Feld-Hell
        if (mag_hell > 0.022f && mag_hell > max_mag * 0.85f) {
            return 0.0f;
        }

        // If high frequency digital carriers are present, reject (Rattlegram / Bell 202 / EAS)
        if (max_digital > 0.024f && max_digital > 0.35f * max_mag) {
            return 0.0f;
        }

        // Pure narrowband check: For a true CW single-tone carrier, max_mag / rms is >= 0.40
        // Wideband signals (OFDM, Rattlegram, wideband noise) distribute energy across frequencies so max_mag / rms < 0.20
        if (max_mag < 0.40f * rms) {
            return 0.0f;
        }

        float avg_other = (sum_mag - max_mag) / 8.0f;
        if (max_mag > 0.022f && max_mag > 2.2f * (avg_other + 0.004f)) {
            if (std::abs(best_freq - tracked_pitch_) > 20.0f) {
                tracked_pitch_ = best_freq;
                update_fir_filter();
            }
            return std::min(0.95f, max_mag * 25.0f);
        }
        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;
        std::string text(reinterpret_cast<const char*>(payload), len);
        
        tx_samples_.clear();
        tx_playback_pos_ = 0;

        float pitch = tracked_pitch_;
        float wpm = 20.0f; // 20 WPM standard
        if (json_config) {
            std::string cfg(json_config);
            size_t p_pos = cfg.find("\"pitch\"");
            if (p_pos != std::string::npos) {
                float p = std::stof(cfg.substr(p_pos + 8));
                if (p >= 300.0f && p <= 2500.0f) pitch = p;
            }
            size_t w_pos = cfg.find("\"wpm\"");
            if (w_pos != std::string::npos) {
                float w = std::stof(cfg.substr(w_pos + 6));
                if (w >= 5.0f && w <= 45.0f) wpm = w;
            }
        }

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

    bool is_rx_active() const override {
        return current_state_ || !current_symbol_.empty();
    }

    size_t get_max_burst_samples() const override {
        return static_cast<size_t>(sample_rate_ * 60);
    }

private:
    void handle_state_transition(bool was_on, size_t duration) {
        if (was_on) {
            // Reject short noise clicks < 18ms
            if (duration >= static_cast<size_t>(sample_rate_ * 0.018f)) {
                if (duration < static_cast<size_t>(1.80f * adaptive_dit_samples_)) {
                    current_symbol_ += '.';
                    if (duration < static_cast<size_t>(0.60f * adaptive_dit_samples_)) {
                        adaptive_dit_samples_ = duration; // rapid lock to fast sender
                    } else {
                        adaptive_dit_samples_ = static_cast<size_t>(0.80f * adaptive_dit_samples_ + 0.20f * duration);
                    }
                } else {
                    current_symbol_ += '-';
                    adaptive_dit_samples_ = static_cast<size_t>(0.80f * adaptive_dit_samples_ + 0.20f * (duration / 3.0f));
                }
                // Clamp adaptive dit length between 50 WPM (24ms) and 6 WPM (200ms)
                size_t min_dit = static_cast<size_t>(sample_rate_ * 0.024f);
                size_t max_dit = static_cast<size_t>(sample_rate_ * 0.200f);
                adaptive_dit_samples_ = std::clamp(adaptive_dit_samples_, min_dit, max_dit);
            }
        } else {
            // Silence ended: if any symbol remains un-flushed, decode it
            if (!current_symbol_.empty() && duration >= static_cast<size_t>(1.8f * adaptive_dit_samples_)) {
                char decoded = dsp::MorseTable::decode_morse(current_symbol_);
                current_symbol_.clear();
                if (decoded != '?' && callback_) {
                    has_emitted_char_ = true;
                    last_was_space_ = false;
                    NativeModemEvent ev{};
                    ev.event_type = static_cast<int32_t>(EventType::TextStream);
                    ev.protocol_id = get_id();
                    ev.snr_db = 15.0f;
                    ev.center_freq = static_cast<int32_t>(tracked_pitch_);
                    ev.payload = reinterpret_cast<const uint8_t*>(&decoded);
                    ev.payload_len = 1;
                    float cur_wpm = 1.2f / (static_cast<float>(adaptive_dit_samples_) / sample_rate_);
                    ev.metadata_int = static_cast<int32_t>(cur_wpm);
                    callback_(&ev);
                }
            }
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    float tracked_pitch_{700.0f};
    size_t adaptive_dit_samples_{2880}; // 20 WPM initial default

    dsp::FIRFilter fir_;
    float envelope_peak_{0.05f};
    float noise_floor_{0.005f};
    float smoothed_env_{0.0f};
    bool current_state_{false};
    size_t state_duration_{0};
    std::string current_symbol_;
    std::vector<float> analysis_window_;
    bool has_emitted_char_{false};
    bool last_was_space_{true};

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(CwMorseEngine, "cw_morse");
