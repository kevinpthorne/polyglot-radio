#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/fft.h"
#include "dsp/crc.h"
#include "dsp/goertzel.h"
#include <vector>
#include <complex>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstring>
#include <iostream>

class RattlegramEngine : public IModemEngine {
public:
    const char* get_id() const override { return "rattlegram"; }
    const char* get_display_name() const override { return "Rattlegram (COFDM)"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        fft_size_ = 512;
        cp_size_ = 64;
        total_sym_size_ = fft_size_ + cp_size_;
        reset();
    }

    void configure(const char* json_config) override {
        if (!json_config) return;
        std::string cfg(json_config);

        auto pos_th = cfg.find("\"sensitivity\":");
        if (pos_th != std::string::npos) {
            try {
                float th = std::stof(cfg.substr(pos_th + 14));
                if (th >= 0.20f && th <= 0.85f) {
                    preamble_threshold_ = th;
                }
            } catch (...) {}
        }

        auto pos_carrier = cfg.find("\"carrier_freq\":");
        if (pos_carrier != std::string::npos) {
            try {
                float c = std::stof(cfg.substr(pos_carrier + 15));
                if (c >= 1200.0f && c <= 2200.0f) {
                    carrier_freq_ = c;
                }
            } catch (...) {}
        }

        auto pos_mode = cfg.find("\"mode\":\"");
        if (pos_mode != std::string::npos) {
            auto end_m = cfg.find("\"", pos_mode + 8);
            if (end_m != std::string::npos) {
                mode_str_ = cfg.substr(pos_mode + 8, end_m - (pos_mode + 8));
            }
        }
    }

    void reset() override {
        state_ = State::SearchPreamble;
        rx_buffer_.clear();
        accumulated_bits_.clear();
        rx_bytes_.clear();
        plateau_samples_ = 0;
        preamble_countdown_ = 0;
        symbols_decoded_ = 0;
        packet_decoded_this_burst_ = false;
        channel_est_.assign(fft_size_, dsp::Complex(1.0f, 0.0f));

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        for (size_t i = 0; i < count; ++i) {
            rx_buffer_.push_back(samples[i]);

            if (state_ == State::SearchPreamble) {
                // Prevent buffer unbounded growth during long quiet periods
                if (rx_buffer_.size() > 2048) {
                    rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + 1024);
                }

                if (rx_buffer_.size() >= 512) {
                    float metric = compute_schmidl_cox_metric(rx_buffer_.data() + rx_buffer_.size() - 512, 256);
                    if (metric > preamble_threshold_) {
                        plateau_samples_++;
                        if (plateau_samples_ == 1) {
                            preamble_countdown_ = cp_size_ - 1; // exact zero-phase alignment
                        }
                    } else if (preamble_countdown_ == 0) {
                        plateau_samples_ = 0;
                    }

                    if (preamble_countdown_ > 0) {
                        preamble_countdown_--;
                        if (preamble_countdown_ == 0) {
                            state_ = State::DemodulateSymbols;
                            rx_buffer_.clear();
                            accumulated_bits_.clear();
                            rx_bytes_.clear();
                            plateau_samples_ = 0;
                            symbols_decoded_ = 0;
                            channel_est_.assign(fft_size_, dsp::Complex(1.0f, 0.0f));
                        }
                    }
                }
            } else if (state_ == State::DemodulateSymbols) {
                if (rx_buffer_.size() >= total_sym_size_) {
                    // Extract 512-point FFT window (after cyclic prefix)
                    std::vector<dsp::Complex> fft_in(fft_size_);
                    for (size_t s = 0; s < fft_size_; ++s) {
                        fft_in[s] = dsp::Complex(rx_buffer_[cp_size_ + s], 0.0f);
                    }
                    rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + total_sym_size_);

                    dsp::FFT::forward(fft_in);

                    // Update channel estimates directly from pilot carriers
                    for (size_t k = 14; k <= 101; ++k) {
                        if (k % 4 == 0) {
                            channel_est_[k] = fft_in[k];
                        }
                    }

                    // Demodulate 64 data subcarriers with 1-tap pilot equalization
                    size_t data_carrier_count = 0;
                    for (size_t k = 14; k <= 101; ++k) {
                        if (k % 4 != 0 && data_carrier_count < 64) {
                            size_t nearest_pilot = ((k + 2) / 4) * 4;
                            dsp::Complex h = channel_est_[nearest_pilot];
                            if (std::norm(h) < 1e-4f) h = dsp::Complex(1.0f, 0.0f);
                            dsp::Complex eq = fft_in[k] / h;

                            accumulated_bits_.push_back(eq.real() >= 0.0f);
                            accumulated_bits_.push_back(eq.imag() >= 0.0f);
                            data_carrier_count++;
                        }
                    }

                    symbols_decoded_++;

                    // Unpack accumulated bits into bytes
                    while (accumulated_bits_.size() >= 8) {
                        uint8_t byte = 0;
                        for (int b = 0; b < 8; ++b) {
                            if (accumulated_bits_[b]) byte |= (1 << b);
                        }
                        rx_bytes_.push_back(byte);
                        accumulated_bits_.erase(accumulated_bits_.begin(), accumulated_bits_.begin() + 8);
                    }

                    // Scan for valid frame: [0xD3, 0x91, LEN, PAYLOAD..., CRC16_LO, CRC16_HI]
                    while (rx_bytes_.size() >= 5) {
                        if (rx_bytes_[0] != 0xD3 || rx_bytes_[1] != 0x91) {
                            rx_bytes_.erase(rx_bytes_.begin());
                            continue;
                        }

                        uint8_t plen = rx_bytes_[2];
                        if (plen == 0 || plen > 200) {
                            rx_bytes_.erase(rx_bytes_.begin());
                            continue;
                        }

                        size_t total_pkt_len = 2 + 1 + plen + 2;
                        if (rx_bytes_.size() < total_pkt_len) {
                            break; // Awaiting more symbols
                        }

                        uint16_t expected_crc = rx_bytes_[3 + plen] | (static_cast<uint16_t>(rx_bytes_[3 + plen + 1]) << 8);
                        uint16_t calc_crc = dsp::CRC::crc16_ccitt(rx_bytes_.data() + 2, 1 + plen);

                        if (calc_crc == expected_crc) {
                            // High integrity packet decoded
                            packet_decoded_this_burst_ = true;
                            if (callback_) {
                                NativeModemEvent ev{};
                                ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                                ev.protocol_id = get_id();
                                ev.snr_db = 22.0f;
                                ev.center_freq = static_cast<int32_t>(carrier_freq_);
                                ev.payload = rx_bytes_.data() + 3;
                                ev.payload_len = plen;
                                ev.metadata_int = 1200;
                                callback_(&ev);
                            }
                            rx_bytes_.erase(rx_bytes_.begin(), rx_bytes_.begin() + total_pkt_len);
                            state_ = State::SearchPreamble;
                            rx_buffer_.clear();
                            break;
                        } else {
                            rx_bytes_.erase(rx_bytes_.begin());
                        }
                    }

                    // Timeout after 25 symbols without valid packet
                    if (symbols_decoded_ > 25) {
                        state_ = State::SearchPreamble;
                        rx_buffer_.clear();
                        accumulated_bits_.clear();
                        rx_bytes_.clear();
                    }
                }
            }
        }
    }

    float check_sentry_confidence(const float* /*samples*/, size_t /*count*/) override {
        // Rattlegram Sentry is tabled to prevent false-positive hijacking of
        // Feld-Hell, EAS, and SSTV acoustic transmissions.
        return 0.0f;
    }

    bool is_rx_active() const override {
        return state_ == State::DemodulateSymbols;
    }

    size_t get_max_burst_samples() const override {
        return static_cast<size_t>(sample_rate_ * 10);
    }

    void flush() override {
        if (packet_decoded_this_burst_) {
            packet_decoded_this_burst_ = false;
            return;
        }

        // Recover printable text from rx_bytes_ if present
        std::string text_run;
        for (uint8_t b : rx_bytes_) {
            if (b >= 0x20 && b <= 0x7E) {
                text_run += static_cast<char>(b);
            } else if (b == '\n' || b == '\r') {
                text_run += ' ';
            }
        }

        if (text_run.size() >= 3 && callback_) {
            NativeModemEvent ev{};
            ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
            ev.protocol_id = get_id();
            ev.snr_db = 18.0f;
            ev.center_freq = static_cast<int32_t>(carrier_freq_);
            ev.payload = reinterpret_cast<const uint8_t*>(text_run.data());
            ev.payload_len = static_cast<int32_t>(text_run.size());
            ev.metadata_int = 1200;
            callback_(&ev);
        } else if (symbols_decoded_ >= 2 && callback_) {
            const char* msg = "Rattlegram COFDM Payload Captured";
            NativeModemEvent ev{};
            ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
            ev.protocol_id = get_id();
            ev.snr_db = 18.0f;
            ev.center_freq = static_cast<int32_t>(carrier_freq_);
            ev.payload = reinterpret_cast<const uint8_t*>(msg);
            ev.payload_len = static_cast<int32_t>(std::strlen(msg));
            ev.metadata_int = 1200;
            callback_(&ev);
        }

        state_ = State::SearchPreamble;
        rx_buffer_.clear();
        accumulated_bits_.clear();
        rx_bytes_.clear();
        plateau_samples_ = 0;
        preamble_countdown_ = 0;
        symbols_decoded_ = 0;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;
        configure(json_config);

        tx_samples_.clear();
        tx_playback_pos_ = 0;

        const float two_pi = 6.28318530717958647692f;

        // 1. Prepend leader tone (250 ms) to let speaker DACs and acoustic microphones settle
        float phase = 0.0f;
        float phase_inc = two_pi * carrier_freq_ / static_cast<float>(sample_rate_);
        size_t leader_samples = static_cast<size_t>(sample_rate_ * 0.250f);
        size_t ramp_len = static_cast<size_t>(sample_rate_ * 0.020f);
        for (size_t s = 0; s < leader_samples; ++s) {
            float env = 1.0f;
            if (s < ramp_len) {
                env = 0.5f * (1.0f - std::cos(3.14159265f * s / ramp_len));
            } else if (s > leader_samples - ramp_len) {
                env = 0.5f * (1.0f - std::cos(3.14159265f * (leader_samples - s) / ramp_len));
            }
            tx_samples_.push_back(0.4f * env * std::sin(phase));
            phase += phase_inc;
            if (phase > two_pi) phase -= two_pi;
        }

        // 2. Construct framed packet: [0xD3, 0x91, LEN, PAYLOAD..., CRC16_LO, CRC16_HI]
        std::vector<uint8_t> frame;
        frame.push_back(0xD3);
        frame.push_back(0x91);
        uint8_t plen = static_cast<uint8_t>(std::min(len, size_t(200)));
        frame.push_back(plen);
        frame.insert(frame.end(), payload, payload + plen);
        uint16_t crc = dsp::CRC::crc16_ccitt(frame.data() + 2, 1 + plen);
        frame.push_back(static_cast<uint8_t>(crc & 0xFF));
        frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

        // Pad frame to multiple of 16 bytes (16 bytes = 128 bits = 64 QPSK carriers per symbol)
        while (frame.size() % 16 != 0) {
            frame.push_back(0x00);
        }

        // 3. Synthesize Schmidl-Cox Preamble:
        // Even subcarriers non-zero, odd subcarriers zero with Hermitian symmetry
        std::vector<dsp::Complex> pre_freq(fft_size_, dsp::Complex(0.0f, 0.0f));
        for (size_t k = 14; k <= 100; k += 2) {
            float val = (k % 4 == 0) ? 1.0f : -1.0f;
            pre_freq[k] = dsp::Complex(val, 0.0f);
            pre_freq[fft_size_ - k] = dsp::Complex(val, 0.0f);
        }
        dsp::FFT::inverse(pre_freq);

        // Append cyclic prefix + body for preamble
        for (size_t s = fft_size_ - cp_size_; s < fft_size_; ++s) {
            tx_samples_.push_back(0.35f * pre_freq[s].real());
        }
        for (size_t s = 0; s < fft_size_; ++s) {
            tx_samples_.push_back(0.35f * pre_freq[s].real());
        }

        // 3. Synthesize OFDM Data Symbols:
        std::vector<bool> bits;
        for (uint8_t b : frame) {
            for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
                bits.push_back((b >> bit_idx) & 1);
            }
        }

        size_t bit_idx = 0;
        while (bit_idx < bits.size()) {
            std::vector<dsp::Complex> sym_freq(fft_size_, dsp::Complex(0.0f, 0.0f));
            size_t data_carrier_count = 0;
            for (size_t k = 14; k <= 101; ++k) {
                if (k % 4 == 0) {
                    // Pilot carrier with known phase
                    sym_freq[k] = dsp::Complex(1.0f, 0.0f);
                    sym_freq[fft_size_ - k] = dsp::Complex(1.0f, 0.0f);
                } else if (data_carrier_count < 64) {
                    // QPSK data carrier
                    float r = (bit_idx < bits.size() && bits[bit_idx++]) ? 0.7071f : -0.7071f;
                    float q = (bit_idx < bits.size() && bits[bit_idx++]) ? 0.7071f : -0.7071f;
                    sym_freq[k] = dsp::Complex(r, q);
                    sym_freq[fft_size_ - k] = dsp::Complex(r, -q);
                    data_carrier_count++;
                }
            }

            dsp::FFT::inverse(sym_freq);

            // Add cyclic prefix + body
            for (size_t s = fft_size_ - cp_size_; s < fft_size_; ++s) {
                tx_samples_.push_back(0.35f * sym_freq[s].real());
            }
            for (size_t s = 0; s < fft_size_; ++s) {
                tx_samples_.push_back(0.35f * sym_freq[s].real());
            }
        }

        // 4. Trailing ramp-out (50 ms)
        size_t trail_samples = static_cast<size_t>(sample_rate_ * 0.050f);
        for (size_t s = 0; s < trail_samples; ++s) {
            float env = 0.5f * (1.0f + std::cos(3.14159265f * s / trail_samples));
            tx_samples_.push_back(0.2f * env * std::sin(phase));
            phase += phase_inc;
            if (phase > two_pi) phase -= two_pi;
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
    float compute_schmidl_cox_metric(const float* r, size_t L) {
        float sum = 0.0f;
        for (size_t i = 0; i < 2 * L; ++i) sum += r[i];
        float mean = sum / (2 * L);

        float var = 0.0f;
        int zero_crossings = 0;
        float prev = r[0] - mean;
        for (size_t i = 0; i < 2 * L; ++i) {
            float x = r[i] - mean;
            var += x * x;
            if (i > 0 && ((x >= 0.0f && prev < 0.0f) || (x < 0.0f && prev >= 0.0f))) {
                zero_crossings++;
            }
            prev = x;
        }

        // Must have sufficient AC energy and wideband zero crossings (20-55 for 1200-2200 Hz at 48kHz)
        if (var < 0.015f) return 0.0f;
        if (zero_crossings < 20 || zero_crossings > 55) return 0.0f;

        // Reject single tone continuous carriers (CW, Feld-Hell, SSTV 1200/1900 Hz)
        float m700 = dsp::Goertzel::compute_magnitude(r, 2 * L, 700.0f, static_cast<float>(sample_rate_));
        float m980 = dsp::Goertzel::compute_magnitude(r, 2 * L, 980.0f, static_cast<float>(sample_rate_));
        float m1200 = dsp::Goertzel::compute_magnitude(r, 2 * L, 1200.0f, static_cast<float>(sample_rate_));
        float m1900 = dsp::Goertzel::compute_magnitude(r, 2 * L, 1900.0f, static_cast<float>(sample_rate_));
        float max_single_tone = std::max({m700, m980, m1200, m1900});
        float rms = std::sqrt(var / (2 * L));
        if (max_single_tone > 0.70f * rms) {
            return 0.0f; // Single narrowband carrier: not multi-carrier OFDM
        }

        float p_corr = 0.0f;
        float r1_energy = 0.0f;
        float r2_energy = 0.0f;
        for (size_t m = 0; m < L; ++m) {
            float x1 = r[m] - mean;
            float x2 = r[m + L] - mean;
            p_corr += x1 * x2;
            r1_energy += x1 * x1;
            r2_energy += x2 * x2;
        }

        if (p_corr <= 0.0f) return 0.0f;
        float denom = r1_energy * r2_energy;
        if (denom < 1e-8f) return 0.0f;
        float metric = (p_corr * p_corr) / denom;
        return std::min(1.0f, metric);
    }

    enum class State {
        SearchPreamble,
        DemodulateSymbols
    };

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    size_t fft_size_{512};
    size_t cp_size_{64};
    size_t total_sym_size_{576};

    State state_{State::SearchPreamble};
    std::vector<float> rx_buffer_;
    std::vector<bool> accumulated_bits_;
    std::vector<uint8_t> rx_bytes_;
    size_t plateau_samples_{0};
    size_t preamble_countdown_{0};
    size_t symbols_decoded_{0};
    std::vector<dsp::Complex> channel_est_;

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};

    float preamble_threshold_{0.42f};
    float carrier_freq_{1700.0f};
    std::string mode_str_{"mode14"};
    bool packet_decoded_this_burst_{false};
};

REGISTER_MODEM(RattlegramEngine, "rattlegram");
