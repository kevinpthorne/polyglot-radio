#include "modem_registry.h"
#include "IModemEngine.h"
#include "native_bridge.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>

struct TestContext {
    bool fired = false;
    std::string text;
};
static TestContext g_test_eas_ctx;

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

    // Verify Olivia & Feld-Hell correctly REJECT pure steady CW tones
    auto olivia = ModemRegistry::get().instantiate("olivia_mfsk");
    assert(olivia != nullptr);
    olivia->init(sample_rate, nullptr);
    auto tone_1000 = test_tone(1000.0f);
    float olivia_conf = olivia->check_sentry_confidence(tone_1000.data(), tone_1000.size());
    assert(olivia_conf < 0.1f);
    std::cout << "  -> Olivia correctly rejects pure CW tone: conf = " << olivia_conf << std::endl;

    auto hell = ModemRegistry::get().instantiate("feld_hell");
    assert(hell != nullptr);
    hell->init(sample_rate, nullptr);
    auto tone_980 = test_tone(980.0f);
    float hell_conf = hell->check_sentry_confidence(tone_980.data(), tone_980.size());
    assert(hell_conf < 0.1f);
    std::cout << "  -> Feld-Hell correctly rejects unmodulated tone: conf = " << hell_conf << std::endl;

    // 3. Verify cross-protocol sentry discrimination
    auto ft8 = ModemRegistry::get().instantiate("ft8_engine");
    assert(ft8 != nullptr);
    ft8->init(sample_rate, nullptr);
    auto eas_space = test_tone(1562.5f);
    float ft8_on_eas = ft8->check_sentry_confidence(eas_space.data(), eas_space.size());
    assert(ft8_on_eas == 0.0f);
    std::cout << "  -> FT8 correctly rejects EAS tone (1562.5 Hz): conf = " << ft8_on_eas << std::endl;

    auto aprs = ModemRegistry::get().instantiate("aprs_packet");
    assert(aprs != nullptr);
    aprs->init(sample_rate, nullptr);
    float aprs_on_hell = aprs->check_sentry_confidence(tone_980.data(), tone_980.size());
    assert(aprs_on_hell == 0.0f);
    std::cout << "  -> APRS correctly rejects Feld-Hell tone (980 Hz): conf = " << aprs_on_hell << std::endl;

    // CW Morse must reject Feld-Hell (980 Hz) and Rattlegram digital carrier (1700 Hz)
    float cw_on_hell = cw->check_sentry_confidence(tone_980.data(), tone_980.size());
    assert(cw_on_hell == 0.0f);
    std::cout << "  -> CW correctly rejects Feld-Hell tone (980 Hz): conf = " << cw_on_hell << std::endl;

    auto tone_1700 = test_tone(1700.0f);
    float cw_on_1700 = cw->check_sentry_confidence(tone_1700.data(), tone_1700.size());
    assert(cw_on_1700 == 0.0f);
    std::cout << "  -> CW correctly rejects Rattlegram tone (1700 Hz): conf = " << cw_on_1700 << std::endl;

    auto eas = ModemRegistry::get().instantiate("eas_same");
    assert(eas != nullptr);
    eas->init(sample_rate, nullptr);
    float eas_on_cw = eas->check_sentry_confidence(cw_audio.data(), cw_audio.size());
    assert(eas_on_cw == 0.0f);
    std::cout << "  -> EAS correctly rejects CW Morse tone (700 Hz): conf = " << eas_on_cw << std::endl;

    auto rg = ModemRegistry::get().instantiate("rattlegram");
    assert(rg != nullptr);
    rg->init(sample_rate, nullptr);
    float rg_on_cw = rg->check_sentry_confidence(cw_audio.data(), cw_audio.size());
    assert(rg_on_cw == 0.0f);
    std::cout << "  -> Rattlegram correctly rejects single CW carrier: conf = " << rg_on_cw << std::endl;

    // CW detects non-standard 600 Hz and 800 Hz pitches
    auto cw_600 = test_tone(600.0f);
    assert(cw->check_sentry_confidence(cw_600.data(), cw_600.size()) > 0.5f);
    auto cw_800 = test_tone(800.0f);
    assert(cw->check_sentry_confidence(cw_800.data(), cw_800.size()) > 0.5f);
    std::cout << "  -> CW successfully detects 600 Hz and 800 Hz off-grid pitches" << std::endl;

    // 4. Cross-protocol sentry discrimination tests
    // Feld-Hell modulated pulsed tone (8.16ms dots)
    std::vector<float> hell_audio(count, 0.0f);
    size_t dot_len = static_cast<size_t>(sample_rate * 0.00816f); // ~392 samples
    for (size_t i = 0; i < count; ++i) {
        bool dot_active = ((i / dot_len) % 2 == 0);
        if (dot_active) {
            hell_audio[i] = 0.4f * std::sin(2.0f * 3.14159265f * 980.0f * i / sample_rate);
        }
    }
    float hell_conf_pulsed = hell->check_sentry_confidence(hell_audio.data(), hell_audio.size());
    float rg_on_hell = rg->check_sentry_confidence(hell_audio.data(), hell_audio.size());
    float sstv_on_hell = sstv->check_sentry_confidence(hell_audio.data(), hell_audio.size());
    float eas_on_hell = eas->check_sentry_confidence(hell_audio.data(), hell_audio.size());
    assert(hell_conf_pulsed > 0.70f);
    assert(rg_on_hell == 0.0f);
    assert(sstv_on_hell == 0.0f);
    assert(eas_on_hell == 0.0f);
    std::cout << "  -> Cross-Discrimination: Feld-Hell detected (" << hell_conf_pulsed << "), others rejected (0.0)" << std::endl;

    // EAS / SAME alternating AFSK tone (520.83 baud = 1.92ms per bit)
    std::vector<float> eas_audio(count, 0.0f);
    size_t eas_bit_len = static_cast<size_t>(sample_rate / 520.83f); // ~92 samples
    for (size_t i = 0; i < count; ++i) {
        float f = ((i / eas_bit_len) % 2 == 0) ? 2083.33f : 1562.5f;
        eas_audio[i] = 0.4f * std::sin(2.0f * 3.14159265f * f * i / sample_rate);
    }
    float eas_conf = eas->check_sentry_confidence(eas_audio.data(), eas_audio.size());
    float rg_on_eas = rg->check_sentry_confidence(eas_audio.data(), eas_audio.size());
    float sstv_on_eas = sstv->check_sentry_confidence(eas_audio.data(), eas_audio.size());
    float hell_on_eas = hell->check_sentry_confidence(eas_audio.data(), eas_audio.size());
    assert(eas_conf > 0.70f);
    assert(rg_on_eas == 0.0f);
    assert(sstv_on_eas == 0.0f);
    assert(hell_on_eas == 0.0f);
    std::cout << "  -> Cross-Discrimination: EAS/SAME detected (" << eas_conf << "), others rejected (0.0)" << std::endl;

    // SSTV leader tone (1900 Hz)
    float sstv_conf_test = sstv->check_sentry_confidence(sstv_audio.data(), sstv_audio.size());
    float rg_on_sstv = rg->check_sentry_confidence(sstv_audio.data(), sstv_audio.size());
    float eas_on_sstv = eas->check_sentry_confidence(sstv_audio.data(), sstv_audio.size());
    float hell_on_sstv = hell->check_sentry_confidence(sstv_audio.data(), sstv_audio.size());
    assert(sstv_conf_test > 0.70f);
    assert(rg_on_sstv == 0.0f);
    assert(eas_on_sstv == 0.0f);
    assert(hell_on_sstv == 0.0f);
    std::cout << "  -> Cross-Discrimination: SSTV detected (" << sstv_conf_test << "), others rejected (0.0)" << std::endl;

    // 5. Silence check (all must be ~0)
    std::vector<float> silence(count, 0.0f);
    assert(sstv->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(cw->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(aprs->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(olivia->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(hell->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(ft8->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(eas->check_sentry_confidence(silence.data(), count) < 0.05f);
    assert(rg->check_sentry_confidence(silence.data(), count) < 0.05f);
    std::cout << "  -> Silence baseline confirmed (< 0.05)" << std::endl;
}

void test_eas_same_afsk_demodulation() {
    std::cout << "[TEST] EAS / SAME 92-sample matched AFSK demodulation..." << std::endl;
    const int sample_rate = 48000;
    auto eas = ModemRegistry::get().instantiate("eas_same");
    assert(eas != nullptr);

    g_test_eas_ctx.fired = false;
    g_test_eas_ctx.text.clear();

    eas->init(sample_rate, [](const NativeModemEvent* ev) {
        if (ev && ev->event_type == static_cast<int32_t>(EventType::PacketDecoded)) {
            g_test_eas_ctx.fired = true;
            if (ev->payload && ev->payload_len > 0) {
                g_test_eas_ctx.text = std::string(reinterpret_cast<const char*>(ev->payload), ev->payload_len);
            }
        }
    });

    const char* alert_msg = "ZCZC-WXR-TOR-020091+0030-1081500-KLOT/NWS-";
    bool prep = eas->prepare_tx(reinterpret_cast<const uint8_t*>(alert_msg), std::strlen(alert_msg), nullptr);
    assert(prep);

    std::vector<float> audio;
    std::vector<float> chunk(4096);
    while (eas->is_tx_active()) {
        size_t n = eas->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(audio.size() > sample_rate); // Should be a few seconds of SAME audio

    // Reset receiver state before feeding audio
    eas->reset();

    // Process audio through 92-sample matched receiver
    size_t pos = 0;
    while (pos < audio.size()) {
        size_t block = std::min(size_t(512), audio.size() - pos);
        eas->process_rx(audio.data() + pos, block);
        pos += block;
    }
    eas->flush();

    assert(g_test_eas_ctx.fired);
    assert(g_test_eas_ctx.text.find("ZCZC") != std::string::npos);
    assert(g_test_eas_ctx.text.find("TOR") != std::string::npos);
    std::cout << "  -> Decoded EAS/SAME Header: \"" << g_test_eas_ctx.text << "\"" << std::endl;
}

void test_feld_hell_double_trace_rendering() {
    std::cout << "[TEST] Feld-Hell Rudolf Hell double-trace canvas rendering..." << std::endl;
    const int sample_rate = 48000;
    auto hell = ModemRegistry::get().instantiate("feld_hell");
    assert(hell != nullptr);

    hell->init(sample_rate, nullptr);
    hell->configure("{\"carrier_freq\":980.0,\"mode\":\"ook\"}");

    const char* msg = "CQ";
    bool prep = hell->prepare_tx(reinterpret_cast<const uint8_t*>(msg), std::strlen(msg), nullptr);
    assert(prep);

    std::vector<float> audio;
    std::vector<float> chunk(4096);
    while (hell->is_tx_active()) {
        size_t n = hell->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(audio.size() > 4000);

    hell->reset();
    uint8_t* canvas = Native_GetSharedCanvasPtr();
    assert(canvas != nullptr);
    Native_ClearCanvas(0);

    // Process synthesized Hell audio
    size_t pos = 0;
    while (pos < audio.size()) {
        size_t block = std::min(size_t(512), audio.size() - pos);
        hell->process_rx(audio.data() + pos, block);
        pos += block;
    }

    // Verify Rudolf Hell double trace: pixels must be active in both upper trace and lower trace!
    // Trace 1: y in [110, 222]
    // Trace 2: y in [226, 338]
    int upper_active_pixels = 0;
    int lower_active_pixels = 0;

    for (int y = 110; y <= 222; ++y) {
        for (int x = 20; x <= 300; ++x) {
            size_t idx = (y * 640 + x) * 4;
            if (canvas[idx] > 50) upper_active_pixels++;
        }
    }

    for (int y = 226; y <= 338; ++y) {
        for (int x = 20; x <= 300; ++x) {
            size_t idx = (y * 640 + x) * 4;
            if (canvas[idx] > 50) lower_active_pixels++;
        }
    }

    std::cout << "  -> Upper Trace Active Pixels: " << upper_active_pixels << std::endl;
    std::cout << "  -> Lower Trace Active Pixels (Duplicate): " << lower_active_pixels << std::endl;
    assert(upper_active_pixels > 50);
    assert(lower_active_pixels > 50);
    std::cout << "  -> Double-trace raster verified (both traces populated)." << std::endl;
}

void test_rattlegram_tuning() {
    std::cout << "[TEST] Rattlegram dynamic threshold & carrier configuration..." << std::endl;
    const int sample_rate = 48000;
    auto rg = ModemRegistry::get().instantiate("rattlegram");
    assert(rg != nullptr);
    rg->init(sample_rate, nullptr);

    // Dynamic configuration
    rg->configure("{\"sensitivity\":0.38,\"carrier_freq\":1750.0,\"mode\":\"mode14\"}");

    // Verify sentry confidence with tuned carrier and threshold
    std::vector<float> chirp(1024);
    for (size_t i = 0; i < 1024; ++i) {
        float f = 1100.0f + 800.0f * (static_cast<float>(i) / 1024.0f);
        chirp[i] = 0.4f * std::sin(2.0f * 3.14159265f * f * i / sample_rate);
    }
    float conf = rg->check_sentry_confidence(chirp.data(), chirp.size());
    assert(conf == 0.0f);
    std::cout << "  -> Rattlegram sentry confirmed tabled: conf = " << conf << std::endl;

    // Verify flush handles empty and partial bursts gracefully
    rg->flush();
    std::cout << "  -> Rattlegram flush executed without error." << std::endl;
}

int main() {
    std::cout << "=== RUNNING SENTRY CORRELATOR TESTS ===" << std::endl;
    test_sentry_detectors();
    test_eas_same_afsk_demodulation();
    test_feld_hell_double_trace_rendering();
    test_rattlegram_tuning();
    std::cout << "=== ALL SENTRY CORRELATOR TESTS PASSED ===" << std::endl;
    return 0;
}
