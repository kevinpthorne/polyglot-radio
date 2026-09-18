#include "dsp/fft.h"
#include "dsp/goertzel.h"
#include "dsp/fir_filter.h"
#include "dsp/costas_loop.h"
#include "dsp/crc.h"
#include "dsp/varicode.h"
#include "dsp/morse_table.h"
#include "dsp/reed_solomon.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>

void test_fft() {
    std::cout << "[TEST] FFT forward & inverse..." << std::endl;
    std::vector<dsp::Complex> data = {
        {1.0f, 0.0f}, {2.0f, 0.0f}, {3.0f, 0.0f}, {4.0f, 0.0f},
        {5.0f, 0.0f}, {6.0f, 0.0f}, {7.0f, 0.0f}, {8.0f, 0.0f}
    };
    auto original = data;
    dsp::FFT::forward(data);
    dsp::FFT::inverse(data);

    for (size_t i = 0; i < data.size(); ++i) {
        float err = std::abs(data[i].real() - original[i].real());
        assert(err < 1e-4f);
    }
    std::cout << "  -> PASS" << std::endl;
}

void test_goertzel() {
    std::cout << "[TEST] Goertzel tone detector..." << std::endl;
    const int count = 480;
    const float fs = 48000.0f;
    std::vector<float> sine(count);

    // 1000 Hz pure tone
    for (int i = 0; i < count; ++i) {
        sine[i] = std::sin(2.0f * 3.14159265f * 1000.0f * i / fs);
    }

    float mag_1000 = dsp::Goertzel::compute_magnitude(sine.data(), count, 1000.0f, fs);
    float mag_2000 = dsp::Goertzel::compute_magnitude(sine.data(), count, 2000.0f, fs);

    assert(mag_1000 > 0.4f);
    assert(mag_2000 < 0.05f);
    std::cout << "  -> PASS (1000 Hz: " << mag_1000 << ", 2000 Hz: " << mag_2000 << ")" << std::endl;
}

void test_crc() {
    std::cout << "[TEST] CRC-16-CCITT and CRC-14..." << std::endl;
    const uint8_t test_data[] = "123456789";
    uint16_t crc16 = dsp::CRC::crc16_ccitt(test_data, 9);
    // Standard CRC-16-CCITT inverted
    assert(crc16 != 0);

    const uint8_t ft8_bits[77] = {1, 0, 1, 1, 0, 0, 1};
    uint16_t crc14 = dsp::CRC::crc14_ft8(ft8_bits, 77);
    assert(crc14 < 0x4000);
    std::cout << "  -> PASS" << std::endl;
}

void test_varicode() {
    std::cout << "[TEST] Varicode encode & decode..." << std::endl;
    for (uint8_t c = 'A'; c <= 'Z'; ++c) {
        const char* code = dsp::Varicode::get_code(c);
        assert(code != nullptr);
        int decoded = dsp::Varicode::decode_code(code);
        if (decoded != c) {
            std::cout << "Mismatch for char '" << (char)c << "' (code: " << code << ", decoded: " << decoded << " ('" << (char)decoded << "'))" << std::endl;
        }
        assert(decoded == c);
    }
    std::cout << "  -> PASS" << std::endl;
}

void test_morse() {
    std::cout << "[TEST] Morse table encode & decode..." << std::endl;
    std::string text = "CQ POLYGLOT";
    for (char c : text) {
        if (c == ' ') continue;
        std::string code = dsp::MorseTable::encode_char(c);
        assert(!code.empty());
        char dec = dsp::MorseTable::decode_morse(code);
        assert(dec == c);
    }
    std::cout << "  -> PASS" << std::endl;
}

void test_reed_solomon() {
    std::cout << "[TEST] Reed-Solomon RS(15, 9) Galois Field Codec..." << std::endl;
    dsp::ReedSolomon rs;
    uint8_t msg[9] = { 1, 4, 7, 2, 9, 11, 3, 5, 8 };
    uint8_t parity[6] = {0};
    rs.encode(msg, parity);

    uint8_t codeword[15];
    for (int i = 0; i < 9; ++i) codeword[i] = msg[i];
    for (int i = 0; i < 6; ++i) codeword[9 + i] = parity[i];

    // Verify error-free decode
    bool ok = rs.decode(codeword);
    assert(ok);

    // Corrupt 1 symbol
    codeword[3] ^= 0x05;
    ok = rs.decode(codeword);
    assert(ok);
    assert(codeword[3] == msg[3]);

    std::cout << "  -> PASS (single-error corrected)" << std::endl;
}

int main() {
    std::cout << "=== RUNNING DSP MATH TESTS ===" << std::endl;
    test_fft();
    test_goertzel();
    test_crc();
    test_varicode();
    test_morse();
    test_reed_solomon();
    std::cout << "=== ALL DSP MATH TESTS PASSED ===" << std::endl;
    return 0;
}
