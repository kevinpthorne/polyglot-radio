#pragma once
#include <cstdint>
#include <cstddef>
#include "dart_api_dl.h"
#include "IModemEngine.h"

#ifndef DART_EXPORT
#if defined(_WIN32)
#define DART_EXPORT __declspec(dllexport)
#else
#define DART_EXPORT __attribute__((visibility("default")))
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 1. Dart API DL & Event Registration
DART_EXPORT intptr_t Native_InitDartApiDL(void* data);
DART_EXPORT void Native_RegisterEventPort(Dart_Port port);
DART_EXPORT void Native_RegisterEventCallback(EventCallback cb);

// 2. Audio HAL Lifecycle
DART_EXPORT bool Native_StartAudioHAL(int32_t sample_rate, bool enable_loopback);
DART_EXPORT void Native_StopAudioHAL();
DART_EXPORT bool Native_IsAudioHALRunning();
DART_EXPORT void Native_SetLoopbackMode(bool enabled);
DART_EXPORT void Native_SetSquelchThreshold(float threshold_db);
DART_EXPORT float Native_GetSquelchLevel();
DART_EXPORT bool Native_IsSquelchOpen();
DART_EXPORT void Native_SetOutputMuted(bool muted);
DART_EXPORT bool Native_IsOutputMuted();
DART_EXPORT void Native_SetInputMuted(bool muted);
DART_EXPORT bool Native_IsInputMuted();

// 3. Transmission & Synthesis
DART_EXPORT bool Native_StartTransmit(
    const char* protocol_id,
    const uint8_t* payload,
    size_t payload_len,
    const char* json_config
);
DART_EXPORT void Native_ConfigureModem(const char* modem_id, const char* json_config);
DART_EXPORT bool Native_IsTransmitting();
DART_EXPORT void Native_AbortTransmit();

// 3b. Audio File Playback (Recorded Transmissions)
DART_EXPORT bool Native_PlayAudioFile(const char* path);
DART_EXPORT void Native_PauseAudioPlayback();
DART_EXPORT void Native_ResumeAudioPlayback();
DART_EXPORT void Native_StopAudioPlayback();
DART_EXPORT bool Native_IsAudioPlaying();
DART_EXPORT float Native_GetAudioPlaybackPosition();
DART_EXPORT float Native_GetAudioPlaybackDuration();
DART_EXPORT void Native_SeekAudioPlayback(float seconds);

// 4. Shared Canvas Framebuffer (Zero-Copy 640x496 RGBA32)
DART_EXPORT uint8_t* Native_GetSharedCanvasPtr();
DART_EXPORT size_t Native_GetSharedCanvasSize();
DART_EXPORT void Native_ClearCanvas(uint32_t argb_fill);

// 5. Real-Time Rolling FFT Spectrogram
DART_EXPORT void Native_GetFFTMagnitudes(float* out_magnitudes, size_t count);

// 6. Offline Reprocessing & Storage
DART_EXPORT bool Native_ReprocessRecording(
    const char* wav_path,
    const char* target_modem_id,
    const char* json_params
);
DART_EXPORT void Native_SetStorageDirectory(const char* dir_path);

// 7. Test / Ingestion Simulation
DART_EXPORT void Native_InjectAudioSamples(const float* samples, size_t count);

// Dispatch helper for modems to notify Dart
void DispatchToDart(const NativeModemEvent* ev);

#ifdef __cplusplus
}
#endif
