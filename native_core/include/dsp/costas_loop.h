#pragma once
#include <cmath>
#include <vector>

namespace dsp {

class CostasLoop {
public:
    CostasLoop(float center_freq, float sample_rate, float alpha = 0.05f, float beta = 0.001f)
        : sample_rate_(sample_rate),
          center_freq_(center_freq),
          current_freq_(center_freq),
          phase_(0.0f),
          alpha_(alpha),
          beta_(beta) {}

    void process(float sample, float& out_i, float& out_q) {
        const float two_pi = 6.28318530717958647692f;
        // Local oscillator
        float osc_cos = std::cos(phase_);
        float osc_sin = std::sin(phase_);

        // Quadrature downconversion
        out_i = sample * osc_cos;
        out_q = -sample * osc_sin;

        // BPSK phase error detector: e = I * Q * sign(I^2 - Q^2)
        float diff = (out_i * out_i) - (out_q * out_q);
        float sign_val = (diff >= 0.0f) ? 1.0f : -1.0f;
        float error = out_i * out_q * sign_val;

        // Clamp error to prevent instability
        if (error > 1.0f) error = 1.0f;
        if (error < -1.0f) error = -1.0f;

        // Loop filter
        current_freq_ += (beta_ * error * sample_rate_ / two_pi);
        // Constrain tracking range to +/- 100 Hz
        if (current_freq_ < center_freq_ - 100.0f) current_freq_ = center_freq_ - 100.0f;
        if (current_freq_ > center_freq_ + 100.0f) current_freq_ = center_freq_ + 100.0f;

        phase_ += two_pi * current_freq_ / sample_rate_ + alpha_ * error;
        while (phase_ > two_pi) phase_ -= two_pi;
        while (phase_ < 0.0f) phase_ += two_pi;
    }

    float get_current_freq() const { return current_freq_; }
    void reset(float freq) {
        center_freq_ = freq;
        current_freq_ = freq;
        phase_ = 0.0f;
    }

private:
    float sample_rate_;
    float center_freq_;
    float current_freq_;
    float phase_;
    float alpha_;
    float beta_;
};

} // namespace dsp
