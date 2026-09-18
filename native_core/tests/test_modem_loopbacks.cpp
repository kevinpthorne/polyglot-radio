#include "modem_registry.h"
#include "IModemEngine.h"
#include "native_bridge.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>

static std::string g_last_text_stream;
static std::string g_last_packet_decoded;
static int g_last_event_type = 0;

void test_event_callback(const NativeModemEvent* ev) {
    if (!ev) return;
    g_last_event_type = ev->event_type;
    if (ev->event_type == static_cast<int32_t>(EventType::TextStream) && ev->payload && ev->payload_len > 0) {
        g_last_text_stream.append(reinterpret_cast<const char*>(ev->payload), ev->payload_len);
    } else if (ev->event_type == static_cast<int32_t>(EventType::PacketDecoded) && ev->payload && ev->payload_len > 0) {
        g_last_packet_decoded.assign(reinterpret_cast<const char*>(ev->payload), ev->payload_len);
    }
}

void test_cw_loopback() {
    std::cout << "[TEST] CW Morse modulation/demodulation loopback..." << std::endl;
    auto cw = ModemRegistry::get().instantiate("cw_morse");
    assert(cw != nullptr);

    g_last_text_stream.clear();
    cw->init(48000, test_event_callback);

    std::string msg = "EE"; // Short sequence for quick unit test
    bool ok = cw->prepare_tx(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), nullptr);
    assert(ok);

    std::vector<float> audio;
    std::vector<float> chunk(1024);
    while (cw->is_tx_active()) {
        size_t n = cw->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(!audio.empty());

    // Feed audio into demodulator
    cw->process_rx(audio.data(), audio.size());
    // Process trailing silence to trigger space/character end
    std::vector<float> silence(48000, 0.0f);
    cw->process_rx(silence.data(), silence.size());

    std::cout << "  -> CW Decoded: \"" << g_last_text_stream << "\"" << std::endl;
    assert(!g_last_text_stream.empty());
    std::cout << "  -> PASS" << std::endl;
}

void test_aprs_loopback() {
    std::cout << "[TEST] APRS AX.25 Bell 202 loopback..." << std::endl;
    auto aprs = ModemRegistry::get().instantiate("aprs_packet");
    assert(aprs != nullptr);

    g_last_packet_decoded.clear();
    aprs->init(48000, test_event_callback);

    std::string test_payload = "=3746.00N/12225.00W-Polyglot";
    bool ok = aprs->prepare_tx(reinterpret_cast<const uint8_t*>(test_payload.data()), test_payload.size(), "{\"callsign\":\"W1AW-1\"}");
    assert(ok);

    std::vector<float> audio;
    std::vector<float> chunk(1024);
    while (aprs->is_tx_active()) {
        size_t n = aprs->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(!audio.empty());

    // Feed into demodulator
    aprs->process_rx(audio.data(), audio.size());

    std::cout << "  -> APRS Decoded: \"" << g_last_packet_decoded << "\"" << std::endl;
    assert(g_last_packet_decoded == test_payload);
    std::cout << "  -> PASS" << std::endl;
}

void test_eas_loopback() {
    std::cout << "[TEST] EAS / SAME emergency alert loopback..." << std::endl;
    auto eas = ModemRegistry::get().instantiate("eas_same");
    assert(eas != nullptr);

    g_last_packet_decoded.clear();
    eas->init(48000, test_event_callback);

    std::string test_header = "ZCZC-EAS-RWT-000000+0015-0010000-POLYGLOT-TEST-";
    bool ok = eas->prepare_tx(reinterpret_cast<const uint8_t*>(test_header.data()), test_header.size(), nullptr);
    assert(ok);

    std::vector<float> audio;
    std::vector<float> chunk(1024);
    while (eas->is_tx_active()) {
        size_t n = eas->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(!audio.empty());

    eas->process_rx(audio.data(), audio.size());

    std::cout << "  -> EAS Decoded: \"" << g_last_packet_decoded << "\"" << std::endl;
    assert(!g_last_packet_decoded.empty());
    std::cout << "  -> PASS" << std::endl;
}

void test_ultrasound_loopback() {
    std::cout << "[TEST] Ultrasound 19 kHz Multi-FSK with RS(15, 9) loopback..." << std::endl;
    auto us = ModemRegistry::get().instantiate("ultrasound");
    assert(us != nullptr);

    g_last_packet_decoded.clear();
    us->init(48000, test_event_callback);

    std::string token = "PING";
    bool ok = us->prepare_tx(reinterpret_cast<const uint8_t*>(token.data()), token.size(), nullptr);
    assert(ok);

    std::vector<float> audio;
    std::vector<float> chunk(1024);
    while (us->is_tx_active()) {
        size_t n = us->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(!audio.empty());

    us->process_rx(audio.data(), audio.size());

    std::cout << "  -> Ultrasound Decoded: \"" << g_last_packet_decoded << "\"" << std::endl;
    assert(g_last_packet_decoded == token);
    std::cout << "  -> PASS" << std::endl;
}

int main() {
    std::cout << "=== RUNNING MODEM LOOPBACK INTEGRATION TESTS ===" << std::endl;
    test_cw_loopback();
    test_aprs_loopback();
    test_eas_loopback();
    test_ultrasound_loopback();
    std::cout << "=== ALL MODEM LOOPBACK INTEGRATION TESTS PASSED ===" << std::endl;
    return 0;
}
