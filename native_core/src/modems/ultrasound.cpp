#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/goertzel.h"
#include "dsp/reed_solomon.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

class UltrasoundEngine : public IModemEngine {
public:
    const char* get_id() const override { return "ultrasound"; }
    const char* get_display_name() const override { return "Ultrasound (19 kHz Silent)"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        base_freq_ = 18500.0f;
        tone_spacing_ = 80.0f;
        samples_per_symbol_ = static_cast<size_t>(sample_rate_ * 0.0125f); // 600 samples
        chirp_samples_ = static_cast<size_t>(sample_rate_ * 0.050f);       // 2400 samples

        // Precompute reference chirp
        ref_chirp_.resize(chirp_samples_);
        const float two_pi = 6.28318530717958647692f;
        float f0 = 18000.0f, f1 = 20000.0f;
        float t_dur = 0.050f;
        for (size_t i = 0; i < chirp_samples_; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sample_rate_);
            float phase = two_pi * (f0 * t + (f1 - f0) / (2.0f * t_dur) * t * t);
            ref_chirp_[i] = std::cos(phase);
        }

        reset();
    }

    void reset() override {
        state_ = State::SearchChirp;
        chirp_window_.clear();
        symbol_buffer_.clear();
        rx_nibbles_.clear();

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        for (size_t i = 0; i < count; ++i) {
            float s = samples[i];

            if (state_ == State::SearchChirp) {
                chirp_window_.push_back(s);
                if (chirp_window_.size() >= chirp_samples_) {
                    float corr = compute_chirp_correlation(chirp_window_.data());
                    if (corr >= 0.70f) {
                        state_ = State::ReadSymbols;
                        chirp_window_.clear();
                        symbol_buffer_.clear();
                        rx_nibbles_.clear();
                    } else {
                        // Keep sliding window
                        chirp_window_.erase(chirp_window_.begin(), chirp_window_.begin() + 128);
                    }
                }
            } else if (state_ == State::ReadSymbols) {
                symbol_buffer_.push_back(s);
                if (symbol_buffer_.size() >= samples_per_symbol_) {
                    // Decimate 16-FSK tone
                    int best_tone = 0;
                    float max_e = 0.0f;
                    for (int t = 0; t < 16; ++t) {
                        float freq = base_freq_ + t * tone_spacing_;
                        float e = dsp::Goertzel::compute_energy(symbol_buffer_.data(), symbol_buffer_.size(), freq, static_cast<float>(sample_rate_));
                        if (e > max_e) {
                            max_e = e;
                            best_tone = t;
                        }
                    }
                    rx_nibbles_.push_back(static_cast<uint8_t>(best_tone & 0x0F));
                    symbol_buffer_.clear();

                    // Minimum frame: 2 sync nibbles + 15-nibble RS codeword (9 data, 6 parity) = 17 nibbles
                    if (rx_nibbles_.size() >= 17) {
                        if (rx_nibbles_[0] == 0x05 && rx_nibbles_[1] == 0x0A) {
                            // Sync matched! Extract 15-nibble codeword
                            uint8_t codeword[15];
                            for (int n = 0; n < 15; ++n) codeword[n] = rx_nibbles_[2 + n];

                            // RS decode
                            dsp::ReedSolomon rs;
                            bool success = rs.decode(codeword);

                            if (success && callback_) {
                                // Extract decoded data bytes (first 9 nibbles -> 4 bytes + 1 nibble)
                                std::vector<uint8_t> payload_bytes;
                                for (int n = 0; n + 1 < 9; n += 2) {
                                    uint8_t byte = (codeword[n] << 4) | (codeword[n + 1] & 0x0F);
                                    if (byte != 0) payload_bytes.push_back(byte);
                                }

                                if (!payload_bytes.empty()) {
                                    NativeModemEvent ev{};
                                    ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                                    ev.protocol_id = get_id();
                                    ev.snr_db = 24.0f;
                                    ev.center_freq = 19100;
                                    ev.payload = payload_bytes.data();
                                    ev.payload_len = payload_bytes.size();
                                    ev.metadata_int = 120;
                                    callback_(&ev);
                                }
                            }
                        }
                        state_ = State::SearchChirp;
                        rx_nibbles_.clear();
                    }
                }
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 256) return 0.0f;
        float mag = dsp::Goertzel::compute_magnitude(samples, count, 19000.0f, static_cast<float>(sample_rate_));
        return (mag > 0.02f) ? std::min(1.0f, mag * 25.0f) : 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;

        tx_samples_.clear();
        tx_playback_pos_ = 0;

        const float two_pi = 6.28318530717958647692f;

        // 1. Preamble Linear Up-Chirp (18 to 20 kHz, 50ms)
        float f0 = 18000.0f, f1 = 20000.0f;
        float t_dur = 0.050f;
        for (size_t i = 0; i < chirp_samples_; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sample_rate_);
            float phase = two_pi * (f0 * t + (f1 - f0) / (2.0f * t_dur) * t * t);
            // Tukey taper
            float w = 1.0f;
            size_t taper_len = chirp_samples_ / 10;
            if (i < taper_len) w = 0.5f * (1.0f - std::cos(3.14159265f * i / taper_len));
            else if (i > chirp_samples_ - taper_len) w = 0.5f * (1.0f - std::cos(3.14159265f * (chirp_samples_ - i) / taper_len));
            tx_samples_.push_back(0.4f * w * std::cos(phase));
        }

        // 2. Sync Nibbles: 0x5, 0xA
        std::vector<uint8_t> nibbles = { 0x05, 0x0A };

        // 3. Prepare 9 data nibbles for RS(15, 9)
        uint8_t rs_msg[9] = {0};
        size_t n_idx = 0;
        for (size_t i = 0; i < len && n_idx < 9; ++i) {
            rs_msg[n_idx++] = (payload[i] >> 4) & 0x0F;
            if (n_idx < 9) {
                rs_msg[n_idx++] = payload[i] & 0x0F;
            }
        }

        // 4. Compute RS(15, 9) parity
        dsp::ReedSolomon rs;
        uint8_t parity[6] = {0};
        rs.encode(rs_msg, parity);

        for (int i = 0; i < 9; ++i) nibbles.push_back(rs_msg[i]);
        for (int i = 0; i < 6; ++i) nibbles.push_back(parity[i]);

        // 5. Synthesize 16-FSK tones
        float phase = 0.0f;
        for (uint8_t nibble : nibbles) {
            float freq = base_freq_ + (nibble & 0x0F) * tone_spacing_;
            float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
            for (size_t s = 0; s < samples_per_symbol_; ++s) {
                tx_samples_.push_back(0.35f * std::sin(phase));
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
    float compute_chirp_correlation(const float* window) {
        float num = 0.0f;
        float den1 = 0.0f, den2 = 0.0f;
        for (size_t i = 0; i < chirp_samples_; ++i) {
            float w = window[i];
            float r = ref_chirp_[i];
            num += w * r;
            den1 += w * w;
            den2 += r * r;
        }
        float den = std::sqrt(den1 * den2);
        return (den > 1e-6f) ? std::abs(num / den) : 0.0f;
    }

    enum class State {
        SearchChirp,
        ReadSymbols
    };

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    float base_freq_{18500.0f};
    float tone_spacing_{80.0f};
    size_t samples_per_symbol_{600};
    size_t chirp_samples_{2400};
    std::vector<float> ref_chirp_;

    State state_{State::SearchChirp};
    std::vector<float> chirp_window_;
    std::vector<float> symbol_buffer_;
    std::vector<uint8_t> rx_nibbles_;

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(UltrasoundEngine, "ultrasound");
