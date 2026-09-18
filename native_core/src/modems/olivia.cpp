#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/goertzel.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

class OliviaEngine : public IModemEngine {
public:
    const char* get_id() const override { return "olivia_mfsk"; }
    const char* get_display_name() const override { return "Olivia MFSK"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        tones_ = 16;
        center_freq_ = 1000.0f;
        tone_spacing_ = 31.25f;
        samples_per_symbol_ = static_cast<size_t>(sample_rate_ / 31.25f); // 1536 samples
        reset();
    }

    void reset() override {
        sample_counter_ = 0;
        symbol_buffer_.clear();

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        for (size_t i = 0; i < count; ++i) {
            symbol_buffer_.push_back(samples[i]);
            if (symbol_buffer_.size() >= samples_per_symbol_) {
                // Compute energy across all 16 orthogonal tone frequencies
                int best_tone = -1;
                float max_energy = 0.0f;
                float sum_energy = 0.0f;

                for (int t = 0; t < tones_; ++t) {
                    float freq = center_freq_ - 250.0f + (t + 0.5f) * tone_spacing_;
                    float e = dsp::Goertzel::compute_energy(symbol_buffer_.data(), symbol_buffer_.size(), freq, static_cast<float>(sample_rate_));
                    sum_energy += e;
                    if (e > max_energy) {
                        max_energy = e;
                        best_tone = t;
                    }
                }

                // Check Walsh peak threshold (> 3.5x average noise floor)
                float avg_noise = (sum_energy - max_energy) / (tones_ - 1 + 1e-6f);
                if (best_tone >= 0 && max_energy > 3.5f * avg_noise && max_energy > 1e-4f) {
                    // Map tone index to 4-bit nibble
                    char c = map_tone_to_char(best_tone);
                    if (c != '\0' && callback_) {
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::TextStream);
                        ev.protocol_id = get_id();
                        ev.snr_db = 15.0f;
                        ev.center_freq = static_cast<int32_t>(center_freq_);
                        ev.payload = reinterpret_cast<const uint8_t*>(&c);
                        ev.payload_len = 1;
                        ev.metadata_int = 31;
                        callback_(&ev);
                    }
                }
                symbol_buffer_.clear();
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 512) return 0.0f;
        float mag = dsp::Goertzel::compute_magnitude(samples, count, center_freq_, static_cast<float>(sample_rate_));
        return (mag > 0.03f) ? std::min(1.0f, mag * 16.0f) : 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;

        tx_samples_.clear();
        tx_playback_pos_ = 0;

        const float two_pi = 6.28318530717958647692f;
        float phase = 0.0f;

        // Preamble tones
        for (int p = 0; p < 8; ++p) {
            float freq = center_freq_ - 250.0f + ((p % tones_) + 0.5f) * tone_spacing_;
            float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
            for (size_t s = 0; s < samples_per_symbol_; ++s) {
                tx_samples_.push_back(0.4f * std::sin(phase));
                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
            }
        }

        // Payload tones
        for (size_t i = 0; i < len; ++i) {
            uint8_t byte = payload[i];
            int t1 = byte & 0x0F;
            int t2 = (byte >> 4) & 0x0F;

            int tones_to_send[2] = { t1, t2 };
            for (int t : tones_to_send) {
                float freq = center_freq_ - 250.0f + (t + 0.5f) * tone_spacing_;
                float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                for (size_t s = 0; s < samples_per_symbol_; ++s) {
                    tx_samples_.push_back(0.4f * std::sin(phase));
                    phase += phase_inc;
                    if (phase > two_pi) phase -= two_pi;
                }
            }
        }

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
    char map_tone_to_char(int tone) {
        // Nibble accumulator
        static int nibble_buf = -1;
        if (nibble_buf < 0) {
            nibble_buf = tone & 0x0F;
            return '\0';
        } else {
            uint8_t ch = static_cast<uint8_t>(nibble_buf | ((tone & 0x0F) << 4));
            nibble_buf = -1;
            if (ch >= 32 && ch <= 126) return static_cast<char>(ch);
            if (ch == '\n' || ch == '\r') return ' ';
            return '?';
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    int tones_{16};
    float center_freq_{1000.0f};
    float tone_spacing_{31.25f};
    size_t samples_per_symbol_{1536};

    size_t sample_counter_{0};
    std::vector<float> symbol_buffer_;

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(OliviaEngine, "olivia_mfsk");
