#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/crc.h"
#include "dsp/goertzel.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstring>

class AprsBell202Engine : public IModemEngine {
public:
    const char* get_id() const override { return "aprs_packet"; }
    const char* get_display_name() const override { return "APRS / AX.25 (Bell 202)"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        samples_per_bit_ = sample_rate_ / 1200; // 40 samples at 48 kHz
        reset();
    }

    void reset() override {
        symbol_window_.clear();
        consecutive_ones_ = 0;
        rx_bit_buffer_ = 0;
        rx_byte_ = 0;
        rx_bit_count_ = 0;
        rx_frame_bytes_.clear();
        in_frame_ = false;
        nrzi_last_bit_ = false;

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        for (size_t i = 0; i < count; ++i) {
            symbol_window_.push_back(samples[i]);
            if (symbol_window_.size() >= samples_per_bit_) {
                // Matched filter: energy at 1200 Hz vs 2200 Hz
                float e1200 = dsp::Goertzel::compute_energy(symbol_window_.data(), symbol_window_.size(), 1200.0f, static_cast<float>(sample_rate_));
                float e2200 = dsp::Goertzel::compute_energy(symbol_window_.data(), symbol_window_.size(), 2200.0f, static_cast<float>(sample_rate_));

                bool raw_bit = (e1200 >= e2200); // Mark = 1200 Hz, Space = 2200 Hz
                handle_demodulated_raw_bit(raw_bit);
                symbol_window_.clear();
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 256) return 0.0f;
        float mark = dsp::Goertzel::compute_magnitude(samples, count, 1200.0f, static_cast<float>(sample_rate_));
        float space = dsp::Goertzel::compute_magnitude(samples, count, 2200.0f, static_cast<float>(sample_rate_));
        float mag_hell = dsp::Goertzel::compute_magnitude(samples, count, 980.0f, static_cast<float>(sample_rate_));
        float mag_eas_space = dsp::Goertzel::compute_magnitude(samples, count, 1562.5f, static_cast<float>(sample_rate_));
        float mag_sstv = dsp::Goertzel::compute_magnitude(samples, count, 1900.0f, static_cast<float>(sample_rate_));

        // Reject if adjacent non-Bell-202 signals are active
        if (mag_hell > mark * 0.75f || mag_eas_space > 0.03f || mag_sstv > 0.035f) {
            return 0.0f;
        }

        float total = mark + space;
        if (total > 0.035f && total > 2.0f * (mag_hell + 0.008f)) {
            return std::min(1.0f, total * 15.0f);
        }
        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;

        std::string src_call = "N0CALL-1";
        std::string dst_call = "APRS  -0";
        if (json_config) {
            std::string cfg(json_config);
            auto pos = cfg.find("\"callsign\":\"");
            if (pos != std::string::npos) {
                size_t start = pos + 12;
                size_t end = cfg.find("\"", start);
                if (end != std::string::npos && end > start) {
                    src_call = cfg.substr(start, end - start);
                }
            }
        }

        auto encode_callsign = [](const std::string& call, uint8_t ssid_last) {
            std::vector<uint8_t> out(7, ' ' << 1);
            std::string base = call;
            int ssid = 0;
            auto dash = call.find('-');
            if (dash != std::string::npos) {
                base = call.substr(0, dash);
                try { ssid = std::stoi(call.substr(dash + 1)); } catch (...) {}
            }
            for (size_t i = 0; i < std::min(base.size(), size_t(6)); ++i) {
                out[i] = static_cast<uint8_t>(base[i]) << 1;
            }
            out[6] = static_cast<uint8_t>(0x60 | ((ssid & 0x0F) << 1) | ssid_last);
            return out;
        };

        std::vector<uint8_t> frame_data;
        auto dst_bytes = encode_callsign(dst_call, 0x00);
        auto src_bytes = encode_callsign(src_call, 0x01); // Last address byte has bit 0 set

        frame_data.insert(frame_data.end(), dst_bytes.begin(), dst_bytes.end());
        frame_data.insert(frame_data.end(), src_bytes.begin(), src_bytes.end());
        frame_data.push_back(0x03); // Control: UI frame
        frame_data.push_back(0xF0); // PID: No layer 3
        frame_data.insert(frame_data.end(), payload, payload + len);

        // Compute CRC-16-CCITT over frame data
        uint16_t crc = dsp::CRC::crc16_ccitt(frame_data.data(), frame_data.size());
        frame_data.push_back(static_cast<uint8_t>(crc & 0xFF));
        frame_data.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

        // Bit stream generation with bit-stuffing and NRZI
        std::vector<bool> bits;
        // Preamble flags (20 flags)
        for (int i = 0; i < 20; ++i) {
            for (int b = 0; b < 8; ++b) {
                bits.push_back((0x7E >> b) & 1);
            }
        }

        // Body bits with stuffing
        int consecutive_ones = 0;
        for (uint8_t byte : frame_data) {
            for (int b = 0; b < 8; ++b) {
                bool bit = (byte >> b) & 1;
                bits.push_back(bit);
                if (bit) {
                    consecutive_ones++;
                    if (consecutive_ones == 5) {
                        bits.push_back(false); // Bit stuff zero
                        consecutive_ones = 0;
                    }
                } else {
                    consecutive_ones = 0;
                }
            }
        }

        // Postamble flags (5 flags)
        for (int i = 0; i < 5; ++i) {
            for (int b = 0; b < 8; ++b) {
                bits.push_back((0x7E >> b) & 1);
            }
        }

        // NRZI Encoding: 0 -> transition, 1 -> no transition
        std::vector<bool> nrzi_bits;
        bool nrzi_state = false;
        for (bool bit : bits) {
            if (!bit) {
                nrzi_state = !nrzi_state; // Toggle on 0
            }
            nrzi_bits.push_back(nrzi_state);
        }

        // Synthesize AFSK audio: true -> 1200 Hz (Mark), false -> 2200 Hz (Space)
        tx_samples_.clear();
        tx_playback_pos_ = 0;
        const float two_pi = 6.28318530717958647692f;
        float phase = 0.0f;

        for (bool nrzi_level : nrzi_bits) {
            float freq = nrzi_level ? 1200.0f : 2200.0f;
            float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
            for (size_t s = 0; s < samples_per_bit_; ++s) {
                tx_samples_.push_back(0.5f * std::sin(phase));
                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
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
    void handle_demodulated_raw_bit(bool raw_bit) {
        // NRZI decoding: transition = 0, no transition = 1
        bool data_bit = (raw_bit == nrzi_last_bit_);
        nrzi_last_bit_ = raw_bit;

        // Check for HDLC flag (01111110 = 0x7E)
        rx_bit_buffer_ = ((rx_bit_buffer_ >> 1) | (data_bit ? 0x80 : 0x00)) & 0xFF;

        if (rx_bit_buffer_ == 0x7E) {
            // Flag detected
            if (in_frame_ && rx_frame_bytes_.size() >= 18) {
                // Verify CRC-16
                uint16_t computed_crc = dsp::CRC::crc16_ccitt(rx_frame_bytes_.data(), rx_frame_bytes_.size() - 2);
                uint16_t frame_crc = rx_frame_bytes_[rx_frame_bytes_.size() - 2] |
                                     (rx_frame_bytes_[rx_frame_bytes_.size() - 1] << 8);

                if (computed_crc == frame_crc && callback_) {
                    // Valid AX.25 frame! Parse payload (skip dst 7, src 7, control 1, pid 1)
                    size_t info_len = rx_frame_bytes_.size() - 2 - 16;
                    const uint8_t* info_ptr = rx_frame_bytes_.data() + 16;

                    NativeModemEvent ev{};
                    ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                    ev.protocol_id = get_id();
                    ev.snr_db = 18.0f;
                    ev.center_freq = 1700;
                    ev.payload = info_ptr;
                    ev.payload_len = info_len;
                    ev.metadata_int = 1200; // Baud rate
                    callback_(&ev);
                }
            }
            in_frame_ = true;
            rx_frame_bytes_.clear();
            rx_byte_ = 0;
            rx_bit_count_ = 0;
            consecutive_ones_ = 0;
            return;
        }

        if (!in_frame_) return;

        // Bit destuffing: if 5 consecutive ones followed by 0, drop the stuffed 0
        if (consecutive_ones_ == 5) {
            if (!data_bit) {
                consecutive_ones_ = 0;
                return; // Stuffed 0 dropped
            }
        }

        if (data_bit) {
            consecutive_ones_++;
        } else {
            consecutive_ones_ = 0;
        }

        // Accumulate byte (LSB first)
        rx_byte_ |= (data_bit ? (1 << rx_bit_count_) : 0);
        rx_bit_count_++;

        if (rx_bit_count_ == 8) {
            rx_frame_bytes_.push_back(rx_byte_);
            rx_byte_ = 0;
            rx_bit_count_ = 0;
            if (rx_frame_bytes_.size() > 512) {
                in_frame_ = false;
            }
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    size_t samples_per_bit_{40};

    std::vector<float> symbol_window_;
    bool nrzi_last_bit_{false};

    int consecutive_ones_{0};
    uint8_t rx_bit_buffer_{0};
    uint8_t rx_byte_{0};
    int rx_bit_count_{0};
    std::vector<uint8_t> rx_frame_bytes_;
    bool in_frame_{false};

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(AprsBell202Engine, "aprs_packet");
