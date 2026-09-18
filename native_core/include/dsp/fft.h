#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <cstddef>
#include <algorithm>

namespace dsp {

using Complex = std::complex<float>;

class FFT {
public:
    static void forward(std::vector<Complex>& a) {
        size_t n = a.size();
        if (n <= 1) return;

        // Bit-reversal permutation
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) {
                j ^= bit;
            }
            j ^= bit;
            if (i < j) {
                std::swap(a[i], a[j]);
            }
        }

        // Cooley-Tukey radix-2
        const float pi = 3.14159265358979323846f;
        for (size_t len = 2; len <= n; len <<= 1) {
            float ang = -2.0f * pi / static_cast<float>(len);
            Complex wlen(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < n; i += len) {
                Complex w(1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j) {
                    Complex u = a[i + j];
                    Complex v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    static void inverse(std::vector<Complex>& a) {
        size_t n = a.size();
        for (auto& x : a) {
            x = std::conj(x);
        }
        forward(a);
        float inv_n = 1.0f / static_cast<float>(n);
        for (auto& x : a) {
            x = std::conj(x) * inv_n;
        }
    }

    static void compute_magnitudes(const std::vector<Complex>& fft_data, std::vector<float>& magnitudes) {
        size_t half_n = fft_data.size() / 2;
        magnitudes.resize(half_n);
        float max_mag = 1e-6f;
        for (size_t i = 0; i < half_n; ++i) {
            float mag = std::abs(fft_data[i]);
            magnitudes[i] = mag;
            if (mag > max_mag) max_mag = mag;
        }
        // Normalize 0.0 to 1.0
        for (size_t i = 0; i < half_n; ++i) {
            magnitudes[i] /= max_mag;
        }
    }
};

} // namespace dsp
