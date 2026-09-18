# RFC-003: Native FFI Interface, Zero-Copy Shared Canvas, & Virtual Loopback Audio HAL

**Status:** Approved  
**Author:** Polyglot Radio Architecture Team  
**Updated:** 2026-09-18  

---

## 1. Abstract

This RFC defines the cross-boundary Foreign Function Interface (FFI) contract, zero-copy shared memory architecture, asynchronous event dispatching mechanism, and the headless virtual loopback audio HAL for automated continuous testing.

---

## 2. C-ABI Exported API

```c
#ifdef __cplusplus
extern "C" {
#endif

// 1. Dart API DL Initialization
DART_EXPORT intptr_t Native_InitDartApiDL(void* data);

// 2. Event Port Registration
DART_EXPORT void Native_RegisterEventPort(Dart_Port port);

// 3. Audio HAL Lifecycle
DART_EXPORT bool Native_StartAudioHAL(int32_t sample_rate, bool enable_loopback);
DART_EXPORT void Native_StopAudioHAL();
DART_EXPORT bool Native_IsAudioHALRunning();
DART_EXPORT void Native_SetLoopbackMode(bool enabled);
DART_EXPORT void Native_SetSquelchThreshold(float threshold_db);

// 4. Transmission & Playback
DART_EXPORT bool Native_StartTransmit(
    const char* protocol_id,
    const uint8_t* payload,
    size_t payload_len,
    const char* json_config
);
DART_EXPORT bool Native_IsTransmitting();
DART_EXPORT void Native_AbortTransmit();

// 5. Shared Canvas Framebuffer (Zero-Copy)
DART_EXPORT uint8_t* Native_GetSharedCanvasPtr();
DART_EXPORT size_t Native_GetSharedCanvasSize();
DART_EXPORT void Native_ClearCanvas(uint32_t argb_fill);

// 6. Real-Time FFT Spectrogram Stream
DART_EXPORT void Native_GetFFTMagnitudes(float* out_magnitudes, size_t count);

// 7. Offline Reprocessing
DART_EXPORT bool Native_ReprocessRecording(
    const char* wav_path,
    const char* target_modem_id,
    const char* json_params
);

// 8. Test / Simulation Hooks
DART_EXPORT void Native_InjectAudioSamples(const float* samples, size_t count);

#ifdef __cplusplus
}
#endif
```

---

## 3. Zero-Copy Shared Canvas Architecture

To render full-resolution analog visual/facsimile scanlines (SSTV Martin/Scottie/Robot/PD-120 and WEFAX 120 LPM) without memory allocation spikes:
* **Heap Ownership:** The C++ native core allocates a persistent buffer sized for the maximum resolution:
$$640\text{ (width)} \times 496\text{ (height)} \times 4\text{ (RGBA32 bytes)} = 1{,}269{,}760\text{ bytes}$$
* **Pointer Projection:** Dart retrieves `Native_GetSharedCanvasPtr()` once upon initialization and projects a `Uint8List.view` over the native memory address:
```dart
final Pointer<Uint8> ptr = Native_GetSharedCanvasPtr();
final Uint8List canvasView = ptr.asTypedList(640 * 496 * 4);
```
* **Dirty Line Signaling:** When a demodulator completes a raster line, it dispatches an event via `Dart_PostCObject_DL` with `EventType::RasterLineReady` and the line index in `metadata_int`. The Flutter UI triggers `decodeImageFromPixels` or updates the texture without serializing any image bytes over FFI.

---

## 4. Headless Virtual Loopback Audio HAL

For headless continuous integration and automated test environments where audio hardware is unavailable:
1. **Software Loopback Bridge:**
   * When `enable_loopback` is true, the native core bridges the audio transmitter directly to the receiver.
   * As `pull_tx` generates audio frames, identical float samples are synchronously pushed into the receive SPSC ring buffer.
   * The DSP worker thread processes these samples through the sentry array, evaluates squelch, triggers autonomous promotion, and streams the decoded output back to Dart.
2. **Deterministic Sample Injection:**
   * `Native_InjectAudioSamples(const float* samples, size_t count)` allows test runners to push exact synthesized audio buffers directly into the DSP ingest pipeline at controlled speeds, enabling 100% deterministic test execution across units, integrations, and E2E scenarios.
