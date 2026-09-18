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
    Robot36,
    Robot72,
    Robot24,
    Scottie1,
    Scottie2,
    ScottieDX,
    Martin1,
    Martin2,
    PD50,
    PD90,
    PD120,
    PD160,
    PD180,
    PD240,
    PD290,
    PasokonP3,
    PasokonP5,
    PasokonP7,
    SC2_30,
    WraaseSC2_60,
    WraaseSC2_120,
    WraaseSC2_180,
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
        y_line_.assign(640, 128.0f);
        r_minus_y_.assign(640, 0.0f);
        b_minus_y_.assign(640, 0.0f);
        r_line_.assign(640, 0.0f);
        g_line_.assign(640, 0.0f);
        b_line_.assign(640, 0.0f);
        reset();
    }

    void reset() override {
        state_ = State::WaitLeader;
        leader_count_ = 0;
        timeout_samples_ = 0;
        sync_samples_ = 0;
        vis_bits_.clear();
        vis_bit_samples_ = 0;
        current_mode_ = SstvMode::Robot36;
        current_line_ = 0;
        line_sample_idx_ = 0;
        prev_s_ = 0.0f;
        sample_idx_ = 0;
        last_cross_idx_ = 0.0f;
        current_freq_ = 1900.0f;

        std::fill(y_line_.begin(), y_line_.end(), 128.0f);
        std::fill(r_minus_y_.begin(), r_minus_y_.end(), 0.0f);
        std::fill(b_minus_y_.begin(), b_minus_y_.end(), 0.0f);
        std::fill(r_line_.begin(), r_line_.end(), 0.0f);
        std::fill(g_line_.begin(), g_line_.end(), 0.0f);
        std::fill(b_line_.begin(), b_line_.end(), 0.0f);

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    bool is_rx_active() const override {
        return (state_ == State::DemodulateLines && current_line_ < get_mode_height()) ||
               (state_ == State::WaitBreakOrVis || state_ == State::ReadVis || state_ == State::WaitVisStop);
    }

    size_t get_max_burst_samples() const override {
        return static_cast<size_t>(sample_rate_ * 300); // Up to 5 mins for high-res SSTV
    }

    float detect_instantaneous_frequency(float s) {
        sample_idx_++;
        // Sign change detection for zero crossings
        if ((prev_s_ <= 0.0f && s > 0.0f) || (prev_s_ >= 0.0f && s < 0.0f)) {
            float frac = (s != prev_s_) ? (-prev_s_) / (s - prev_s_) : 0.5f;
            float cross_idx = static_cast<float>(sample_idx_ - 1) + frac;
            if (last_cross_idx_ > 0.0f) {
                float half_samples = cross_idx - last_cross_idx_;
                // Range: 500 Hz to 3000 Hz at sample_rate
                if (half_samples >= 8.0f && half_samples <= 48.0f) {
                    float f = static_cast<float>(sample_rate_) / (2.0f * half_samples);
                    current_freq_ = 0.70f * current_freq_ + 0.30f * f;
                }
            }
            last_cross_idx_ = cross_idx;
        }
        prev_s_ = s;
        return current_freq_;
    }

    void process_rx(const float* samples, size_t count) override {
        uint8_t* canvas = Native_GetSharedCanvasPtr();

        for (size_t i = 0; i < count; ++i) {
            float s = samples[i];
            float inst_freq = detect_instantaneous_frequency(s);

            // Horizontal sync pulse detector: 1200 Hz +/- 80 Hz
            if (inst_freq >= 1120.0f && inst_freq <= 1280.0f) {
                sync_samples_++;
            } else {
                if (sync_samples_ >= 120) { // Valid sync pulse (>= 2.5ms at 48kHz)
                    if (state_ == State::DemodulateLines) {
                        size_t expected_line_samples = static_cast<size_t>(sample_rate_ * get_line_duration());
                        // GATED SYNC PLL: Accept sync pulse ONLY in the expected boundary window
                        // (either near the end >= 85% or very beginning <= 15% of the line)
                        if (line_sample_idx_ >= expected_line_samples * 0.85f || line_sample_idx_ <= expected_line_samples * 0.15f) {
                            advance_line();
                        }
                    }
                }
                sync_samples_ = 0;
            }

            if (state_ == State::WaitLeader) {
                leader_window_.push_back(s);
                if (leader_window_.size() >= 512) {
                    float m1900 = dsp::Goertzel::compute_magnitude(leader_window_.data(), leader_window_.size(), 1900.0f, static_cast<float>(sample_rate_));
                    if (m1900 > 0.035f) {
                        leader_count_++;
                        if (leader_count_ >= 3) { // ~32ms of leader tone
                            state_ = State::WaitBreakOrVis;
                            leader_count_ = 0;
                            timeout_samples_ = 0;
                        }
                    } else {
                        leader_count_ = 0;
                    }
                    leader_window_.clear();
                }
            } else if (state_ == State::WaitBreakOrVis) {
                timeout_samples_++;
                leader_window_.push_back(s);
                if (leader_window_.size() >= 256) {
                    float m1200 = dsp::Goertzel::compute_magnitude(leader_window_.data(), leader_window_.size(), 1200.0f, static_cast<float>(sample_rate_));
                    if (m1200 > 0.035f) {
                        // 1200 Hz sync/start detected!
                        state_ = State::ReadVis;
                        vis_bits_.clear();
                        vis_bit_samples_ = 0;
                        leader_window_.clear();
                    } else {
                        leader_window_.clear();
                    }
                }
                // Timeout after ~800ms of waiting: start demodulating lines with default mode
                if (timeout_samples_ >= static_cast<size_t>(sample_rate_ * 0.8f)) {
                    state_ = State::DemodulateLines;
                    current_line_ = 0;
                    line_sample_idx_ = 0;
                    sync_samples_ = 0;
                    leader_window_.clear();
                }
            } else if (state_ == State::ReadVis) {
                leader_window_.push_back(s);
                vis_bit_samples_++;
                size_t spb = static_cast<size_t>(sample_rate_ * 0.030f); // 30 ms bit length (~1440 samples)
                if (vis_bit_samples_ >= spb) {
                    // Center sample: middle 60% of the 30 ms bit
                    size_t mid_start = spb / 5;
                    size_t mid_len = (spb * 3) / 5;
                    const float* bit_samples = (leader_window_.size() >= mid_start + mid_len)
                        ? (leader_window_.data() + mid_start)
                        : leader_window_.data();
                    size_t bit_count = (leader_window_.size() >= mid_start + mid_len) ? mid_len : leader_window_.size();

                    float m1300 = dsp::Goertzel::compute_magnitude(bit_samples, bit_count, 1300.0f, static_cast<float>(sample_rate_));
                    float m1100 = dsp::Goertzel::compute_magnitude(bit_samples, bit_count, 1100.0f, static_cast<float>(sample_rate_));
                    bool bit = (m1100 > m1300); // Logic 1 is 1100 Hz, Logic 0 is 1300 Hz
                    vis_bits_.push_back(bit);
                    vis_bit_samples_ = 0;
                    leader_window_.clear();

                    if (vis_bits_.size() >= 8) {
                        decode_vis_bits();
                        state_ = State::WaitVisStop;
                        vis_bit_samples_ = 0;
                    }
                }
            } else if (state_ == State::WaitVisStop) {
                vis_bit_samples_++;
                size_t spb = static_cast<size_t>(sample_rate_ * 0.030f); // 30 ms stop bit
                if (vis_bit_samples_ >= spb) {
                    state_ = State::DemodulateLines;
                    current_line_ = 0;
                    line_sample_idx_ = 0;
                    sync_samples_ = 0;
                    vis_bit_samples_ = 0;
                }
            } else if (state_ == State::DemodulateLines) {
                demodulate_pixel_sample(canvas, inst_freq);

                line_sample_idx_++;
                size_t expected_line_samples = static_cast<size_t>(sample_rate_ * get_line_duration());
                // Flywheel timeout: advance line if sync was lost beyond 105% of duration
                if (line_sample_idx_ >= expected_line_samples * 1.05f) {
                    advance_line();
                }
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 512) return 0.0f;
        float m1900 = dsp::Goertzel::compute_magnitude(samples, count, 1900.0f, static_cast<float>(sample_rate_));
        float m2083 = dsp::Goertzel::compute_magnitude(samples, count, 2083.33f, static_cast<float>(sample_rate_));
        float m700  = dsp::Goertzel::compute_magnitude(samples, count, 700.0f, static_cast<float>(sample_rate_));
        float m980  = dsp::Goertzel::compute_magnitude(samples, count, 980.0f, static_cast<float>(sample_rate_));

        // Reject other modems: CW (700), Feld-Hell (980), EAS mark (2083)
        if (m2083 > 1.25f * m1900 && m2083 > 0.025f) return 0.0f;
        if (m700 > 1.2f * m1900 && m700 > 0.025f) return 0.0f;
        if (m980 > 1.2f * m1900 && m980 > 0.025f) return 0.0f;

        float out_band = (m700 + m980 + m2083) / 3.0f;
        if (m1900 > 0.030f && m1900 > 1.6f * (out_band + 0.003f)) {
            float conf = m1900 / (m1900 + out_band + 0.005f);
            return std::clamp(conf * 1.20f, 0.0f, 1.0f);
        }
        return 0.0f;
    }

    static void get_pixel_yuv(const uint8_t* payload, size_t len, int x, int y, float& Y, float& R_Y, float& B_Y) {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (payload && len >= 320 * 240 * 3) {
            size_t stride = (len >= 320 * 240 * 4) ? 4 : 3;
            size_t offset = (y * 320 + x) * stride;
            r = static_cast<float>(payload[offset + 0]);
            g = static_cast<float>(payload[offset + 1]);
            b = static_cast<float>(payload[offset + 2]);
        } else {
            // High-visibility standard SMPTE 8-bar test pattern (40 pixels per bar)
            int bar = std::clamp(x / 40, 0, 7);
            static const uint8_t kSmpte[8][3] = {
                {235, 235, 235}, // White
                {235, 235,  16}, // Yellow
                { 16, 235, 235}, // Cyan
                { 16, 235,  16}, // Green
                {235,  16, 235}, // Magenta
                {235,  16,  16}, // Red
                { 16,  16, 235}, // Blue
                { 16,  16,  16}  // Black
            };
            r = static_cast<float>(kSmpte[bar][0]);
            g = static_cast<float>(kSmpte[bar][1]);
            b = static_cast<float>(kSmpte[bar][2]);
            if (y >= 180 && y <= 215 && x >= 40 && x < 280) {
                r = 32.0f; g = 32.0f; b = 32.0f;
            }
        }
        Y = std::clamp(0.299f * r + 0.587f * g + 0.114f * b, 0.0f, 255.0f);
        R_Y = std::clamp((r - Y) * (128.0f / 180.0f), -128.0f, 127.0f);
        B_Y = std::clamp((b - Y) * (128.0f / 180.0f), -128.0f, 127.0f);
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

        // Parse desired mode from json_config
        uint8_t vis_code = 0x08; // Default Robot 36
        std::string mode_str = "robot36";
        if (json_config) {
            std::string cfg(json_config);
            auto pos = cfg.find("\"sstv_mode\":\"");
            if (pos != std::string::npos) {
                auto end_pos = cfg.find("\"", pos + 13);
                if (end_pos != std::string::npos) {
                    mode_str = cfg.substr(pos + 13, end_pos - (pos + 13));
                }
            }
        }
        if (mode_str == "martin1") vis_code = 0x2C;
        else if (mode_str == "martin2") vis_code = 0x28;
        else if (mode_str == "scottie1") vis_code = 0x3C;
        else if (mode_str == "scottie2") vis_code = 0x38;
        else if (mode_str == "robot72") vis_code = 0x0C;
        else if (mode_str == "pd120") vis_code = 0x5F;
        else vis_code = 0x08; // robot36

        // SSTV Header / VIS Sequence:
        // 1. Leader tone: 1900 Hz for 300 ms
        append_tone(1900.0f, 0.300f);
        // 2. Break tone: 1200 Hz for 10 ms
        append_tone(1200.0f, 0.010f);
        // 3. Leader tone: 1900 Hz for 300 ms
        append_tone(1900.0f, 0.300f);
        // 4. Start bit: 1200 Hz for 30 ms
        append_tone(1200.0f, 0.030f);

        // 5. VIS Code (7 data bits + 1 even parity bit, 30 ms each)
        uint8_t parity = 0;
        for (int b = 0; b < 7; ++b) {
            bool bit = (vis_code >> b) & 1;
            if (bit) parity ^= 1;
            append_tone(bit ? 1100.0f : 1300.0f, 0.030f);
        }
        append_tone(parity ? 1100.0f : 1300.0f, 0.030f);
        // Stop bit: 1200 Hz for 30 ms
        append_tone(1200.0f, 0.030f);

        // Full 240-line color video scanline synthesis
        const int lines = 240;

        if (mode_str == "robot72") {
            size_t y_sweep_samples = static_cast<size_t>(sample_rate_ * 0.138f); // 138ms Y (6624 samples)
            size_t c_sweep_samples = static_cast<size_t>(sample_rate_ * 0.069f); // 69ms Chroma (3312 samples)

            for (int l = 0; l < lines; ++l) {
                // Line sync: 1200 Hz for 9 ms
                append_tone(1200.0f, 0.009f);
                // Sync porch: 1500 Hz for 3 ms
                append_tone(1500.0f, 0.003f);

                // Luminance Y sweep (138 ms)
                for (size_t s = 0; s < y_sweep_samples; ++s) {
                    int px = static_cast<int>(s * 320 / y_sweep_samples);
                    float Y, R_Y, B_Y;
                    get_pixel_yuv(payload, len, px, l, Y, R_Y, B_Y);
                    float freq = 1500.0f + 800.0f * (Y / 255.0f);
                    float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                    tx_samples_.push_back(0.4f * std::sin(phase));
                    phase += phase_inc;
                    if (phase > two_pi) phase -= two_pi;
                }

                // Separator 1: 1500 Hz for 4.5 ms
                append_tone(1500.0f, 0.0045f);
                // Porch 1: 1900 Hz for 1.5 ms
                append_tone(1900.0f, 0.0015f);

                // R-Y sweep (69 ms)
                for (size_t s = 0; s < c_sweep_samples; ++s) {
                    int px = static_cast<int>(s * 320 / c_sweep_samples);
                    float Y, R_Y, B_Y;
                    get_pixel_yuv(payload, len, px, l, Y, R_Y, B_Y);
                    float freq = 1900.0f + 400.0f * (R_Y / 128.0f);
                    float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                    tx_samples_.push_back(0.4f * std::sin(phase));
                    phase += phase_inc;
                    if (phase > two_pi) phase -= two_pi;
                }

                // Separator 2: 2300 Hz for 4.5 ms
                append_tone(2300.0f, 0.0045f);
                // Porch 2: 1900 Hz for 1.5 ms
                append_tone(1900.0f, 0.0015f);

                // B-Y sweep (69 ms)
                for (size_t s = 0; s < c_sweep_samples; ++s) {
                    int px = static_cast<int>(s * 320 / c_sweep_samples);
                    float Y, R_Y, B_Y;
                    get_pixel_yuv(payload, len, px, l, Y, R_Y, B_Y);
                    float freq = 1900.0f + 400.0f * (B_Y / 128.0f);
                    float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                    tx_samples_.push_back(0.4f * std::sin(phase));
                    phase += phase_inc;
                    if (phase > two_pi) phase -= two_pi;
                }
            }
        } else {
            // Default Robot 36
            size_t y_sweep_samples = static_cast<size_t>(sample_rate_ * 0.088f); // 88ms Y (4224 samples)
            size_t c_sweep_samples = static_cast<size_t>(sample_rate_ * 0.044f); // 44ms Chroma (2112 samples)

            for (int l = 0; l < lines; ++l) {
                // Line sync: 1200 Hz for 9 ms
                append_tone(1200.0f, 0.009f);
                // Sync porch: 1500 Hz for 3 ms
                append_tone(1500.0f, 0.003f);

                // Luminance Y sweep (88 ms)
                for (size_t s = 0; s < y_sweep_samples; ++s) {
                    int px = static_cast<int>(s * 320 / y_sweep_samples);
                    float Y, R_Y, B_Y;
                    get_pixel_yuv(payload, len, px, l, Y, R_Y, B_Y);
                    float freq = 1500.0f + 800.0f * (Y / 255.0f);
                    float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
                    tx_samples_.push_back(0.4f * std::sin(phase));
                    phase += phase_inc;
                    if (phase > two_pi) phase -= two_pi;
                }

                // Separator pulse: 1500 Hz even line, 2300 Hz odd line (4.5 ms)
                append_tone((l % 2 == 0) ? 1500.0f : 2300.0f, 0.0045f);
                // Porch: 1900 Hz for 1.5 ms
                append_tone(1900.0f, 0.0015f);

                // Chrominance sweep (44 ms) centered around 1900 Hz (R-Y even, B-Y odd)
                for (size_t s = 0; s < c_sweep_samples; ++s) {
                    int px = static_cast<int>(s * 320 / c_sweep_samples);
                    float Y, R_Y, B_Y;
                    get_pixel_yuv(payload, len, px, l, Y, R_Y, B_Y);
                    float chroma = (l % 2 == 0) ? R_Y : B_Y;
                    float freq = 1900.0f + 400.0f * (chroma / 128.0f);
                    float phase_inc = two_pi * freq / static_cast<float>(sample_rate_);
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
    void decode_vis_bits() {
        uint8_t vis = 0;
        for (size_t i = 0; i < 7 && i < vis_bits_.size(); ++i) {
            if (vis_bits_[i]) vis |= (1 << i);
        }
        decode_vis_mode(vis);

        if (callback_) {
            const char* mname = get_mode_name();
            NativeModemEvent ev{};
            ev.event_type = static_cast<int32_t>(EventType::StatusUpdate);
            ev.protocol_id = get_id();
            ev.snr_db = 20.0f;
            ev.center_freq = 1900;
            ev.payload = reinterpret_cast<const uint8_t*>(mname);
            ev.payload_len = std::strlen(mname);
            ev.metadata_int = static_cast<int32_t>(current_mode_);
            callback_(&ev);
        }
    }

    void decode_vis_mode(uint8_t vis) {
        vis &= 0x7F; // Mask out parity bit
        switch (vis) {
            case 0x08: current_mode_ = SstvMode::Robot36; break;
            case 0x0C: current_mode_ = SstvMode::Robot72; break;
            case 0x04:
            case 0x02: current_mode_ = SstvMode::Robot24; break;
            case 0x3C: current_mode_ = SstvMode::Scottie1; break;
            case 0x38: current_mode_ = SstvMode::Scottie2; break;
            case 0x4C: current_mode_ = SstvMode::ScottieDX; break;
            case 0x2C: current_mode_ = SstvMode::Martin1; break;
            case 0x28: current_mode_ = SstvMode::Martin2; break;
            case 0x5D: current_mode_ = SstvMode::PD50; break;
            case 0x63: current_mode_ = SstvMode::PD90; break;
            case 0x5F: current_mode_ = SstvMode::PD120; break;
            case 0x62: current_mode_ = SstvMode::PD160; break;
            case 0x60: current_mode_ = SstvMode::PD180; break;
            case 0x61: current_mode_ = SstvMode::PD240; break;
            case 0x5E: current_mode_ = SstvMode::PD290; break;
            case 0x71: current_mode_ = SstvMode::PasokonP3; break;
            case 0x72: current_mode_ = SstvMode::PasokonP5; break;
            case 0x73: current_mode_ = SstvMode::PasokonP7; break;
            case 0x3E:
            case 0x40: current_mode_ = SstvMode::SC2_30; break;
            case 0x20: current_mode_ = SstvMode::WraaseSC2_60; break;
            case 0x24: current_mode_ = SstvMode::WraaseSC2_120; break;
            case 0x27: current_mode_ = SstvMode::WraaseSC2_180; break;
            default:   current_mode_ = SstvMode::Robot36; break;
        }
    }

    const char* get_mode_name() const {
        switch (current_mode_) {
            case SstvMode::Robot36: return "Robot 36 (Color)";
            case SstvMode::Robot72: return "Robot 72 (Color)";
            case SstvMode::Robot24: return "Robot 24 (Color)";
            case SstvMode::Scottie1: return "Scottie 1 (RGB)";
            case SstvMode::Scottie2: return "Scottie 2 (RGB)";
            case SstvMode::ScottieDX: return "Scottie DX (RGB)";
            case SstvMode::Martin1: return "Martin 1 (RGB)";
            case SstvMode::Martin2: return "Martin 2 (RGB)";
            case SstvMode::PD50: return "PD-50";
            case SstvMode::PD90: return "PD-90";
            case SstvMode::PD120: return "PD-120";
            case SstvMode::PD160: return "PD-160";
            case SstvMode::PD180: return "PD-180";
            case SstvMode::PD240: return "PD-240";
            case SstvMode::PD290: return "PD-290";
            case SstvMode::PasokonP3: return "Pasokon P3";
            case SstvMode::PasokonP5: return "Pasokon P5";
            case SstvMode::PasokonP7: return "Pasokon P7";
            case SstvMode::SC2_30: return "Wraase SC2-30";
            case SstvMode::WraaseSC2_60: return "Wraase SC2-60";
            case SstvMode::WraaseSC2_120: return "Wraase SC2-120";
            case SstvMode::WraaseSC2_180: return "Wraase SC2-180";
            default: return "SSTV";
        }
    }

    float get_line_duration() const {
        switch (current_mode_) {
            case SstvMode::Robot36: return 0.150f;
            case SstvMode::Robot72: return 0.300f;
            case SstvMode::Robot24: return 0.100f;
            case SstvMode::Scottie1: return 0.432f;
            case SstvMode::Scottie2: return 0.279f;
            case SstvMode::ScottieDX: return 1.050f;
            case SstvMode::Martin1: return 0.4467f;
            case SstvMode::Martin2: return 0.228f;
            case SstvMode::PD50: return 0.195f;
            case SstvMode::PD90: return 0.351f;
            case SstvMode::PD120: return 0.244f;
            case SstvMode::PD180: return 0.363f;
            default: return 0.150f;
        }
    }

    int get_mode_width() const {
        if (current_mode_ == SstvMode::PD120 || current_mode_ == SstvMode::PD180 ||
            current_mode_ == SstvMode::PD240 || current_mode_ == SstvMode::PasokonP3 ||
            current_mode_ == SstvMode::PasokonP5 || current_mode_ == SstvMode::PasokonP7) {
            return 640;
        }
        return 320;
    }

    int get_mode_height() const {
        if (current_mode_ == SstvMode::PD120 || current_mode_ == SstvMode::PD180 ||
            current_mode_ == SstvMode::PD240 || current_mode_ == SstvMode::PasokonP3 ||
            current_mode_ == SstvMode::PasokonP5 || current_mode_ == SstvMode::PasokonP7) {
            return 496;
        }
        if (current_mode_ == SstvMode::Robot36 || current_mode_ == SstvMode::Robot72 || current_mode_ == SstvMode::Robot24) {
            return 240;
        }
        return 256;
    }

    void advance_line() {
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

        if (current_line_ >= get_mode_height()) {
            // Full image complete!
            if (callback_) {
                NativeModemEvent ev{};
                ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                ev.protocol_id = get_id();
                ev.snr_db = 22.0f;
                ev.center_freq = 1900;
                const char* mname = get_mode_name();
                ev.payload = reinterpret_cast<const uint8_t*>(mname);
                ev.payload_len = static_cast<int32_t>(std::strlen(mname));
                ev.metadata_int = get_mode_height();
                callback_(&ev);
            }
            state_ = State::WaitLeader;
            current_line_ = 0;
        }
    }

    void demodulate_pixel_sample(uint8_t* canvas, float inst_freq) {
        if (!canvas) return;

        // Normalize frequency from 1500 Hz (black) to 2300 Hz (white)
        float norm = (inst_freq - 1500.0f) / 800.0f;
        norm = std::clamp(norm, 0.0f, 1.0f);

        if (current_mode_ == SstvMode::Robot36) {
            // Robot 36 Timing at 48kHz (7200 samples total per line):
            // advance_line() is called at the end of the 9ms sync pulse (432 samples).
            // So line_sample_idx_ = 0 begins at the start of the 1500 Hz sync porch.
            // 0..144 (3ms): Porch @ 1500Hz
            // 144..4368 (88ms = 4224 samples): Y (Luminance) sweep across 320 visible pixels
            // 4368..4584 (4.5ms): Separator (1500Hz even, 2300Hz odd)
            // 4584..4656 (1.5ms): Porch @ 1900Hz
            // 4656..6768 (44ms = 2112 samples): Chrominance (R-Y even, B-Y odd)
            // 6768..7200 (9ms = 432 samples): Next line's 1200Hz sync pulse (NOT painted)
            if (line_sample_idx_ >= 144 && line_sample_idx_ < 4368) {
                int px = static_cast<int>((line_sample_idx_ - 144) * 320 / 4224);
                if (px >= 0 && px < 320) {
                    y_line_[px] = norm * 255.0f;
                    // Real-time luminance sweep painting
                    uint8_t y_byte = static_cast<uint8_t>(y_line_[px]);
                    paint_pixel_scaled(canvas, px, current_line_, 320, 240, y_byte, y_byte, y_byte);
                }
            } else if (line_sample_idx_ >= 4656 && line_sample_idx_ < 6768) {
                int px = static_cast<int>((line_sample_idx_ - 4656) * 320 / 2112);
                if (px >= 0 && px < 320) {
                    float chroma = ((inst_freq - 1900.0f) / 400.0f) * 128.0f;
                    chroma = std::clamp(chroma, -128.0f, 127.0f);
                    if (current_line_ % 2 == 0) {
                        r_minus_y_[px] = chroma;
                    } else {
                        b_minus_y_[px] = chroma;
                    }

                    // Composite full YUV -> RGB pixel
                    float Y = y_line_[px];
                    float R = std::clamp(Y + 1.402f * r_minus_y_[px], 0.0f, 255.0f);
                    float G = std::clamp(Y - 0.344136f * b_minus_y_[px] - 0.714136f * r_minus_y_[px], 0.0f, 255.0f);
                    float B = std::clamp(Y + 1.772f * b_minus_y_[px], 0.0f, 255.0f);

                    paint_pixel_scaled(canvas, px, current_line_, 320, 240,
                                       static_cast<uint8_t>(R), static_cast<uint8_t>(G), static_cast<uint8_t>(B));
                }
            }
        } else if (current_mode_ == SstvMode::Robot72) {
            // Robot 72 Timing at 48kHz (14400 samples total per line = 300 ms):
            // 0..144 (3ms): Porch @ 1500Hz
            // 144..6768 (138ms = 6624 samples): Y (Luminance) sweep across 320 visible pixels
            // 6768..6984 (4.5ms): Separator 1 @ 1500Hz
            // 6984..7056 (1.5ms): Porch @ 1900Hz
            // 7056..10368 (69ms = 3312 samples): R-Y Chrominance sweep across 320 visible pixels
            // 10368..10584 (4.5ms): Separator 2 @ 2300Hz
            // 10584..10656 (1.5ms): Porch @ 1900Hz
            // 10656..13968 (69ms = 3312 samples): B-Y Chrominance sweep across 320 visible pixels
            // 13968..14400 (9ms = 432 samples): Next line's 1200Hz sync pulse (NOT painted)
            if (line_sample_idx_ >= 144 && line_sample_idx_ < 6768) {
                int px = static_cast<int>((line_sample_idx_ - 144) * 320 / 6624);
                if (px >= 0 && px < 320) {
                    y_line_[px] = norm * 255.0f;
                    uint8_t y_byte = static_cast<uint8_t>(y_line_[px]);
                    paint_pixel_scaled(canvas, px, current_line_, 320, 240, y_byte, y_byte, y_byte);
                }
            } else if (line_sample_idx_ >= 7056 && line_sample_idx_ < 10368) {
                int px = static_cast<int>((line_sample_idx_ - 7056) * 320 / 3312);
                if (px >= 0 && px < 320) {
                    float chroma = ((inst_freq - 1900.0f) / 400.0f) * 128.0f;
                    r_minus_y_[px] = std::clamp(chroma, -128.0f, 127.0f);
                }
            } else if (line_sample_idx_ >= 10656 && line_sample_idx_ < 13968) {
                int px = static_cast<int>((line_sample_idx_ - 10656) * 320 / 3312);
                if (px >= 0 && px < 320) {
                    float chroma = ((inst_freq - 1900.0f) / 400.0f) * 128.0f;
                    b_minus_y_[px] = std::clamp(chroma, -128.0f, 127.0f);

                    // Composite full YUV -> RGB pixel
                    float Y = y_line_[px];
                    float R = std::clamp(Y + 1.402f * r_minus_y_[px], 0.0f, 255.0f);
                    float G = std::clamp(Y - 0.344136f * b_minus_y_[px] - 0.714136f * r_minus_y_[px], 0.0f, 255.0f);
                    float B = std::clamp(Y + 1.772f * b_minus_y_[px], 0.0f, 255.0f);

                    paint_pixel_scaled(canvas, px, current_line_, 320, 240,
                                       static_cast<uint8_t>(R), static_cast<uint8_t>(G), static_cast<uint8_t>(B));
                }
            }
        } else if (current_mode_ == SstvMode::Martin1) {
            // Martin 1 (320x256, 446.7 ms per line):
            // Sync: 4.8ms, Porch: 0.57ms, Green: 146.4ms, Porch: 0.57ms, Blue: 146.4ms, Porch: 0.57ms, Red: 146.4ms
            size_t sample_rate_f = sample_rate_;
            size_t g_start = static_cast<size_t>(sample_rate_f * 0.00537f);
            size_t sweep_len = static_cast<size_t>(sample_rate_f * 0.14643f);
            size_t b_start = g_start + sweep_len + static_cast<size_t>(sample_rate_f * 0.00057f);
            size_t r_start = b_start + sweep_len + static_cast<size_t>(sample_rate_f * 0.00057f);

            if (line_sample_idx_ >= g_start && line_sample_idx_ < g_start + sweep_len) {
                int px = static_cast<int>((line_sample_idx_ - g_start) * 320 / sweep_len);
                if (px >= 0 && px < 320) g_line_[px] = norm * 255.0f;
            } else if (line_sample_idx_ >= b_start && line_sample_idx_ < b_start + sweep_len) {
                int px = static_cast<int>((line_sample_idx_ - b_start) * 320 / sweep_len);
                if (px >= 0 && px < 320) b_line_[px] = norm * 255.0f;
            } else if (line_sample_idx_ >= r_start && line_sample_idx_ < r_start + sweep_len) {
                int px = static_cast<int>((line_sample_idx_ - r_start) * 320 / sweep_len);
                if (px >= 0 && px < 320) {
                    r_line_[px] = norm * 255.0f;
                    paint_pixel_scaled(canvas, px, current_line_, 320, 256,
                                       static_cast<uint8_t>(r_line_[px]),
                                       static_cast<uint8_t>(g_line_[px]),
                                       static_cast<uint8_t>(b_line_[px]));
                }
            }
        } else {
            // General SSTV grayscale / luminance fallback sweep
            int width = get_mode_width();
            int height = get_mode_height();
            size_t samples_per_line = static_cast<size_t>(sample_rate_ * get_line_duration());
            int x = static_cast<int>((line_sample_idx_ * width) / std::max(size_t(1), samples_per_line));
            uint8_t val = static_cast<uint8_t>(norm * 255.0f);
            paint_pixel_scaled(canvas, x, current_line_, width, height, val, val, val);
        }
    }

    void paint_pixel_scaled(uint8_t* canvas, int x, int y, int src_w, int src_h, uint8_t r, uint8_t g, uint8_t b) {
        if (!canvas || x < 0 || y < 0) return;

        if (src_w == 320 && src_h <= 256) {
            // 2x horizontal scaling to 640 width
            int cx0 = x * 2;
            int cx1 = cx0 + 1;
            int cy0 = (src_h == 240) ? (y * 2) : (y * 496 / src_h);
            int cy1 = (src_h == 240) ? (cy0 + 1) : cy0;

            for (int cy = cy0; cy <= cy1; ++cy) {
                if (cy >= 496) continue;
                for (int cx = cx0; cx <= cx1; ++cx) {
                    if (cx >= 640) continue;
                    size_t idx = (cy * 640 + cx) * 4;
                    canvas[idx + 0] = r;
                    canvas[idx + 1] = g;
                    canvas[idx + 2] = b;
                    canvas[idx + 3] = 255;
                }
            }
        } else {
            if (x < 640 && y < 496) {
                size_t idx = (y * 640 + x) * 4;
                canvas[idx + 0] = r;
                canvas[idx + 1] = g;
                canvas[idx + 2] = b;
                canvas[idx + 3] = 255;
            }
        }
    }

    enum class State {
        WaitLeader,
        WaitBreakOrVis,
        ReadVis,
        WaitVisStop,
        DemodulateLines
    };

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    State state_{State::WaitLeader};
    int leader_count_{0};
    size_t timeout_samples_{0};
    std::vector<float> leader_window_;

    std::vector<bool> vis_bits_;
    size_t vis_bit_samples_{0};
    SstvMode current_mode_{SstvMode::Robot36};

    int current_line_{0};
    size_t line_sample_idx_{0};
    size_t sync_samples_{0};
    float prev_s_{0.0f};
    size_t sample_idx_{0};
    float last_cross_idx_{0.0f};
    float current_freq_{1900.0f};

    std::vector<float> y_line_;
    std::vector<float> r_minus_y_;
    std::vector<float> b_minus_y_;
    std::vector<float> r_line_;
    std::vector<float> g_line_;
    std::vector<float> b_line_;

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(SstvEngine, "sstv_engine");
