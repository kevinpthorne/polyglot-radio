#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/goertzel.h"
#include "native_bridge.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>

class FeldHellEngine : public IModemEngine {
public:
    const char* get_id() const override { return "feld_hell"; }
    const char* get_display_name() const override { return "Feld Hell (Hellschreiber)"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        samples_per_dot_ = static_cast<float>(sample_rate_) / 122.5f; // ~391.836 samples
        reset();
    }

    void reset() override {
        dot_sample_counter_ = 0.0f;
        current_column_ = 0;
        current_row_ = 0;
        dot_window_.clear();
        has_dots_in_burst_ = false;
        carrier_freq_ = default_carrier_freq_;
        tuned_carrier_ = false;
        accumulated_rx_samples_ = 0;

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void configure(const char* json_config) override {
        if (!json_config) return;
        std::string cfg(json_config);

        auto pos_freq = cfg.find("\"carrier_freq\":");
        if (pos_freq != std::string::npos) {
            try {
                float freq = std::stof(cfg.substr(pos_freq + 15));
                if (freq >= 500.0f && freq <= 2500.0f) {
                    carrier_freq_ = freq;
                    default_carrier_freq_ = freq;
                    tuned_carrier_ = true;
                }
            } catch (...) {}
        }

        if (cfg.find("\"mode\":\"fsk") != std::string::npos || cfg.find("\"mode\":\"fsk240") != std::string::npos) {
            is_fsk_mode_ = true;
        } else if (cfg.find("\"mode\":\"ook") != std::string::npos) {
            is_fsk_mode_ = false;
        }
    }

    bool is_rx_active() const override {
        return has_dots_in_burst_ && current_column_ > 0 && current_column_ < 120;
    }

    size_t get_max_burst_samples() const override {
        return static_cast<size_t>(sample_rate_ * 60); // 1 minute ticker
    }

    void process_rx(const float* samples, size_t count) override {
        uint8_t* canvas = Native_GetSharedCanvasPtr();

        // 1. Adaptive frequency tracking: scan 700-1800 Hz during burst onset if not statically set
        if (!tuned_carrier_ && count >= 512 && accumulated_rx_samples_ < static_cast<size_t>(sample_rate_ * 2)) {
            float best_mag = 0.0f;
            float best_f = carrier_freq_;
            for (float f = 700.0f; f <= 1800.0f; f += 25.0f) {
                float m = dsp::Goertzel::compute_magnitude(samples, std::min(count, size_t(1024)), f, static_cast<float>(sample_rate_));
                if (m > best_mag) {
                    best_mag = m;
                    best_f = f;
                }
            }
            if (best_mag > 0.025f) {
                carrier_freq_ = best_f;
                tuned_carrier_ = true;
            }
        }
        accumulated_rx_samples_ += count;

        for (size_t i = 0; i < count; ++i) {
            float s = samples[i];
            dot_window_.push_back(s);
            dot_sample_counter_ += 1.0f;

            if (dot_sample_counter_ >= samples_per_dot_) {
                dot_sample_counter_ -= samples_per_dot_;

                float mag_mark = dsp::Goertzel::compute_magnitude(dot_window_.data(), dot_window_.size(), carrier_freq_, static_cast<float>(sample_rate_));
                float mag_space = dsp::Goertzel::compute_magnitude(dot_window_.data(), dot_window_.size(), carrier_freq_ - fsk_shift_, static_cast<float>(sample_rate_));
                dot_window_.clear();

                float mag = mag_mark;
                // Automatic FSK-Hell vs OOK detection: if complementary energy exists at carrier - 240 Hz
                if (is_fsk_mode_ || (mag_space > 0.025f && (mag_mark + mag_space) > 0.040f)) {
                    mag = std::max(0.0f, mag_mark - 0.70f * mag_space);
                }

                // Soft anti-aliased grayscale intensity [0..255]
                float norm = (mag - 0.012f) / 0.035f;
                uint8_t pixel_val = static_cast<uint8_t>(std::clamp(norm, 0.0f, 1.0f) * 255.0f);
                if (pixel_val > 30) has_dots_in_burst_ = true;

                // Rudolf Hell standard double-trace canvas rendering
                if (canvas) {
                    // Shared canvas is 640 wide x 496 tall
                    // Dot size: 5px wide (dx: 0..4), 8px high (dy: 0..7)
                    // 14 dots per column scan bottom to top (row 0 at bottom, row 13 at top)
                    int y_row = 13 - (current_row_ % 14);
                    int y1 = 110 + y_row * 8;         // Trace 1 (upper trace)
                    int y2 = 110 + 116 + y_row * 8;   // Trace 2 (lower trace, Rudolf Hell duplicate)
                    int x = 20 + current_column_ * 5; // 5 pixels per column

                    for (int dy = 0; dy < 7; ++dy) {
                        for (int dx = 0; dx < 4; ++dx) {
                            int px = x + dx;
                            if (px < 640) {
                                // Draw Trace 1
                                int py1 = y1 + dy;
                                if (py1 < 496) {
                                    size_t idx1 = (py1 * 640 + px) * 4;
                                    canvas[idx1 + 0] = pixel_val;
                                    canvas[idx1 + 1] = pixel_val;
                                    canvas[idx1 + 2] = pixel_val;
                                    canvas[idx1 + 3] = 255;
                                }
                                // Draw Trace 2 (stacked duplicate to eliminate phase offset splitting)
                                int py2 = y2 + dy;
                                if (py2 < 496) {
                                    size_t idx2 = (py2 * 640 + px) * 4;
                                    canvas[idx2 + 0] = pixel_val;
                                    canvas[idx2 + 1] = pixel_val;
                                    canvas[idx2 + 2] = pixel_val;
                                    canvas[idx2 + 3] = 255;
                                }
                            }
                        }
                    }
                }

                current_row_++;
                if (current_row_ >= 14) {
                    current_row_ = 0;
                    current_column_++;

                    // Fire live line ready event every 2 columns for fluid UI updates
                    if (callback_ && (current_column_ % 2 == 0)) {
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::RasterLineReady);
                        ev.protocol_id = get_id();
                        ev.snr_db = 18.0f;
                        ev.center_freq = static_cast<int32_t>(carrier_freq_);
                        ev.payload = nullptr;
                        ev.payload_len = 0;
                        ev.metadata_int = current_column_;
                        callback_(&ev);
                    }

                    // Wrap around ticker tape when approaching canvas right border (120 columns * 5px = 600px)
                    if (current_column_ >= 120) {
                        if (callback_) {
                            NativeModemEvent ev{};
                            ev.event_type = static_cast<int32_t>(EventType::PacketDecoded);
                            ev.protocol_id = get_id();
                            ev.snr_db = 18.0f;
                            ev.center_freq = static_cast<int32_t>(carrier_freq_);
                            const char* msg = "Feld-Hell Ticker Tape Line";
                            ev.payload = reinterpret_cast<const uint8_t*>(msg);
                            ev.payload_len = static_cast<int32_t>(std::strlen(msg));
                            ev.metadata_int = current_column_;
                            callback_(&ev);
                        }
                        current_column_ = 0;
                    }
                }
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 512) return 0.0f;

        // Check carrier frequency candidates (default, 980, 1000, 1225)
        float max_mag = 0.0f;
        float target_f = carrier_freq_;
        const float candidates[] = {carrier_freq_, 980.0f, 1000.0f, 1225.0f};
        for (float f : candidates) {
            float m = dsp::Goertzel::compute_magnitude(samples, count, f, static_cast<float>(sample_rate_));
            if (m > max_mag) {
                max_mag = m;
                target_f = f;
            }
        }

        if (max_mag < 0.020f) return 0.0f;

        // Reject other modems (CW Morse 700/800 Hz, EAS 1562.5 Hz, SSTV 1900 Hz)
        float mag_cw = dsp::Goertzel::compute_magnitude(samples, count, 700.0f, static_cast<float>(sample_rate_));
        float mag_cw8 = dsp::Goertzel::compute_magnitude(samples, count, 800.0f, static_cast<float>(sample_rate_));
        float mag_eas = dsp::Goertzel::compute_magnitude(samples, count, 1562.5f, static_cast<float>(sample_rate_));
        float mag_sstv = dsp::Goertzel::compute_magnitude(samples, count, 1900.0f, static_cast<float>(sample_rate_));

        if (std::max(mag_cw, mag_cw8) > 1.2f * max_mag && std::max(mag_cw, mag_cw8) > 0.025f) return 0.0f;
        if (mag_eas > 1.2f * max_mag && mag_eas > 0.025f) return 0.0f;
        if (mag_sstv > 1.2f * max_mag && mag_sstv > 0.025f) return 0.0f;

        // Distinguish between keyed Hell dots and a pure continuous unmodulated tone (CW/tuning carrier)
        // A continuous steady carrier has identical energy across all sub-dot slices (ratio > 0.85).
        // Keyed Hell dots alternate on/off, giving near-zero energy in off-slices (ratio < 0.40).
        if (count >= 1024) {
            size_t slice_len = 256;
            size_t num_slices = count / slice_len;
            float min_s = 1e9f;
            float max_s = 0.0f;
            for (size_t s = 0; s < num_slices; ++s) {
                float sm = dsp::Goertzel::compute_magnitude(samples + s * slice_len, slice_len, target_f, static_cast<float>(sample_rate_));
                if (sm < min_s) min_s = sm;
                if (sm > max_s) max_s = sm;
            }
            if (max_s > 0.04f && min_s > 0.80f * max_s) {
                // Completely continuous unkeyed carrier: reject in favor of CW
                return 0.0f;
            }
        }

        float out_band = (mag_cw + mag_eas + mag_sstv) / 3.0f;
        if (max_mag > 1.6f * (out_band + 0.003f)) {
            float conf = max_mag / (max_mag + out_band + 0.005f);
            return std::clamp(conf * 1.15f, 0.0f, 1.0f);
        }

        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;
        configure(json_config);

        tx_samples_.clear();
        tx_playback_pos_ = 0;

        const float two_pi = 6.28318530717958647692f;
        float phase_mark = 0.0f;
        float phase_space = 0.0f;
        float inc_mark = two_pi * carrier_freq_ / static_cast<float>(sample_rate_);
        float inc_space = two_pi * (carrier_freq_ - fsk_shift_) / static_cast<float>(sample_rate_);
        size_t spd = static_cast<size_t>(samples_per_dot_);

        auto append_dot = [&](bool on) {
            for (size_t s = 0; s < spd; ++s) {
                float val = 0.0f;
                if (on) {
                    val = 0.45f * std::sin(phase_mark);
                } else if (is_fsk_mode_) {
                    val = 0.45f * std::sin(phase_space);
                }
                tx_samples_.push_back(val);
                phase_mark += inc_mark;
                if (phase_mark > two_pi) phase_mark -= two_pi;
                phase_space += inc_space;
                if (phase_space > two_pi) phase_space -= two_pi;
            }
        };

        // Lead-in silence (2 columns)
        for (int i = 0; i < 28; ++i) append_dot(false);

        for (size_t i = 0; i < len; ++i) {
            char c = std::toupper(payload[i]);
            const auto& cols = get_font_columns(c);
            for (uint8_t col : cols) {
                // Scan bottom to top, 7 rows doubled to 14
                for (int r = 0; r < 7; ++r) {
                    bool dot = (col >> r) & 1;
                    append_dot(dot);
                    append_dot(dot); // 2 dots per row = 14 dots per column
                }
            }
            // Inter-character spacing (2 blank columns = 28 dots)
            for (int r = 0; r < 28; ++r) append_dot(false);
        }

        // Tail silence (2 columns)
        for (int i = 0; i < 28; ++i) append_dot(false);

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
    std::vector<uint8_t> get_font_columns(char c) {
        switch (c) {
            case 'A': return {0x7C, 0x12, 0x11, 0x12, 0x7C};
            case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
            case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
            case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
            case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
            case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
            case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
            case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
            case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
            case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
            case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
            case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
            case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
            case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
            case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
            case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
            case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
            case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
            case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
            case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
            case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
            case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
            case 'W': return {0x7F, 0x20, 0x18, 0x20, 0x7F};
            case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
            case 'Y': return {0x07, 0x08, 0x70, 0x08, 0x07};
            case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};

            case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
            case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
            case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
            case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
            case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
            case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
            case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
            case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
            case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
            case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};

            case ' ': return {0x00, 0x00, 0x00};
            case '.': return {0x40, 0x00, 0x00};
            case ',': return {0x40, 0x20, 0x00};
            case '?': return {0x02, 0x01, 0x51, 0x09, 0x06};
            case '/': return {0x20, 0x10, 0x08, 0x04, 0x02};
            case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
            case '=': return {0x14, 0x14, 0x14, 0x14, 0x14};
            case ':': return {0x36, 0x36, 0x00};
            case '!': return {0x00, 0x7D, 0x00};
            case '+': return {0x08, 0x08, 0x3E, 0x08, 0x08};
            default:  return {0x55, 0x2A, 0x55, 0x2A};
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    float samples_per_dot_{391.836f};

    float default_carrier_freq_{980.0f};
    float carrier_freq_{980.0f};
    bool tuned_carrier_{false};
    bool is_fsk_mode_{false};
    float fsk_shift_{240.0f};
    size_t accumulated_rx_samples_{0};

    float dot_sample_counter_{0.0f};
    int current_column_{0};
    int current_row_{0};
    std::vector<float> dot_window_;
    bool has_dots_in_burst_{false};

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(FeldHellEngine, "feld_hell");
