#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/goertzel.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstring>

class EasSameEngine : public IModemEngine {
public:
    const char* get_id() const override { return "eas_same"; }
    const char* get_display_name() const override { return "EAS / SAME Emergency Alert"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        samples_per_bit_ = static_cast<float>(sample_rate_) / 520.8333f;
        reset();
    }

    void reset() override {
        phase_accum_ = 0.0f;
        rx_byte_ = 0;
        rx_bit_count_ = 0;
        preamble_count_ = 0;
        in_message_ = false;
        message_buffer_.clear();

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        // Evaluate energy at Mark (2083.3 Hz) and Space (1562.5 Hz)
        const size_t window_size = static_cast<size_t>(samples_per_bit_);
        if (window_size == 0) return;

        for (size_t i = 0; i < count; ++i) {
            sliding_window_.push_back(samples[i]);
            if (sliding_window_.size() >= window_size) {
                float mark = dsp::Goertzel::compute_energy(sliding_window_.data(), sliding_window_.size(), 2083.33f, static_cast<float>(sample_rate_));
                float space = dsp::Goertzel::compute_energy(sliding_window_.data(), sliding_window_.size(), 1562.5f, static_cast<float>(sample_rate_));

                bool bit = (mark > space);
                handle_bit(bit);
                sliding_window_.clear();
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 256) return 0.0f;
        float mark = dsp::Goertzel::compute_magnitude(samples, count, 2083.33f, static_cast<float>(sample_rate_));
        float space = dsp::Goertzel::compute_magnitude(samples, count, 1562.5f, static_cast<float>(sample_rate_));
        float sum = mark + space;
        if (sum > 0.04f && (mark > space * 2.0f || space > mark * 2.0f)) {
            return std::min(1.0f, sum * 15.0f);
        }
        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;
        std::string user_msg(reinterpret_cast<const char*>(payload), len);

        // Standard SAME format if plain text is passed:
        // Preamble (16 bytes of 0xAB) + ZCZC-WXR-TOR-012345+0030-2611200-WXL40- + 1s pause + repeated 3 times
        std::string same_header = user_msg;
        if (same_header.find("ZCZC-") == std::string::npos) {
            // Wrap in standard EAS header
            same_header = "ZCZC-EAS-RWT-000000+0015-0010000-POLYGLOT-" + user_msg + "-";
        }

        std::vector<uint8_t> stream;
        // Preamble: 16 bytes of 0xAB
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
                float freq = bit ? 2083.33f : 1562.5f;
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

private:
    void handle_bit(bool bit) {
        rx_byte_ |= (bit ? (1 << rx_bit_count_) : 0);
        rx_bit_count_++;

        if (rx_bit_count_ == 8) {
            uint8_t b = rx_byte_;
            rx_byte_ = 0;
            rx_bit_count_ = 0;

            if (b == 0xAB) {
                preamble_count_++;
                if (preamble_count_ >= 4) {
                    in_message_ = true;
                    message_buffer_.clear();
                }
                return;
            }

            if (in_message_) {
                char c = static_cast<char>(b);
                message_buffer_ += c;

                // Check for EAS message terminator (ends with dash or NNNN)
                if (message_buffer_.find("NNNN") != std::string::npos ||
                    (message_buffer_.size() > 20 && message_buffer_.back() == '-')) {
                    if (callback_ && message_buffer_.size() >= 10) {
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                        ev.protocol_id = get_id();
                        ev.snr_db = 20.0f;
                        ev.center_freq = 1820;
                        ev.payload = reinterpret_cast<const uint8_t*>(message_buffer_.data());
                        ev.payload_len = message_buffer_.size();
                        ev.metadata_int = 520;
                        callback_(&ev);
                    }
                    in_message_ = false;
                    preamble_count_ = 0;
                    message_buffer_.clear();
                }
            }
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    float samples_per_bit_{92.16f};
    float phase_accum_{0.0f};

    std::vector<float> sliding_window_;
    uint8_t rx_byte_{0};
    int rx_bit_count_{0};
    int preamble_count_{0};
    bool in_message_{false};
    std::string message_buffer_;

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(EasSameEngine, "eas_same");
