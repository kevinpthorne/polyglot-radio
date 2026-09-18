#include "native_bridge.h"
#include "audio_hal.h"
#include "sentry_coordinator.h"
#include "modem_registry.h"
#include "dart_api_dl.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

static uint8_t* g_shared_canvas = nullptr;
static const size_t g_canvas_size = 640 * 496 * 4; // 1,269,760 bytes

static Dart_Port g_event_port = 0;
static EventCallback g_native_event_callback = nullptr;

void DispatchToDart(const NativeModemEvent* ev) {
    if (!ev) return;

    // 1. Direct native callback if registered
    if (g_native_event_callback) {
        g_native_event_callback(ev);
    }

    // 2. Dart_PostCObject_DL port dispatch if initialized
    if (g_event_port != 0) {
        Dart_CObject c_type;
        c_type.type = Dart_CObject_kInt32;
        c_type.value.as_int32 = ev->event_type;

        Dart_CObject c_proto;
        c_proto.type = Dart_CObject_kString;
        c_proto.value.as_string = const_cast<char*>(ev->protocol_id ? ev->protocol_id : "");

        Dart_CObject c_snr;
        c_snr.type = Dart_CObject_kDouble;
        c_snr.value.as_double = static_cast<double>(ev->snr_db);

        Dart_CObject c_meta;
        c_meta.type = Dart_CObject_kInt32;
        c_meta.value.as_int32 = ev->metadata_int;

        Dart_CObject c_payload;
        c_payload.type = Dart_CObject_kTypedData;
        c_payload.value.as_typed_data.type = Dart_TypedData_kUint8;
        c_payload.value.as_typed_data.length = ev->payload_len;
        c_payload.value.as_typed_data.values = const_cast<uint8_t*>(ev->payload ? ev->payload : reinterpret_cast<const uint8_t*>(""));

        Dart_CObject* array_values[] = { &c_type, &c_proto, &c_snr, &c_meta, &c_payload };
        Dart_CObject root;
        root.type = Dart_CObject_kArray;
        root.value.as_array.length = 5;
        root.value.as_array.values = array_values;

        Dart_PostCObject_DL(g_event_port, &root);
    }
}

extern "C" {

DART_EXPORT intptr_t Native_InitDartApiDL(void* data) {
    return Dart_InitializeApiDL(data);
}

DART_EXPORT void Native_RegisterEventPort(Dart_Port port) {
    g_event_port = port;
}

DART_EXPORT void Native_RegisterEventCallback(EventCallback cb) {
    g_native_event_callback = cb;
}

DART_EXPORT bool Native_StartAudioHAL(int32_t sample_rate, bool enable_loopback) {
    core::SentryCoordinator::get().init(sample_rate, DispatchToDart);

    hal::AudioHAL::get().set_rx_callback([](const float* samples, size_t count) {
        core::SentryCoordinator::get().process_audio(samples, count);
    });

    return hal::AudioHAL::get().init(sample_rate, enable_loopback);
}

DART_EXPORT void Native_StopAudioHAL() {
    hal::AudioHAL::get().shutdown();
    core::SentryCoordinator::get().reset();
}

DART_EXPORT bool Native_IsAudioHALRunning() {
    return hal::AudioHAL::get().is_running();
}

DART_EXPORT void Native_SetLoopbackMode(bool enabled) {
    hal::AudioHAL::get().set_loopback_mode(enabled);
}

DART_EXPORT void Native_SetSquelchThreshold(float threshold_db) {
    hal::AudioHAL::get().set_squelch_threshold_db(threshold_db);
}

DART_EXPORT float Native_GetSquelchLevel() {
    return hal::AudioHAL::get().get_current_rms_db();
}

DART_EXPORT bool Native_IsSquelchOpen() {
    return hal::AudioHAL::get().is_squelch_open();
}

DART_EXPORT void Native_SetOutputMuted(bool muted) {
    hal::AudioHAL::get().set_output_muted(muted);
}

DART_EXPORT bool Native_IsOutputMuted() {
    return hal::AudioHAL::get().is_output_muted();
}

DART_EXPORT void Native_SetInputMuted(bool muted) {
    hal::AudioHAL::get().set_input_muted(muted);
}

DART_EXPORT bool Native_IsInputMuted() {
    return hal::AudioHAL::get().is_input_muted();
}

DART_EXPORT bool Native_StartTransmit(
    const char* protocol_id,
    const uint8_t* payload,
    size_t payload_len,
    const char* json_config
) {
    if (!protocol_id) return false;
    static const uint8_t s_dummy_byte = 0;
    if (!payload || payload_len == 0) {
        payload = &s_dummy_byte;
        payload_len = 1;
    }

    auto modem = ModemRegistry::get().instantiate(protocol_id);
    if (!modem) return false;

    modem->init(hal::kSampleRate, DispatchToDart);
    if (!modem->prepare_tx(payload, payload_len, json_config)) {
        return false;
    }

    // Pull all generated TX samples into buffer
    std::vector<float> full_tx;
    std::vector<float> chunk(4096);
    while (modem->is_tx_active()) {
        size_t n = modem->pull_tx(chunk.data(), chunk.size());
        if (n == 0) break;
        full_tx.insert(full_tx.end(), chunk.begin(), chunk.begin() + n);
    }

    // Queue for playback / loopback
    return hal::AudioHAL::get().queue_tx_samples(full_tx.data(), full_tx.size());
}

DART_EXPORT void Native_ConfigureModem(const char* modem_id, const char* json_config) {
    if (modem_id) {
        core::SentryCoordinator::get().configure_modem(modem_id, json_config);
    }
}

DART_EXPORT bool Native_IsTransmitting() {
    return hal::AudioHAL::get().is_tx_active();
}

DART_EXPORT void Native_AbortTransmit() {
    hal::AudioHAL::get().abort_tx();
}

DART_EXPORT bool Native_PlayAudioFile(const char* path) {
    if (!path) return false;
    return hal::AudioHAL::get().play_audio_file(path);
}

DART_EXPORT void Native_PauseAudioPlayback() {
    hal::AudioHAL::get().pause_audio_playback();
}

DART_EXPORT void Native_ResumeAudioPlayback() {
    hal::AudioHAL::get().resume_audio_playback();
}

DART_EXPORT void Native_StopAudioPlayback() {
    hal::AudioHAL::get().stop_audio_playback();
}

DART_EXPORT bool Native_IsAudioPlaying() {
    return hal::AudioHAL::get().is_audio_playing();
}

DART_EXPORT float Native_GetAudioPlaybackPosition() {
    return hal::AudioHAL::get().get_audio_playback_position();
}

DART_EXPORT float Native_GetAudioPlaybackDuration() {
    return hal::AudioHAL::get().get_audio_playback_duration();
}

DART_EXPORT void Native_SeekAudioPlayback(float seconds) {
    hal::AudioHAL::get().seek_audio_playback(seconds);
}

DART_EXPORT uint8_t* Native_GetSharedCanvasPtr() {
    if (!g_shared_canvas) {
        g_shared_canvas = static_cast<uint8_t*>(calloc(1, g_canvas_size));
    }
    return g_shared_canvas;
}

DART_EXPORT size_t Native_GetSharedCanvasSize() {
    return g_canvas_size;
}

DART_EXPORT void Native_ClearCanvas(uint32_t argb_fill) {
    if (!g_shared_canvas) return;
    uint32_t* pixels = reinterpret_cast<uint32_t*>(g_shared_canvas);
    for (size_t i = 0; i < (g_canvas_size / 4); ++i) {
        pixels[i] = argb_fill;
    }
}

DART_EXPORT void Native_GetFFTMagnitudes(float* out_magnitudes, size_t count) {
    if (out_magnitudes && count > 0) {
        hal::AudioHAL::get().get_latest_fft(out_magnitudes, count);
    }
}

DART_EXPORT bool Native_ReprocessRecording(
    const char* wav_path,
    const char* target_modem_id,
    const char* json_params
) {
    return core::SentryCoordinator::get().reprocess_wav(wav_path, target_modem_id, json_params);
}

DART_EXPORT void Native_SetStorageDirectory(const char* dir_path) {
    if (dir_path) {
        core::SentryCoordinator::get().set_storage_directory(dir_path);
    }
}

DART_EXPORT void Native_InjectAudioSamples(const float* samples, size_t count) {
    if (samples && count > 0) {
        hal::AudioHAL::get().inject_rx_samples(samples, count);
    }
}

} // extern "C"
