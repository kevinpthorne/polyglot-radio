#pragma once
#include <cmath>
#include <cstddef>

namespace dsp {

class Goertzel {
public:
    static float compute_energy(const float* samples, size_t count, float target_freq, float sample_rate) {
        if (count == 0 || sample_rate <= 0.0f) return 0.0f;

        const float pi = 3.14159265358979323846f;
        float omega = 2.0f * pi * target_freq / sample_rate;
        float coeff = 2.0f * std::cos(omega);

        float s_prev = 0.0f;
        float s_prev2 = 0.0f;

        for (size_t i = 0; i < count; ++i) {
            float s = samples[i] + coeff * s_prev - s_prev2;
            s_prev2 = s_prev;
            s_prev = s;
        }

        float power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
        return (power < 0.0f) ? 0.0f : (power / static_cast<float>(count * count));
    }

    static float compute_magnitude(const float* samples, size_t count, float target_freq, float sample_rate) {
        return std::sqrt(compute_energy(samples, count, target_freq, sample_rate));
    }
};

} // namespace dsp
