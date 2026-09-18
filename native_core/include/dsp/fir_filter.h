#pragma once
#include <vector>
#include <cmath>
#include <cstddef>

namespace dsp {

class FIRFilter {
public:
    FIRFilter() : state_(), coeffs_() {}

    void set_coefficients(const std::vector<float>& coeffs) {
        coeffs_ = coeffs;
        state_.assign(coeffs.size(), 0.0f);
    }

    static std::vector<float> design_bandpass(size_t taps, float center_freq, float bandwidth, float sample_rate) {
        if (taps % 2 == 0) taps += 1; // Enforce odd length for symmetric filter
        std::vector<float> coeffs(taps);
        const float pi = 3.14159265358979323846f;
        int m = static_cast<int>(taps - 1) / 2;
        float f1 = (center_freq - bandwidth / 2.0f) / sample_rate;
        float f2 = (center_freq + bandwidth / 2.0f) / sample_rate;

        float sum = 0.0f;
        for (int i = 0; i < static_cast<int>(taps); ++i) {
            int n = i - m;
            float h = 0.0f;
            if (n == 0) {
                h = 2.0f * (f2 - f1);
            } else {
                h = (std::sin(2.0f * pi * f2 * n) - std::sin(2.0f * pi * f1 * n)) / (pi * n);
            }
            // Hamming window
            float w = 0.54f - 0.46f * std::cos(2.0f * pi * i / (taps - 1));
            coeffs[i] = h * w;
            sum += std::abs(coeffs[i]);
        }
        if (sum > 0.0f) {
            for (auto& c : coeffs) c /= sum;
        }
        return coeffs;
    }

    float process_sample(float in) {
        if (coeffs_.empty()) return in;
        // Shift state
        for (size_t i = state_.size() - 1; i > 0; --i) {
            state_[i] = state_[i - 1];
        }
        state_[0] = in;

        float out = 0.0f;
        for (size_t i = 0; i < coeffs_.size(); ++i) {
            out += coeffs_[i] * state_[i];
        }
        return out;
    }

    void reset() {
        std::fill(state_.begin(), state_.end(), 0.0f);
    }

private:
    std::vector<float> state_;
    std::vector<float> coeffs_;
};

} // namespace dsp
