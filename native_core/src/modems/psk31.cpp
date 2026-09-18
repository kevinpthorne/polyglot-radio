#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/costas_loop.h"
#include "dsp/varicode.h"
#include "dsp/goertzel.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

class Psk31Engine : public IModemEngine {
public:
    const char* get_id() const override { return "psk31"; }
    const char* get_display_name() const override { return "PSK31 (BPSK)"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        samples_per_symbol_ = sample_rate_ / 31.25f; // 1536 samples
        costas_ = std::make_unique<dsp::CostasLoop>(1000.0f, static_cast<float>(sample_rate_));
        reset();
    }

    void reset() override {
        if (costas_) costas_->reset(1000.0f);
        symbol_timer_ = 0.0f;
        prev_i_sign_ = 1.0f;
        bit_history_.clear();
        consecutive_zeros_ = 0;
        accumulated_code_.clear();

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        for (size_t i = 0; i < count; ++i) {
            float s = samples[i];
            float out_i = 0.0f, out_q = 0.0f;
            costas_->process(s, out_i, out_q);

            symbol_timer_ += 1.0f;
            if (symbol_timer_ >= samples_per_symbol_) {
                symbol_timer_ -= samples_per_symbol_;

                // Detect phase change
                // In BPSK PSK31: phase reversal (180 deg) = bit '0', no reversal = bit '1'
                float current_sign = (out_i >= 0.0f) ? 1.0f : -1.0f;
                bool bit = (current_sign == prev_i_sign_);
                prev_i_sign_ = current_sign;

                handle_demodulated_bit(bit);
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 512) return 0.0f;
        float mag = dsp::Goertzel::compute_magnitude(samples, count, 1000.0f, static_cast<float>(sample_rate_));
        if (mag > 0.02f) {
            return std::min(1.0f, mag * 18.0f);
        }
        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;

        tx_samples_.clear();
        tx_playback_pos_ = 0;

        float carrier_freq = 1000.0f;
        const float two_pi = 6.28318530717958647692f;
        float phase = 0.0f;
        float phase_inc = two_pi * carrier_freq / static_cast<float>(sample_rate_);

        // Varicode bit sequence: Preamble (continuous carrier / '1's for 500ms)
        std::vector<bool> tx_bits;
        for (int i = 0; i < 32; ++i) tx_bits.push_back(true); // Preamble 1s

        for (size_t i = 0; i < len; ++i) {
            const char* code = dsp::Varicode::get_code(payload[i]);
            for (size_t c = 0; code[c] != '\0'; ++c) {
                tx_bits.push_back(code[c] == '1');
            }
            // Varicode delimiter: '00'
            tx_bits.push_back(false);
            tx_bits.push_back(false);
        }

        // Postamble
        for (int i = 0; i < 16; ++i) tx_bits.push_back(true);

        // Modulate with raised-cosine envelope
        float current_phase_sign = 1.0f;
        size_t sps = static_cast<size_t>(samples_per_symbol_);

        for (bool bit : tx_bits) {
            float next_phase_sign = bit ? current_phase_sign : -current_phase_sign;

            for (size_t s = 0; s < sps; ++s) {
                // Raised cosine envelope transition
                float progress = static_cast<float>(s) / static_cast<float>(sps);
                float envelope = 0.5f * (1.0f - std::cos(3.14159265f * progress));
                float carrier_amp = current_phase_sign + (next_phase_sign - current_phase_sign) * envelope;

                float val = 0.4f * carrier_amp * std::sin(phase);
                tx_samples_.push_back(val);

                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
            }
            current_phase_sign = next_phase_sign;
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
    void handle_demodulated_bit(bool bit) {
        if (bit) {
            consecutive_zeros_ = 0;
            accumulated_code_ += '1';
        } else {
            consecutive_zeros_++;
            if (consecutive_zeros_ == 2) {
                // Varicode delimiter detected!
                if (accumulated_code_.size() > 1) {
                    // Strip the trailing '0' that was part of the delimiter
                    if (accumulated_code_.back() == '0') {
                        accumulated_code_.pop_back();
                    }
                    int decoded_char = dsp::Varicode::decode_code(accumulated_code_);
                    if (decoded_char >= 0 && callback_) {
                        uint8_t c = static_cast<uint8_t>(decoded_char);
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::TextStream);
                        ev.protocol_id = get_id();
                        ev.snr_db = 14.0f;
                        ev.center_freq = static_cast<int32_t>(costas_->get_current_freq());
                        ev.payload = &c;
                        ev.payload_len = 1;
                        ev.metadata_int = 31;
                        callback_(&ev);
                    }
                }
                accumulated_code_.clear();
                consecutive_zeros_ = 0;
            } else {
                accumulated_code_ += '0';
            }
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    float samples_per_symbol_{1536.0f};
    std::unique_ptr<dsp::CostasLoop> costas_;

    float symbol_timer_{0.0f};
    float prev_i_sign_{1.0f};
    std::vector<bool> bit_history_;
    int consecutive_zeros_{0};
    std::string accumulated_code_;

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(Psk31Engine, "psk31");
