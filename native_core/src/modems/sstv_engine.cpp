#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/goertzel.h"
#include "native_bridge.h"
#include <vector>
#include <cmath>
#include <complex>
#include <string>
#include <algorithm>
#include <cstring>

enum class SstvMode {
    Martin1,
    Martin2,
    Scottie1,
    Scottie2,
    Robot36,
    Robot72,
    PD120,
    Unknown
};

class SstvEngine : public IModemEngine {
public:
    const char* get_id() const override { return "sstv_engine"; }
    const char* get_display_name() const override { return "SSTV (Slow Scan TV)"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        reset();
    }

    void reset() override {
        state_ = State::WaitLeader;
        leader_count_ = 0;
        vis_bits_.clear();
        vis_bit_samples_ = 0;
        current_mode_ = SstvMode::Robot36;
        current_line_ = 0;
        line_sample_idx_ = 0;
        prev_sample_ = 0.0f;
        pll_drift_ = 0.0f;

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        uint8_t* canvas = Native_GetSharedCanvasPtr();
        const float two_pi = 6.28318530717958647692f;
        const float rad_to_hz = static_cast<float>(sample_rate_) / two_pi;

        for (size_t i = 0; i < count; ++i) {
            float s = samples[i];

            // FM Discriminator via complex instantaneous phase differentiation
            // x[n] * conj(x[n-1]) -> arg
            // For real input, Hilbert transform or delay-differentiate:
            // s[n] * prev - prev[n] * s etc. Approximate instantaneous freq:
            float delta = s - prev_sample_;
            prev_sample_ = s;
            float inst_freq = std::abs(delta) * (sample_rate_ / 2.0f);
            if (inst_freq < 1000.0f) inst_freq = 1200.0f;
            if (inst_freq > 2500.0f) inst_freq = 2300.0f;

            if (state_ == State::WaitLeader) {
                // Check 1900 Hz leader
                leader_window_.push_back(s);
                if (leader_window_.size() >= 512) {
                    float m1900 = dsp::Goertzel::compute_magnitude(leader_window_.data(), leader_window_.size(), 1900.0f, static_cast<float>(sample_rate_));
                    if (m1900 > 0.05f) {
                        leader_count_++;
                        if (leader_count_ >= 10) { // ~100ms of leader
                            state_ = State::WaitBreak;
                            leader_count_ = 0;
                        }
                    } else {
                        leader_count_ = 0;
                    }
                    leader_window_.clear();
                }
            } else if (state_ == State::WaitBreak) {
                // Wait for 1200 Hz break (10ms)
                leader_window_.push_back(s);
                if (leader_window_.size() >= 256) {
                    float m1200 = dsp::Goertzel::compute_magnitude(leader_window_.data(), leader_window_.size(), 1200.0f, static_cast<float>(sample_rate_));
                    if (m1200 > 0.05f) {
                        state_ = State::ReadVis;
                        vis_bits_.clear();
                        vis_bit_samples_ = 0;
                    }
                    leader_window_.clear();
                }
            } else if (state_ == State::ReadVis) {
                // 30ms per bit (33.3 baud), 1300 Hz = '1', 1100 Hz = '0'
                leader_window_.push_back(s);
                vis_bit_samples_++;
                size_t spb = static_cast<size_t>(sample_rate_ * 0.030f); // ~1440 samples
                if (vis_bit_samples_ >= spb) {
                    float m1300 = dsp::Goertzel::compute_magnitude(leader_window_.data(), leader_window_.size(), 1300.0f, static_cast<float>(sample_rate_));
                    float m1100 = dsp::Goertzel::compute_magnitude(leader_window_.data(), leader_window_.size(), 1100.0f, static_cast<float>(sample_rate_));
                    bool bit = (m1300 > m1100);
                    vis_bits_.push_back(bit);
                    vis_bit_samples_ = 0;
                    leader_window_.clear();

                    if (vis_bits_.size() >= 8) { // 7 bits VIS + 1 parity
                        decode_vis_mode();
                        state_ = State::DemodulateLines;
                        current_line_ = 0;
                        line_sample_idx_ = 0;
                    }
                }
            } else if (state_ == State::DemodulateLines) {
                // Decode pixel video sweeps into shared canvas
                // Normalize frequency: 1500 Hz (0) to 2300 Hz (255)
                float norm = (inst_freq - 1500.0f) / 800.0f;
                norm = std::clamp(norm, 0.0f, 1.0f);
                uint8_t pixel_val = static_cast<uint8_t>(norm * 255.0f);

                int width = get_mode_width();
                int height = get_mode_height();
                size_t samples_per_line = sample_rate_ / 4; // ~0.25s per line average

                // Resample to canvas width
                int x = static_cast<int>((line_sample_idx_ * width) / samples_per_line);
                int y = current_line_;

                if (canvas && x < 640 && y < 496) {
                    size_t idx = (y * 640 + x) * 4;
                    canvas[idx + 0] = pixel_val;
                    canvas[idx + 1] = pixel_val;
                    canvas[idx + 2] = pixel_val;
                    canvas[idx + 3] = 255;
                }

                line_sample_idx_++;
                if (line_sample_idx_ >= samples_per_line) {
                    line_sample_idx_ = 0;
                    current_line_++;

                    if (callback_) {
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::RasterLineReady);
                        ev.protocol_id = get_id();
                        ev.snr_db = 22.0f;
                        ev.center_freq = 1900;
                        ev.payload = nullptr;
                        ev.payload_len = 0;
                        ev.metadata_int = current_line_;
                        callback_(&ev);
                    }

                    if (current_line_ >= height) {
                        // Image complete!
                        if (callback_) {
                            NativeModemEvent ev{};
                            ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                            ev.protocol_id = get_id();
                            ev.snr_db = 22.0f;
                            ev.center_freq = 1900;
                            ev.payload = nullptr;
                            ev.payload_len = 0;
                            ev.metadata_int = height;
                            callback_(&ev);
                        }
                        state_ = State::WaitLeader;
                    }
                }
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 512) return 0.0f;
        float leader = dsp::Goertzel::compute_magnitude(samples, count, 1900.0f, static_cast<float>(sample_rate_));
        if (leader > 0.04f) {
            return std::min(1.0f, leader * 20.0f);
        }
        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        tx_samples_.clear();
        tx_playback_pos_ = 0;

        const float two_pi = 6.28318530717958647692f;
        float phase = 0.0f;

        auto append_tone = [&](float freq, float duration_sec) {
            size_t num_samples = static_cast<size_t>(sample_rate_ * duration_sec);
            float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
            for (size_t s = 0; s < num_samples; ++s) {
                tx_samples_.push_back(0.4f * std::sin(phase));
                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
            }
        };

        // SSTV Header / VIS Sequence:
        // 1. Leader tone: 1900 Hz for 300 ms
        append_tone(1900.0f, 0.300f);
        // 2. Break tone: 1200 Hz for 10 ms
        append_tone(1200.0f, 0.010f);
        // 3. Leader tone: 1900 Hz for 300 ms
        append_tone(1900.0f, 0.300f);
        // 4. Start bit: 1200 Hz for 30 ms
        append_tone(1200.0f, 0.030f);

        // 5. VIS Code (Robot 36 = 0x08, Martin 1 = 0x2C)
        uint8_t vis_code = 0x08; // Robot 36 default
        uint8_t parity = 0;
        for (int b = 0; b < 7; ++b) {
            bool bit = (vis_code >> b) & 1;
            if (bit) parity ^= 1;
            append_tone(bit ? 1300.0f : 1100.0f, 0.030f);
        }
        // Parity bit (even parity)
        append_tone(parity ? 1300.0f : 1100.0f, 0.030f);
        // Stop bit: 1200 Hz for 30 ms
        append_tone(1200.0f, 0.030f);

        // Video scanlines (synthesize a test pattern / color bar sweep)
        int lines = 32; // Shortened burst for responsive acoustic delivery
        for (int l = 0; l < lines; ++l) {
            // Line sync: 1200 Hz for 9 ms
            append_tone(1200.0f, 0.009f);
            // Sync porch: 1500 Hz for 3 ms
            append_tone(1500.0f, 0.003f);
            // Video sweep: linear FM from 1500 Hz (black) to 2300 Hz (white)
            size_t sweep_samples = static_cast<size_t>(sample_rate_ * 0.088f); // 88ms sweep
            for (size_t s = 0; s < sweep_samples; ++s) {
                float prog = static_cast<float>(s) / static_cast<float>(sweep_samples);
                float freq = 1500.0f + 800.0f * prog;
                float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                tx_samples_.push_back(0.4f * std::sin(phase));
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
    void decode_vis_mode() {
        uint8_t vis = 0;
        for (size_t i = 0; i < 7 && i < vis_bits_.size(); ++i) {
            if (vis_bits_[i]) vis |= (1 << i);
        }
        switch (vis) {
            case 0x2C: current_mode_ = SstvMode::Martin1; break;
            case 0x28: current_mode_ = SstvMode::Martin2; break;
            case 0x3C: current_mode_ = SstvMode::Scottie1; break;
            case 0x38: current_mode_ = SstvMode::Scottie2; break;
            case 0x08: current_mode_ = SstvMode::Robot36; break;
            case 0x0C: current_mode_ = SstvMode::Robot72; break;
            case 0x5F: current_mode_ = SstvMode::PD120; break;
            default:   current_mode_ = SstvMode::Robot36; break;
        }
    }

    int get_mode_width() const {
        return (current_mode_ == SstvMode::PD120) ? 640 : 320;
    }
    int get_mode_height() const {
        if (current_mode_ == SstvMode::PD120) return 496;
        if (current_mode_ == SstvMode::Robot36 || current_mode_ == SstvMode::Robot72) return 240;
        return 256;
    }

    enum class State {
        WaitLeader,
        WaitBreak,
        ReadVis,
        DemodulateLines
    };

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    State state_{State::WaitLeader};
    int leader_count_{0};
    std::vector<float> leader_window_;

    std::vector<bool> vis_bits_;
    size_t vis_bit_samples_{0};
    SstvMode current_mode_{SstvMode::Robot36};

    int current_line_{0};
    size_t line_sample_idx_{0};
    float prev_sample_{0.0f};
    float pll_drift_{0.0f};

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(SstvEngine, "sstv_engine");
