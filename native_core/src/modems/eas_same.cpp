#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/goertzel.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstring>
#include <iostream>

class EasSameEngine : public IModemEngine {
public:
    const char* get_id() const override { return "eas_same"; }
    const char* get_display_name() const override { return "EAS / SAME Emergency Alert"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        // 520.83333 baud -> exactly 92.16 samples per bit at 48000 Hz
        samples_per_bit_ = static_cast<float>(sample_rate_) / 520.83333f;
        win_size_ = 92;

        const float two_pi = 6.28318530717958647692f;
        phase_m_inc_ = two_pi * 2083.3333f / static_cast<float>(sample_rate_);
        phase_s_inc_ = two_pi * 1562.5f / static_cast<float>(sample_rate_);
        reset();
    }

    void reset() override {
        phase_m_ = 0.0f;
        phase_s_ = 0.0f;
        dpll_phase_ = 0.0f;
        sampled_this_bit_ = false;
        prev_discrim_ = 0.0f;

        win_mi_.assign(win_size_, 0.0f);
        win_mq_.assign(win_size_, 0.0f);
        win_si_.assign(win_size_, 0.0f);
        win_sq_.assign(win_size_, 0.0f);
        w_idx_ = 0;
        sum_mi_ = 0.0f;
        sum_mq_ = 0.0f;
        sum_si_ = 0.0f;
        sum_sq_ = 0.0f;

        bit_shift_ = 0;
        preamble_count_ = 0;
        in_message_ = false;
        current_byte_ = 0;
        rx_bit_count_ = 0;
        message_buffer_.clear();

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void configure(const char* json_config) override {
        if (!json_config) return;
        std::string cfg(json_config);

        auto pos_org = cfg.find("\"originator\":\"");
        if (pos_org != std::string::npos) {
            auto end_org = cfg.find("\"", pos_org + 14);
            if (end_org != std::string::npos) {
                originator_ = cfg.substr(pos_org + 14, end_org - (pos_org + 14));
            }
        }

        auto pos_evt = cfg.find("\"event_code\":\"");
        if (pos_evt != std::string::npos) {
            auto end_evt = cfg.find("\"", pos_evt + 14);
            if (end_evt != std::string::npos) {
                event_code_ = cfg.substr(pos_evt + 14, end_evt - (pos_evt + 14));
            }
        }
    }

    bool is_rx_active() const override {
        return in_message_ || preamble_count_ > 0;
    }

    size_t get_max_burst_samples() const override {
        return static_cast<size_t>(sample_rate_ * 30);
    }

    void process_rx(const float* samples, size_t count) override {
        const float two_pi = 6.28318530717958647692f;

        for (size_t i = 0; i < count; ++i) {
            float s = samples[i];

            // 1. Heterodyne Mark and Space components
            float mi = s * std::cos(phase_m_);
            float mq = s * std::sin(phase_m_);
            float si = s * std::cos(phase_s_);
            float sq = s * std::sin(phase_s_);

            phase_m_ += phase_m_inc_;
            if (phase_m_ > two_pi) phase_m_ -= two_pi;
            phase_s_ += phase_s_inc_;
            if (phase_s_ > two_pi) phase_s_ -= two_pi;

            // 2. Running matched filter (exact 92-sample boxcar integrator)
            sum_mi_ += mi - win_mi_[w_idx_];
            sum_mq_ += mq - win_mq_[w_idx_];
            sum_si_ += si - win_si_[w_idx_];
            sum_sq_ += sq - win_sq_[w_idx_];

            win_mi_[w_idx_] = mi;
            win_mq_[w_idx_] = mq;
            win_si_[w_idx_] = si;
            win_sq_[w_idx_] = sq;
            w_idx_ = (w_idx_ + 1) % win_size_;

            float e_mark = sum_mi_ * sum_mi_ + sum_mq_ * sum_mq_;
            float e_space = sum_si_ * sum_si_ + sum_sq_ * sum_sq_;
            float discrim = e_mark - e_space;

            // 3. DPLL zero-crossing transition tracking
            if ((discrim >= 0.0f && prev_discrim_ < 0.0f) || (discrim < 0.0f && prev_discrim_ >= 0.0f)) {
                float phase_err = (dpll_phase_ > 0.5f) ? (dpll_phase_ - 1.0f) : dpll_phase_;
                dpll_phase_ -= 0.15f * phase_err;
            }
            prev_discrim_ = discrim;

            // Advance DPLL clock
            dpll_phase_ += (1.0f / samples_per_bit_);

            // 4. Sample bit at the eye center
            if (dpll_phase_ >= 0.5f && !sampled_this_bit_) {
                sampled_this_bit_ = true;
                bool bit = (discrim > 0.0f);
                handle_bit(bit);
            }

            if (dpll_phase_ >= 1.0f) {
                dpll_phase_ -= 1.0f;
                sampled_this_bit_ = false;
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 256) return 0.0f;
        float mark = dsp::Goertzel::compute_magnitude(samples, count, 2083.33f, static_cast<float>(sample_rate_));
        float space = dsp::Goertzel::compute_magnitude(samples, count, 1562.5f, static_cast<float>(sample_rate_));
        float mag_hell = dsp::Goertzel::compute_magnitude(samples, count, 980.0f, static_cast<float>(sample_rate_));
        float mag_sstv = dsp::Goertzel::compute_magnitude(samples, count, 1900.0f, static_cast<float>(sample_rate_));
        float mag_cw = dsp::Goertzel::compute_magnitude(samples, count, 700.0f, static_cast<float>(sample_rate_));
        float m450 = dsp::Goertzel::compute_magnitude(samples, count, 450.0f, static_cast<float>(sample_rate_));
        float m3200 = dsp::Goertzel::compute_magnitude(samples, count, 3200.0f, static_cast<float>(sample_rate_));

        float eas_peak = std::max(mark, space);

        // Reject if single tones (CW, Feld-Hell, SSTV) dominate EAS frequencies
        if (mag_cw > 1.25f * eas_peak && mag_cw > 0.025f) return 0.0f;
        if (mag_hell > 1.25f * eas_peak && mag_hell > 0.025f) return 0.0f;
        if (mag_sstv > 1.35f * eas_peak && mag_sstv > 0.035f) return 0.0f;

        float out_band = (mag_cw + mag_hell + m450 + m3200) * 0.25f;
        float eas_sum = mark + space;

        // EAS / SAME requires significant mark/space energy and high contrast over out-of-band noise
        if (eas_sum > 0.025f && eas_peak > 1.6f * (out_band + 0.003f)) {
            float contrast = eas_peak / (eas_peak + out_band + 0.005f);
            return std::clamp(contrast * 1.15f, 0.0f, 1.0f);
        }
        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;
        configure(json_config);

        std::string user_msg(reinterpret_cast<const char*>(payload), len);

        std::string callsign = "POLYGLOT";
        if (json_config) {
            std::string cfg(json_config);
            auto pos = cfg.find("\"callsign\":\"");
            if (pos != std::string::npos) {
                auto end_pos = cfg.find("\"", pos + 12);
                if (end_pos != std::string::npos) {
                    callsign = cfg.substr(pos + 12, end_pos - (pos + 12));
                }
            }
        }
        while (callsign.size() < 8) callsign += " ";
        if (callsign.size() > 8) callsign = callsign.substr(0, 8);

        std::string same_header = user_msg;
        if (same_header.find("ZCZC-") == std::string::npos) {
            same_header = "ZCZC-" + originator_ + "-" + event_code_ + "-000000+0015-0010000-" + callsign + "-" + user_msg + "-";
        }

        std::vector<uint8_t> stream;
        // Preamble: 16 bytes of 0xAB (10101011 LSB first)
        for (int i = 0; i < 16; ++i) stream.push_back(0xAB);
        for (char c : same_header) stream.push_back(static_cast<uint8_t>(c));

        // Generate AFSK audio
        tx_samples_.clear();
        tx_playback_pos_ = 0;
        const float two_pi = 6.28318530717958647692f;
        float phase = 0.0f;

        for (uint8_t byte : stream) {
            for (int b = 0; b < 8; ++b) {
                bool bit = (byte >> b) & 1;
                float freq = bit ? 2083.3333f : 1562.5f;
                float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                size_t num_s = static_cast<size_t>(samples_per_bit_);
                for (size_t s = 0; s < num_s; ++s) {
                    tx_samples_.push_back(0.45f * std::sin(phase));
                    phase += phase_inc;
                    if (phase > two_pi) phase -= two_pi;
                }
            }
        }

        // Attention Signal: 853 Hz + 960 Hz dual-tone burst (500 ms)
        size_t attn_samples = static_cast<size_t>(sample_rate_ * 0.500f);
        float p1 = 0.0f, p2 = 0.0f;
        float inc1 = two_pi * 853.0f / static_cast<float>(sample_rate_);
        float inc2 = two_pi * 960.0f / static_cast<float>(sample_rate_);
        for (size_t s = 0; s < attn_samples; ++s) {
            float val = 0.25f * (std::sin(p1) + std::sin(p2));
            tx_samples_.push_back(val);
            p1 += inc1; if (p1 > two_pi) p1 -= two_pi;
            p2 += inc2; if (p2 > two_pi) p2 -= two_pi;
        }

        // Standard SAME End of Message (EOM) burst: 16 bytes 0xAB + NNNN-
        std::vector<uint8_t> eom_stream;
        for (int i = 0; i < 16; ++i) eom_stream.push_back(0xAB);
        std::string eom_str = "NNNN-";
        for (char c : eom_str) eom_stream.push_back(static_cast<uint8_t>(c));

        for (uint8_t byte : eom_stream) {
            for (int b = 0; b < 8; ++b) {
                bool bit = (byte >> b) & 1;
                float freq = bit ? 2083.3333f : 1562.5f;
                float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                size_t num_s = static_cast<size_t>(samples_per_bit_);
                for (size_t s = 0; s < num_s; ++s) {
                    tx_samples_.push_back(0.45f * std::sin(phase));
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

    void flush() override {
        if (callback_ && message_buffer_.size() >= 8) {
            NativeModemEvent ev{};
            ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
            ev.protocol_id = get_id();
            ev.snr_db = 20.0f;
            ev.center_freq = 1823;
            ev.payload = reinterpret_cast<const uint8_t*>(message_buffer_.data());
            ev.payload_len = static_cast<int32_t>(message_buffer_.size());
            ev.metadata_int = 520;
            callback_(&ev);
        }
        in_message_ = false;
        preamble_count_ = 0;
        bit_shift_ = 0;
        current_byte_ = 0;
        rx_bit_count_ = 0;
        message_buffer_.clear();
    }

private:
    void handle_bit(bool bit) {
        // Slide bit into 32-bit shift register (LSB first: newest bit enters MSB)
        bit_shift_ = (bit_shift_ >> 1) | (bit ? 0x80000000U : 0x00000000U);

        if (!in_message_) {
            // Check for exact 32-bit "ZCZC" pattern:
            // Byte 0: 'Z' (0x5A)
            // Byte 1: 'C' (0x43)
            // Byte 2: 'Z' (0x5A)
            // Byte 3: 'C' (0x43)
            const uint32_t kZczcPattern = (0x43 << 24) | (0x5A << 16) | (0x43 << 8) | 0x5A;
            if (bit_shift_ == kZczcPattern) {
                in_message_ = true;
                message_buffer_ = "ZCZC";
                current_byte_ = 0;
                rx_bit_count_ = 0;
                return;
            }

            // Also check for standard 0xAB preamble sync
            if ((bit_shift_ & 0xFF) == 0xAB) {
                preamble_count_++;
            }
        } else {
            current_byte_ = (current_byte_ >> 1) | (bit ? 0x80 : 0x00);
            rx_bit_count_++;

            if (rx_bit_count_ == 8) {
                uint8_t b = current_byte_;
                current_byte_ = 0;
                rx_bit_count_ = 0;

                char c = static_cast<char>(b);
                if (b >= 0x20 && b <= 0x7E) {
                    message_buffer_ += c;
                } else if (b == 0x0A || b == 0x0D) {
                    if (!message_buffer_.empty() && message_buffer_.back() != '\n') {
                        message_buffer_ += '\n';
                    }
                }

                // Check for completion of EAS SAME message:
                int hyphens = 0;
                for (char ch : message_buffer_) {
                    if (ch == '-') hyphens++;
                }

                bool has_eom = (message_buffer_.find("NNNN") != std::string::npos);
                bool has_full_header = (hyphens >= 5 && message_buffer_.size() >= 28 && message_buffer_.back() == '-');

                if (has_eom || has_full_header) {
                    flush();
                }
            }
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    float samples_per_bit_{92.16f};
    size_t win_size_{92};

    float phase_m_inc_{0.272707f};
    float phase_s_inc_{0.204530f};
    float phase_m_{0.0f};
    float phase_s_{0.0f};

    float dpll_phase_{0.0f};
    bool sampled_this_bit_{false};
    float prev_discrim_{0.0f};

    std::vector<float> win_mi_;
    std::vector<float> win_mq_;
    std::vector<float> win_si_;
    std::vector<float> win_sq_;
    size_t w_idx_{0};
    float sum_mi_{0.0f};
    float sum_mq_{0.0f};
    float sum_si_{0.0f};
    float sum_sq_{0.0f};

    uint32_t bit_shift_{0};
    int preamble_count_{0};
    bool in_message_{false};
    uint8_t current_byte_{0};
    int rx_bit_count_{0};
    std::string message_buffer_;

    std::string originator_{"EAS"};
    std::string event_code_{"RWT"};

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(EasSameEngine, "eas_same");
