#include "modem_registry.h"
#include "IModemEngine.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>

void test_sentry_detectors() {
    std::cout << "[TEST] Sentry detectors on synthetic signals..." << std::endl;
    const int sample_rate = 48000;
    const size_t count = 2048;

    auto test_tone = [&](float freq) {
        std::vector<float> samples(count);
        for (size_t i = 0; i < count; ++i) {
            samples[i] = 0.5f * std::sin(2.0f * 3.14159265f * freq * i / sample_rate);
        }
        return samples;
    };

    // 1. SSTV 1900 Hz leader
    auto sstv = ModemRegistry::get().instantiate("sstv_engine");
    assert(sstv != nullptr);
    sstv->init(sample_rate, nullptr);
    auto sstv_audio = test_tone(1900.0f);
    float sstv_conf = sstv->check_sentry_confidence(sstv_audio.data(), sstv_audio.size());
    assert(sstv_conf > 0.5f);
    std::cout << "  -> SSTV Sentry Confidence: " << sstv_conf << std::endl;

    // 2. CW Morse 700 Hz
    auto cw = ModemRegistry::get().instantiate("cw_morse");
    assert(cw != nullptr);
    cw->init(sample_rate, nullptr);
    auto cw_audio = test_tone(700.0f);
    float cw_conf = cw->check_sentry_confidence(cw_audio.data(), cw_audio.size());
    assert(cw_conf > 0.5f);
    std::cout << "  -> CW Sentry Confidence: " << cw_conf << std::endl;

    // 3. APRS 1200 Hz
    auto aprs = ModemRegistry::get().instantiate("aprs_packet");
    assert(aprs != nullptr);
    aprs->init(sample_rate, nullptr);
    auto aprs_audio = test_tone(1200.0f);
    float aprs_conf = aprs->check_sentry_confidence(aprs_audio.data(), aprs_audio.size());
    assert(aprs_conf > 0.5f);
    std::cout << "  -> APRS Sentry Confidence: " << aprs_conf << std::endl;

    // 4. Silence check (all must be ~0)
    std::vector<float> silence(count, 0.0f);
    assert(sstv->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(cw->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(aprs->check_sentry_confidence(silence.data(), count) < 0.05f);
    std::cout << "  -> Silence baseline confirmed (< 0.05)" << std::endl;
}

int main() {
    std::cout << "=== RUNNING SENTRY CORRELATOR TESTS ===" << std::endl;
    test_sentry_detectors();
    std::cout << "=== ALL SENTRY CORRELATOR TESTS PASSED ===" << std::endl;
    return 0;
}
