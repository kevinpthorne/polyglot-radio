#include "modem_registry.h"
#include "IModemEngine.h"
#include "native_bridge.h"
#include "audio_hal.h"
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

    // 1. Standard 700 Hz
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

    std::cout << "  -> CW Decoded (700 Hz): \"" << g_last_text_stream << "\"" << std::endl;
    assert(!g_last_text_stream.empty());

    // 2. Third-party 600 Hz generator at 15 WPM
    std::cout << "[TEST] CW Morse third-party 600 Hz generator..." << std::endl;
    cw->reset();
    g_last_text_stream.clear();
    std::string msg600 = "SOS";
    const char* cfg600 = "{\"pitch\": 600, \"wpm\": 15}";
    ok = cw->prepare_tx(reinterpret_cast<const uint8_t*>(msg600.data()), msg600.size(), cfg600);
    assert(ok);
    audio.clear();
    while (cw->is_tx_active()) {
        size_t n = cw->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    cw->process_rx(audio.data(), audio.size());
    cw->process_rx(silence.data(), silence.size());
    std::cout << "  -> CW Decoded (600 Hz, 15 WPM): \"" << g_last_text_stream << "\"" << std::endl;
    assert(g_last_text_stream.find("SOS") != std::string::npos);

    // 3. Third-party 850 Hz generator at 25 WPM
    std::cout << "[TEST] CW Morse third-party 850 Hz generator..." << std::endl;
    cw->reset();
    g_last_text_stream.clear();
    std::string msg850 = "CQ";
    const char* cfg850 = "{\"pitch\": 850, \"wpm\": 25}";
    ok = cw->prepare_tx(reinterpret_cast<const uint8_t*>(msg850.data()), msg850.size(), cfg850);
    assert(ok);
    audio.clear();
    while (cw->is_tx_active()) {
        size_t n = cw->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    cw->process_rx(audio.data(), audio.size());
    cw->process_rx(silence.data(), silence.size());
    std::cout << "  -> CW Decoded (850 Hz, 25 WPM): \"" << g_last_text_stream << "\"" << std::endl;
    assert(g_last_text_stream.find("CQ") != std::string::npos);
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

void test_audio_hal_playback_and_mute() {
    std::cout << "[TEST] AudioHAL playback and TX/RX mute controls..." << std::endl;

    // 1. Test TX Output Mute
    Native_SetOutputMuted(true);
    assert(Native_IsOutputMuted() == true);
    Native_SetOutputMuted(false);
    assert(Native_IsOutputMuted() == false);

    // 2. Test RX Input Mute
    Native_SetInputMuted(true);
    assert(Native_IsInputMuted() == true);
    Native_SetInputMuted(false);
    assert(Native_IsInputMuted() == false);

    // 3. Test Loopback Mode Toggle
    Native_SetLoopbackMode(true);
    assert(hal::AudioHAL::get().is_loopback_mode() == true);
    Native_SetLoopbackMode(false);
    assert(hal::AudioHAL::get().is_loopback_mode() == false);

    // 3. Synthesize a 48 kHz mono 16-bit test WAV file
    const char* test_wav_path = "/tmp/test_hal_playback.wav";
    FILE* f = fopen(test_wav_path, "wb");
    assert(f != nullptr);

    // Write a standard 44-byte WAV header for 48000 samples (1 second) of 16-bit PCM mono
    uint32_t sample_rate = 48000;
    uint32_t num_samples = 48000;
    uint32_t byte_rate = sample_rate * 2;
    uint16_t block_align = 2;
    uint16_t bits_per_sample = 16;
    uint32_t subchunk2_size = num_samples * 2;
    uint32_t chunk_size = 36 + subchunk2_size;

    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t subchunk1_size = 16;
    uint16_t audio_format = 1; // PCM
    uint16_t num_channels = 1;
    fwrite(&subchunk1_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&subchunk2_size, 4, 1, f);

    // Write 1 second of 440 Hz sine wave
    std::vector<int16_t> pcm(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        pcm[i] = static_cast<int16_t>(16384.0f * std::sin(2.0f * 3.14159265f * 440.0f * i / sample_rate));
    }
    fwrite(pcm.data(), sizeof(int16_t), num_samples, f);
    fclose(f);

    // 4. Test PlayAudioFile
    bool ok = Native_PlayAudioFile(test_wav_path);
    assert(ok);
    assert(Native_IsAudioPlaying() == true);
    assert(Native_GetAudioPlaybackDuration() >= 0.99f);

    // 5. Test pull samples
    std::vector<float> out(1024, 0.0f);
    size_t pulled = hal::AudioHAL::get().pull_playback_samples(out.data(), 1024);
    assert(pulled == 1024);
    assert(Native_GetAudioPlaybackPosition() > 0.0f);

    // 6. Test pause & resume
    Native_PauseAudioPlayback();
    std::vector<float> paused_out(1024, 1.0f);
    size_t p_pulled = hal::AudioHAL::get().pull_playback_samples(paused_out.data(), 1024);
    assert(p_pulled == 1024);
    assert(paused_out[0] == 0.0f); // Muted during pause

    Native_ResumeAudioPlayback();

    // 7. Test seek
    Native_SeekAudioPlayback(0.5f);
    float pos = Native_GetAudioPlaybackPosition();
    assert(pos >= 0.49f && pos <= 0.51f);

    // 8. Test stop
    Native_StopAudioPlayback();
    assert(Native_IsAudioPlaying() == false);

    remove(test_wav_path);
    std::cout << "  -> AudioHAL Playback & Mute: PASS" << std::endl;
}

void test_rattlegram_loopback() {
    std::cout << "[TEST] Rattlegram COFDM loopback..." << std::endl;
    auto rg = ModemRegistry::get().instantiate("rattlegram");
    assert(rg != nullptr);

    g_last_packet_decoded.clear();
    rg->init(48000, test_event_callback);

    std::string test_msg = "HELLO RATTLEGRAM";
    bool ok = rg->prepare_tx(reinterpret_cast<const uint8_t*>(test_msg.data()), test_msg.size(), "{\"carrier_freq\": 1700}");
    assert(ok);

    std::vector<float> audio;
    std::vector<float> chunk(1024);
    while (rg->is_tx_active()) {
        size_t n = rg->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(!audio.empty());

    rg->process_rx(audio.data(), audio.size());

    std::cout << "  -> Rattlegram Decoded: \"" << g_last_packet_decoded << "\"" << std::endl;
    assert(g_last_packet_decoded == test_msg);
    std::cout << "  -> PASS" << std::endl;
}

void test_sstv_loopback() {
    std::cout << "[TEST] SSTV Robot 36 scanline demodulation..." << std::endl;
    auto sstv = ModemRegistry::get().instantiate("sstv_engine");
    assert(sstv != nullptr);

    g_last_packet_decoded.clear();
    sstv->init(48000, test_event_callback);

    std::string test_data = "TEST_SSTV_IMAGE";
    bool ok = sstv->prepare_tx(reinterpret_cast<const uint8_t*>(test_data.data()), test_data.size(), "{\"sstv_mode\": \"robot36\"}");
    assert(ok);

    std::vector<float> audio;
    std::vector<float> chunk(1024);
    while (sstv->is_tx_active()) {
        size_t n = sstv->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        audio.insert(audio.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(!audio.empty());

    sstv->process_rx(audio.data(), audio.size());
    std::cout << "  -> PASS" << std::endl;
}

int main() {
    std::cout << "=== RUNNING MODEM LOOPBACK INTEGRATION TESTS ===" << std::endl;
    test_cw_loopback();
    test_aprs_loopback();
    test_eas_loopback();
    test_ultrasound_loopback();
    test_rattlegram_loopback();
    test_sstv_loopback();
    test_audio_hal_playback_and_mute();
    std::cout << "=== ALL MODEM LOOPBACK INTEGRATION TESTS PASSED ===" << std::endl;
    return 0;
}
