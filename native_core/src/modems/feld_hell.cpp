#include "IModemEngine.h"
#include "modem_registry.h"
#include "dsp/goertzel.h"
#include "native_bridge.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <cctype>

class FeldHellEngine : public IModemEngine {
public:
    const char* get_id() const override { return "feld_hell"; }
    const char* get_display_name() const override { return "Feld Hell (Hellschreiber)"; }
    int get_sample_rate() const override { return sample_rate_; }

    void init(int sample_rate, EventCallback callback) override {
        sample_rate_ = sample_rate;
        callback_ = callback;
        samples_per_dot_ = static_cast<float>(sample_rate_) / 122.5f; // ~391.8 samples
        reset();
    }

    void reset() override {
        dot_sample_counter_ = 0.0f;
        current_column_ = 0;
        current_row_ = 0;
        column_energy_ = 0.0f;
        column_sample_count_ = 0;

        is_tx_active_ = false;
        tx_samples_.clear();
        tx_playback_pos_ = 0;
    }

    void process_rx(const float* samples, size_t count) override {
        uint8_t* canvas = Native_GetSharedCanvasPtr();

        for (size_t i = 0; i < count; ++i) {
            float s = samples[i];
            column_energy_ += s * s;
            column_sample_count_++;
            dot_sample_counter_ += 1.0f;

            if (dot_sample_counter_ >= samples_per_dot_) {
                dot_sample_counter_ -= samples_per_dot_;

                float rms = (column_sample_count_ > 0) ? std::sqrt(column_energy_ / column_sample_count_) : 0.0f;
                column_energy_ = 0.0f;
                column_sample_count_ = 0;

                // Threshold to 0 or 255
                uint8_t pixel_val = (rms > 0.04f) ? 255 : 0;

                // Paint to shared canvas if available
                // Sized 640x496
                if (canvas) {
                    // Vertical raster scan (14 pixels high)
                    int y = (current_row_ % 14) * 8 + 50;
                    int x = (current_column_ % 600) + 20;

                    for (int dy = 0; dy < 6; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            int px = x + dx;
                            int py = y + dy;
                            if (px < 640 && py < 496) {
                                size_t idx = (py * 640 + px) * 4;
                                canvas[idx + 0] = pixel_val; // R
                                canvas[idx + 1] = pixel_val; // G
                                canvas[idx + 2] = pixel_val; // B
                                canvas[idx + 3] = 255;       // A
                            }
                        }
                    }
                }

                current_row_++;
                if (current_row_ >= 14) {
                    current_row_ = 0;
                    current_column_++;

                    if (callback_ && (current_column_ % 7 == 0)) {
                        NativeModemEvent ev{};
                        ev.event_type = static_cast<int32_t>(EventType::RasterLineReady);
                        ev.protocol_id = get_id();
                        ev.snr_db = 16.0f;
                        ev.center_freq = 980;
                        ev.payload = nullptr;
                        ev.payload_len = 0;
                        ev.metadata_int = current_column_;
                        callback_(&ev);
                    }
                }
            }
        }
    }

    float check_sentry_confidence(const float* samples, size_t count) override {
        if (count < 256) return 0.0f;
        float mag = dsp::Goertzel::compute_magnitude(samples, count, 980.0f, static_cast<float>(sample_rate_));
        if (mag > 0.03f) {
            return std::min(1.0f, mag * 20.0f);
        }
        return 0.0f;
    }

    bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) override {
        if (!payload || len == 0) return false;

        tx_samples_.clear();
        tx_playback_pos_ = 0;

        float tone_freq = 980.0f;
        const float two_pi = 6.28318530717958647692f;
        float phase = 0.0f;
        float phase_inc = two_pi * tone_freq / static_cast<float>(sample_rate_);
        size_t spd = static_cast<size_t>(samples_per_dot_);

        auto append_dot = [&](bool on) {
            for (size_t s = 0; s < spd; ++s) {
                float val = on ? (0.4f * std::sin(phase)) : 0.0f;
                tx_samples_.push_back(val);
                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
            }
        };

        // Lead-in silence
        for (int i = 0; i < 28; ++i) append_dot(false);

        for (size_t i = 0; i < len; ++i) {
            char c = std::toupper(payload[i]);
            // 7x7 standard Hellschreiber font column representation (each column 7 bits doubled to 14 bits)
            const auto& cols = get_font_columns(c);
            for (uint8_t col : cols) {
                // Scan bottom to top, 7 rows doubled to 14
                for (int r = 0; r < 7; ++r) {
                    bool dot = (col >> r) & 1;
                    append_dot(dot);
                    append_dot(dot); // Doubled scan
                }
            }
            // Inter-character space (1 blank column = 14 blank dots)
            for (int r = 0; r < 14; ++r) append_dot(false);
        }

        // Tail silence
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
        // Basic 7-high bitmap font columns
        switch (c) {
            case 'A': return {0x7C, 0x12, 0x11, 0x12, 0x7C};
            case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
            case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
            case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
            case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
            case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
            case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
            case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
            case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
            case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
            case ' ': return {0x00, 0x00, 0x00};
            default:  return {0x55, 0x2A, 0x55, 0x2A};
        }
    }

    int sample_rate_{48000};
    EventCallback callback_{nullptr};
    float samples_per_dot_{391.836f};

    float dot_sample_counter_{0.0f};
    int current_column_{0};
    int current_row_{0};
    float column_energy_{0.0f};
    size_t column_sample_count_{0};

    bool is_tx_active_{false};
    std::vector<float> tx_samples_;
    size_t tx_playback_pos_{0};
};

REGISTER_MODEM(FeldHellEngine, "feld_hell");
