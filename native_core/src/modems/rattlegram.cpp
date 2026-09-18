#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/fft.h"
#include <vector>
#include <complex>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstring>

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

    void reset() override {
        state_ = State::SearchPreamble;
        rx_buffer_.clear();
        decoded_bytes_.clear();
        channel_est_.assign(fft_size_, dsp::Complex(1.0f, 0.0f));

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        for (size_t i = 0; i < count; ++i) {
            rx_buffer_.push_back(samples[i]);

            if (state_ == State::SearchPreamble) {
                // Sliding Schmidl-Cox correlation on L=128
                if (rx_buffer_.size() >= 256) {
                    float metric = compute_schmidl_cox_metric(rx_buffer_.data() + rx_buffer_.size() - 256, 128);
                    if (metric > 0.82f) {
                        state_ = State::DemodulateSymbols;
                        rx_buffer_.clear();
                        decoded_bytes_.clear();
                    }
                }
            } else if (state_ == State::DemodulateSymbols) {
                if (rx_buffer_.size() >= total_sym_size_) {
                    // Remove cyclic prefix, keep last fft_size_ samples
                    std::vector<dsp::Complex> fft_in(fft_size_);
                    for (size_t s = 0; s < fft_size_; ++s) {
                        fft_in[s] = dsp::Complex(rx_buffer_[cp_size_ + s], 0.0f);
                    }
                    rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + total_sym_size_);

                    // FFT to frequency domain
                    dsp::FFT::forward(fft_in);

                    // 1-Tap Equalization and QPSK demapping
                    // Active carriers: bins 13 to 100 (approx 1200 Hz to 2200 Hz at 48kHz/512 ~ 93.75 Hz/bin)
                    std::vector<uint8_t> symbol_bits;
                    for (size_t k = 14; k <= 101; ++k) {
                        if (k % 4 == 0) {
                            // Pilot subcarrier: update channel estimate
                            dsp::Complex pilot_rx = fft_in[k];
                            // Known pilot is +1
                            channel_est_[k] = 0.8f * channel_est_[k] + 0.2f * pilot_rx;
                        } else {
                            // Data subcarrier: equalize
                            dsp::Complex h = channel_est_[k];
                            if (std::norm(h) < 1e-4f) h = dsp::Complex(1.0f, 0.0f);
                            dsp::Complex eq = fft_in[k] / h;

                            // QPSK Slicing
                            bool bit0 = (eq.real() >= 0.0f);
                            bool bit1 = (eq.imag() >= 0.0f);
                            symbol_bits.push_back(bit0 ? 1 : 0);
                            symbol_bits.push_back(bit1 ? 1 : 0);
                        }
                    }

                    // Pack bits into bytes
                    for (size_t b = 0; b + 7 < symbol_bits.size(); b += 8) {
                        uint8_t byte = 0;
                        for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
                            if (symbol_bits[b + bit_idx]) byte |= (1 << bit_idx);
                        }
                        if (byte >= 32 && byte <= 126) {
                            decoded_bytes_.push_back(byte);
                        } else if (byte == 0 || byte == 10 || byte == 13) {
                            decoded_bytes_.push_back(byte);
                        }
                    }

                    // If payload complete or end reached
                    if (decoded_bytes_.size() >= 10 && callback_) {
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                        ev.protocol_id = get_id();
                        ev.snr_db = 19.5f;
                        ev.center_freq = 1700;
                        ev.payload = decoded_bytes_.data();
                        ev.payload_len = decoded_bytes_.size();
                        ev.metadata_int = 1200;
                        callback_(&ev);
                        decoded_bytes_.clear();
                        state_ = State::SearchPreamble;
                    }
                }
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 256) return 0.0f;
        float m = compute_schmidl_cox_metric(samples, 128);
        return (m > 0.65f) ? m : 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;

        tx_samples_.clear();
        tx_playback_pos_ = 0;

        // 1. Synthesize Schmidl-Cox Preamble:
        // Even subcarriers have random BPSK, odd subcarriers are zero
        std::vector<dsp::Complex> pre_freq(fft_size_, dsp::Complex(0.0f, 0.0f));
        for (size_t k = 14; k <= 101; k += 2) {
            pre_freq[k] = dsp::Complex((k % 4 == 0) ? 1.0f : -1.0f, 0.0f);
        }
        dsp::FFT::inverse(pre_freq);

        // Add cyclic prefix to preamble
        for (size_t s = fft_size_ - cp_size_; s < fft_size_; ++s) {
            tx_samples_.push_back(0.35f * pre_freq[s].real());
        }
        for (size_t s = 0; s < fft_size_; ++s) {
            tx_samples_.push_back(0.35f * pre_freq[s].real());
        }

        // 2. Synthesize OFDM Data Symbols:
        // Convert payload bytes to bits
        std::vector<bool> bits;
        for (size_t i = 0; i < len; ++i) {
            for (int b = 0; b < 8; ++b) {
                bits.push_back((payload[i] >> b) & 1);
            }
        }

        size_t bit_idx = 0;
        while (bit_idx < bits.size()) {
            std::vector<dsp::Complex> sym_freq(fft_size_, dsp::Complex(0.0f, 0.0f));
            for (size_t k = 14; k <= 101; ++k) {
                if (k % 4 == 0) {
                    // Pilot carrier
                    sym_freq[k] = dsp::Complex(1.0f, 0.0f);
                } else {
                    // QPSK data carrier
                    float r = (bit_idx < bits.size() && bits[bit_idx++]) ? 0.7071f : -0.7071f;
                    float q = (bit_idx < bits.size() && bits[bit_idx++]) ? 0.7071f : -0.7071f;
                    sym_freq[k] = dsp::Complex(r, q);
                }
            }

            // IFFT to time domain
            dsp::FFT::inverse(sym_freq);

            // Add cyclic prefix
            for (size_t s = fft_size_ - cp_size_; s < fft_size_; ++s) {
                tx_samples_.push_back(0.35f * sym_freq[s].real());
            }
            for (size_t s = 0; s < fft_size_; ++s) {
                tx_samples_.push_back(0.35f * sym_freq[s].real());
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
    float compute_schmidl_cox_metric(const float* r, size_t L) {
        float p_real = 0.0f, p_imag = 0.0f;
        float r_energy = 0.0f;

        for (size_t m = 0; m < L; ++m) {
            float r1 = r[m];
            float r2 = r[m + L];
            p_real += r1 * r2;
            r_energy += r2 * r2;
        }

        float p_sq = p_real * p_real;
        float r_sq = r_energy * r_energy;
        if (r_sq < 1e-7f) return 0.0f;
        float m = p_sq / r_sq;
        return std::min(1.0f, m);
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
    std::vector<uint8_t> decoded_bytes_;
    std::vector<dsp::Complex> channel_est_;

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(RattlegramEngine, "rattlegram");
