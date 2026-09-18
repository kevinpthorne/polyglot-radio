# Polyglot Radio: Universal Acoustic Software-Defined Transceiver & Modem Framework

**Engineering Specification Document**
**Version:** 1.0.0-PROD

**Target Platforms:** iOS 16.0+, Android 10.0+ (API Level 29+), macOS 13.0+, Linux (Debian 12+)

**Primary Tech Stack:** Flutter (Dart 3.x), C++20, `dart:ffi`, `miniaudio`, SQLite

---

## 1. Executive Summary & Architecture Overview

Polyglot Radio is a multi-mode acoustic software-defined modem that uses a device's built-in loudspeaker and microphone to transmit and receive digital, analog, and facsimile radio protocols over sound. The system operates entirely across audible and near-ultrasonic acoustic channels (300 Hz – 20 kHz), requiring no external RF hardware or Software-Defined Radio (SDR) peripherals.

### 1.1 High-Level Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           FLUTTER UI LAYER                              │
│  ┌───────────────────────┐ ┌──────────────────┐ ┌────────────────────┐  │
│  │   Home Chat Logbook   │ │ Waterfall Visual │ │  Bottom Composer   │  │
│  │ (Dynamic Mode Bubbles)│ │ (FFT Spectrogram)│ │(Mode-Filtered TX)  │  │
│  └───────────▲───────────┘ └────────▲─────────┘ └─────────┬──────────┘  │
│              │                      │                     │             │
│              │ Drift/SQLite         │ CustomPainter       │ FFI Dispatch│
│  ┌───────────┴──────────────────────┴─────────────────────▼──────────┐  │
│  │                      ModemCoordinator (Dart)                      │  │
│  │  - Protocol Plugin Registry                                       │  │
│  │  - Zero-Copy Buffer Binding                                       │  │
│  │  - Audio Asset & Lifecycle Manager                                │  │
│  └──────────────────────────────────▲────────────────────────────────┘  │
└─────────────────────────────────────┼───────────────────────────────────┘
                         dart:ffi     │ C-ABI / Dart_PostCObject_DL
┌─────────────────────────────────────▼───────────────────────────────────┐
│                         NATIVE CORE (C++20)                             │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                       Native Modem Engine                         │  │
│  │   ┌─────────────────────┐               ┌─────────────────────┐   │  │
│  │   │   Sentry Array      │               │ Active Demodulator  │   │  │
│  │   │ (Parallel Detectors)│               │  (Promoted Engine)  │   │  │
│  │   └──────────▲──────────┘               └──────────▲──────────┘   │  │
│  │              │                                     │              │  │
│  │   ┌──────────┴─────────────────────────────────────┴──────────┐   │  │
│  │   │        Modem Registry & Dynamic Strategy Router           │   │  │
│  │   └──────────────────────────────▲────────────────────────────┘   │  │
│  └──────────────────────────────────┼────────────────────────────────┘  │
│                                     │ Read/Write Raw PCM                │
│  ┌──────────────────────────────────┴────────────────────────────────┐  │
│  │                    Audio HAL & Buffering Engine                   │  │
│  │   ┌───────────────────────────┐     ┌─────────────────────────┐   │  │
│  │   │ SPSC Ring Pre-Buffer (5s) │     │ Squelch / WAV Slicer    │   │  │
│  │   └──────────▲────────────────┘     └────────────┬────────────┘   │  │
│  │              │                                   │                │  │
│  │   ┌──────────┴───────────────────────────────────▼────────────┐   │  │
│  │   │          miniaudio Core (48 kHz / 16-bit Mono / Raw)      │   │  │
│  │   └──────────────────────────────▲────────────────────────────┘   │  │
│  └──────────────────────────────────┼────────────────────────────────┘  │
└─────────────────────────────────────┼───────────────────────────────────┘
                                      │ Audio I/O
                          ┌───────────┴───────────┐
                          │ Hardware Mic/Speaker  │
                          └───────────────────────┘

```

### 1.2 Threading & Execution Invariants

1. **Audio Driver Thread (Real-Time Priority):** Owned by `miniaudio`. Performs zero heap allocations, zero file I/O, and zero mutex locks. Pushes raw PCM samples into a lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
2. **DSP Worker Thread (High Priority):** Owned by Native C++. Pulls samples from the SPSC buffer, executes continuous tone tracking/sentry correlation, runs the active demodulator, computes 512-point FFT frames for the UI waterfall, and pipes raw audio into the WAV stream slicer.
3. **Dart UI Isolate (Platform Main Thread):** Handles Flutter rendering, UI layout, user input, and SQLite query orchestration.
4. **Dart Background Task Worker (Background Isolate):** Handles audio file compression (WAV to Opus/FLAC transcoding) and database maintenance.

---

## 2. Audio Hardware & Platform Abstraction Layer (HAL)

Acoustic modems require bit-accurate, linear PCM samples. Modern mobile operating systems treat audio as speech by default, applying software and hardware filtering that corrupts digital audio subcarriers. The HAL must explicitly strip these out.

### 2.1 Native Audio Session Invariants

* **Sample Rate:** Fixed at $48{,}000\text{ Hz}$ (native internal DAC/ADC rate on modern smartphones).
* **Channel Configuration:** 1 Channel (Mono), 32-bit Floating-Point internally for DSP, 16-bit Signed Integer PCM for disk storage.
* **Buffer Size:** 256 or 512 frames ($5.33\text{ ms}$ or $10.66\text{ ms}$ latency).

#### iOS Platform Implementation (`AVAudioSession`)

```objc
AVAudioSession *session = [AVAudioSession sharedInstance];
NSError *error = nil;

// Enforce raw measurement mode to kill AGC, high-pass speech filters, and beamforming
[session setCategory:AVAudioSessionCategoryPlayAndRecord
                mode:AVAudioSessionModeMeasurement
             options:AVAudioSessionCategoryOptionDefaultToSpeaker | 
                     AVAudioSessionCategoryOptionAllowBluetooth
               error:&error];

[session setPreferredSampleRate:48000.0 error:&error];
[session setPreferredIOBufferDuration:0.00533 error:&error]; // 256 samples
[session setActive:YES error:&error];

```

#### Android Platform Implementation (`AAudio` via `miniaudio`)

```c
ma_device_config config = ma_device_config_init(ma_device_type_duplex);
config.sampleRate = 48000;
config.capture.format = ma_format_f32;
config.capture.channels = 1;
config.playback.format = ma_format_f32;
config.playback.channels = 1;
config.performanceProfile = ma_performance_profile_low_latency;
config.dataCallback = native_audio_callback;

// Under AAudio backend:
// AAUDIO_CONTENT_TYPE_MUSIC (Prevents voice bandpass clipping)
// AAUDIO_USAGE_MEDIA
// aaudiostream_setNoiseSuppressorEnabled(stream, false);
// aaudiostream_setAcousticEchoCancelerEnabled(stream, false);

```

### 2.2 Squelch, SPSC Ring Buffer, and WAV Stream Slicer

To ensure no preambles are missed while avoiding gigabytes of dead-air recordings, the native engine maintains a **Circular Pre-Trigger Buffer**:

1. Incoming audio is continuously written into a $5\text{-second}$ (240,000 float) memory-mapped ring buffer.
2. A dual-state squelch circuit monitors root-mean-square (RMS) energy:

$$\text{RMS}_{\text{dBFS}} = 20 \log_{10}\left(\sqrt{\frac{1}{N}\sum_{i=0}^{N-1} x[i]^2}\right)$$


3. **Trigger Event:** Squelch trips open if signal energy exceeds noise floor by $+6\text{ dB}$, or any Tier-1 protocol sentry indicates correlation confidence $>0.70$.
4. **Recording Generation:** The engine creates an empty WAV container, prepends the preceding $3.0\text{ seconds}$ of audio from the ring buffer, and streams incoming audio to the file until carrier loss is declared.
5. **Hangover Timer:** Recording continues for $1.5\text{ seconds}$ after energy drops below threshold to avoid clipping trailing sync or stop pulses.

---

## 3. Native DSP Engine & Dynamic Plugin Registry

### 3.1 Plugin Native Interface (`IModemEngine.h`)

Every protocol modem implements this pure virtual base class:

```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>

enum class EventType : int32_t {
    CarrierDetected = 1,
    CarrierLost     = 2,
    PacketDecoded   = 3,
    TextStream      = 4,
    RasterLineReady = 5,
    StatusUpdate    = 6
};

#pragma pack(push, 1)
struct NativeModemEvent {
    int32_t event_type;       // EventType
    const char* protocol_id;  // null-terminated string
    float snr_db;             // Signal-to-noise ratio
    int32_t center_freq;      // Frequency offset (Hz)
    const uint8_t* payload;   // Data buffer
    size_t payload_len;       // Buffer size in bytes
    int32_t metadata_int;     // Line index for raster, or baud rate
};
#pragma pack(pop)

using EventCallback = void(*)(const NativeModemEvent*);

class IModemEngine {
public:
    virtual ~IModemEngine() = default;

    virtual const char* get_id() const = 0;
    virtual const char* get_display_name() const = 0;
    virtual int get_sample_rate() const = 0;

    virtual void init(int sample_rate, EventCallback callback) = 0;
    virtual void reset() = 0;

    // Real-time Audio Ingest
    virtual void process_rx(const float* samples, size_t count) = 0;

    // Preamble / Quick Correlator for Sentry Array
    virtual float check_sentry_confidence(const float* samples, size_t count) = 0;

    // Transmission Modulation
    virtual bool prepare_tx(const uint8_t* payload, size_t len, const char* json_config) = 0;
    virtual size_t pull_tx(float* output, size_t max_samples) = 0;
    virtual bool is_tx_active() const = 0;
};

```

### 3.2 Registry and Registration Macro

```cpp
#include <unordered_map>
#include <memory>

class ModemRegistry {
public:
    using Factory = std::function<std::unique_ptr<IModemEngine>()>;
    static ModemRegistry& get() {
        static ModemRegistry instance;
        return instance;
    }
    void register_modem(const std::string& id, Factory factory) {
        factories_[id] = factory;
    }
    std::unique_ptr<IModemEngine> instantiate(const std::string& id) {
        if (factories_.find(id) != factories_.end()) return factories_[id]();
        return nullptr;
    }
    const std::unordered_map<std::string, Factory>& all() const { return factories_; }
private:
    std::unordered_map<std::string, Factory> factories_;
};

#define REGISTER_MODEM(CLASS_NAME, ID_STR) \
    static bool _reg_##CLASS_NAME = []() { \
        ModemRegistry::get().register_modem(ID_STR, []() { \
            return std::make_unique<CLASS_NAME>(); \
        }); \
        return true; \
    }();

```

---

## 4. Cross-Boundary Memory & FFI Bridge

To prevent memory leaks and minimize garbage collection pauses, the bridge uses two memory passing designs:

### 4.1 Zero-Copy Framebuffer for Raster/Visual Modes (SSTV, WEFAX, Hell)

Passing full-resolution image scanlines over Dart ports causes frame drops. The C++ heap owns a shared, double-buffered RGBA32 canvas.

```cpp
// native_bridge.cpp
static uint8_t* g_shared_canvas = nullptr;
static size_t g_canvas_size = 640 * 496 * 4; // Sized for max resolution (PD-120 / WEFAX)

extern "C" {
    DART_EXPORT uint8_t* Native_GetSharedCanvasPtr() {
        if (!g_shared_canvas) {
            g_shared_canvas = static_cast<uint8_t*>(calloc(1, g_canvas_size));
        }
        return g_shared_canvas;
    }

    DART_EXPORT void Native_ClearCanvas(uint32_t argb_fill) {
        if (!g_shared_canvas) return;
        uint32_t* pixels = reinterpret_cast<uint32_t*>(g_shared_canvas);
        for (size_t i = 0; i < (g_canvas_size / 4); ++i) {
            pixels[i] = argb_fill;
        }
    }
}

```

**Dart-Side Canvas Mapping (`raster_bridge.dart`):**

```dart
import 'dart:ffi';
import 'dart:typed_data';
import 'dart:ui' as ui;

class RasterBridge {
  static final RasterBridge instance = RasterBridge._internal();
  RasterBridge._internal();

  late final Pointer<Uint8> _nativePtr;
  late final Uint8List canvasView;

  void initialize(DynamicLibrary lib) {
    final Pointer<Uint8> Function() getPtr = lib
        .lookup<NativeFunction<Pointer<Uint8> Function()>>('Native_GetSharedCanvasPtr')
        .asFunction();
    _nativePtr = getPtr();
    // Zero-copy: Direct memory projection over C++ heap
    canvasView = _nativePtr.asTypedList(640 * 496 * 4);
  }

  void paintLineToUi(int lineIdx, int width, int height, Function(ui.Image) onFrameReady) {
    ui.decodeImageFromPixels(
      canvasView,
      width,
      height,
      ui.PixelFormat.rgba8888,
      onFrameReady,
    );
  }
}

```

### 4.2 Asynchronous Event Dispatch (`Dart_PostCObject_DL`)

The DSP thread dispatches discrete packets, decoded text tokens, telemetry, and line sync notifications to Dart using `Dart_PostCObject_DL`:

```cpp
static Dart_Port g_event_port = 0;

extern "C" DART_EXPORT void Native_RegisterEventPort(Dart_Port port) {
    g_event_port = port;
}

void DispatchToDart(const NativeModemEvent& ev) {
    if (g_event_port == 0) return;

    Dart_CObject c_type;
    c_type.type = Dart_CObject_kInt32;
    c_type.value.as_int32 = ev.event_type;

    Dart_CObject c_proto;
    c_proto.type = Dart_CObject_kString;
    c_proto.value.as_string = const_cast<char*>(ev.protocol_id);

    Dart_CObject c_snr;
    c_snr.type = Dart_CObject_kDouble;
    c_snr.value.as_double = static_cast<double>(ev.snr_db);

    Dart_CObject c_meta;
    c_meta.type = Dart_CObject_kInt32;
    c_meta.value.as_int32 = ev.metadata_int;

    Dart_CObject c_payload;
    c_payload.type = Dart_CObject_kTypedData;
    c_payload.value.as_typed_data.type = Dart_TypedData_kUint8;
    c_payload.value.as_typed_data.length = ev.payload_len;
    c_payload.value.as_typed_data.values = const_cast<uint8_t*>(ev.payload);

    Dart_CObject* array_values[] = { &c_type, &c_proto, &c_snr, &c_meta, &c_payload };
    Dart_CObject root;
    root.type = Dart_CObject_kArray;
    root.value.as_array.length = 5;
    root.value.as_array.values = array_values;

    Dart_PostCObject_DL(g_event_port, &root);
}

```

---

## 5. Protocol Engine Matrix & Implementation Details

The framework includes 11 protocol modem implementations:

| Protocol Engine ID | Physical Layer Modulation | Carrier / Subcarriers | Baud / Bitrate | Frame / Synchronization Pattern |
| --- | --- | --- | --- | --- |
| `sstv_engine` | Analog Subcarrier FM | 1200 Hz Sync, 1500–2300 Hz Lum/Chroma | 1200–2300 Hz sweeps | VIS Header (300 ms 1900 Hz, 10 ms 1200 Hz, 7-bit FSK @ 33.3 baud) |
| `rattlegram` | COFDM (QPSK / 16-QAM) | 88 subcarriers, 1.2–2.2 kHz | ~200–1200 bps | Cyclic prefix autocorrelation (Schmidl-Cox sync) |
| `aprs_packet` | AFSK (Bell 202) | 1200 Hz (Mark), 2200 Hz (Space) | 1200 baud | HDLC Flag sequences (`0x7E`), NRZI, CRC-16-CCITT |
| `eas_same` | AFSK | 2083.3 Hz (Mark), 1562.5 Hz (Space) | 520.83 baud | Preamble byte sequence (`0xAB 0xAB 0xAB 0xAB`) |
| `cw_morse` | OOK (On-Off Keying) | Auto-track (400–1000 Hz, default 700 Hz) | 5–45 WPM adaptive | PARIS Dit-length tracking, Schmitt-trigger envelope |
| `psk31` | BPSK (Raised Cosine) | Dynamic center (default 1000 Hz) | 31.25 baud | Varicode framing, 180° phase transitions, Costas loop |
| `feld_hell` | OOK / AM Facsimile | 980 Hz baseband tone | 122.5 baud (pixel clock) | 14-pixel vertical raster scanning, 122.5 Hz dot clock |
| `olivia_mfsk` | MFSK (Walsh Matrix) | 8 or 16 orthogonal tones (500 Hz BW) | 31.25 baud | Fast Walsh-Hadamard correlation peak ($>3.5\sigma$) |
| `hf_wefax` | Analog Subcarrier FM | 1500 Hz (Black) to 2300 Hz (White) | 120 lines/minute | 1200 Hz Start tone (5s), 5 ms phasing pulse, 450 Hz Stop (5s) |
| `ft8_engine` | 8-GFSK | 8 tones spaced @ 6.25 Hz (50 Hz BW) | 6.25 baud | 7x7 Costas array sync pattern, 15.0s time slots, LDPC(174,87) |
| `ultrasound` | Inaudible Multi-FSK | 18.5 kHz – 19.8 kHz | ~120 bps | Chirp preamble (18 to 20 kHz up-chirp), Reed-Solomon |

### 5.1 SSTV Protocol Specification

* **FM Discriminator:** Computed via complex instantaneous phase differentiation:

$$f(t) = \frac{f_s}{2\pi} \cdot \text{Arg}\left( x[n] \cdot x^*[n-1] \right)$$



Normalized frequency output maps linearly from $1500\text{ Hz} \to 0$ to $2300\text{ Hz} \to 255$.
* **Unified VIS Decoder:** Decodes modes automatically. Supports:
* **Martin 1 / Martin 2:** G-B-R sequential. 320x256. 1200 Hz line sync ($4.862\text{ ms}$) + 1500 Hz sync porch ($0.572\text{ ms}$).
* **Scottie 1 / Scottie 2 / Scottie DX:** G-B-R sequential. 320x256. 1200 Hz line sync pulse ($9.0\text{ ms}$) inserted between Blue and Red scans.
* **Robot 36 / Robot 72:** Y/C component scan. 320x240. Robot 36 sends full luminance scan ($88\text{ ms}$), then alternating $R-Y$ ($44\text{ ms}$) and $B-Y$ ($44\text{ ms}$) scans.
* **PD-50 / PD-120 / PD-180 (ISS Standard):** High-resolution component scan. Decodes dual luminance lines paired with differential chrominance. PD-120 outputs 640x496 pixels.


* **Slant Correction:** Tracks horizontal sync edge arrival times using a Phase-Locked Loop (PLL). Samples per line are dynamically resampled using linear interpolation:

$$x_{\text{corrected}}[t] = x\left[t \cdot \left(1 + \Delta_{\text{drift}}\right)\right]$$



### 5.2 Rattlegram (COFDM) Engine

* **Modulation:** 88 active subcarriers modulated in QPSK or 16-QAM within an 1800 Hz bandwidth.
* **Preamble Sync (Schmidl-Cox):**
Uses two identical half-symbols in the time domain. Conjugate correlation metric $M(d)$:

$$P(d) = \sum_{m=0}^{L-1} (r_{d+m}^* \cdot r_{d+m+L}), \quad R(d) = \sum_{m=0}^{L-1} \vert{}r_{d+m+L}\vert{}^2, \quad M(d) = \frac{\vert{}P(d)\vert{}^2}{(R(d))^2}$$



A peak where $M(d) > 0.85$ locks symbol timing and estimates initial Carrier Frequency Offset (CFO).
* **Channel Equalization:** Pilot carriers interspersed every 4th subcarrier estimate multi-path impulse response and room echo transfer function $H(f)$. Corrected via Single-Tap Frequency-Domain Equalizer:

$$\hat{S}_{k} = \frac{Y_{k}}{H_{k}}$$



### 5.3 Continuous Visual & Text Modes (CW, Feld Hell, PSK31)

* **CW Morse:**
* Bandpass filter: 48-tap FIR centered at auto-tracked pitch (400–1000 Hz).
* Adaptive Slicer: Dynamically maintains moving thresholds $T_{\text{high}} = 0.4 \cdot A_{\text{peak}}$, $T_{\text{low}} = 0.2 \cdot A_{\text{peak}}$.
* Symbol Tokenizer: Decodes dot/dash intervals into binary trie indices, emitting ASCII tokens via `EventType::TextStream`.


* **Feld Hell:**
* Envelope detector outputs to a 14-pixel vertical column buffer at a $122.5\text{ Hz}$ column clock.
* Noise Gate Metric: Dot-clock bandpass filter running at $122.5\text{ Hz}$. Output only paints to canvas if energy at $122.5\text{ Hz}$ exceeds adjacent spectrum by $+8\text{ dB}$.


* **PSK31:**
* Quadrature downconversion to baseband $I/Q$.
* Costas Loop recovers carrier phase: $\text{Error} = I \cdot Q \cdot \text{sign}(I^2 - Q^2)$.
* Matched filter: Root-Raised Cosine ($\alpha = 0.5$, symbol rate $31.25\text{ Hz}$).
* Bit Decimation: Extracts bits at zero-crossings and parses through Varicode state machine.



---

## 6. Sentry Array & Autonomous Promotion Engine

To decode signals hands-free without forcing the user to select a mode, the native layer runs an autonomous **Two-Stage Sentry System**:

```
               Incoming Float Stream (48 kHz PCM)
                                │
       ┌────────────────────────┼────────────────────────┐
       ▼                        ▼                        ▼
[SSTV Sentry]          [Rattlegram Sentry]        [APRS Sentry]
(1900 Hz Goertzel)     (Schmidl-Cox Metric)       (Bell 202 Demod)
       │                        │                        │
       └────────────────────────┼────────────────────────┘
                                │ Confidence > Threshold
                                ▼
                 [Engine Coordinator Lockout]
                 1. Mute all competing sentries
                 2. Attach active demodulator to buffer
                 3. Prepend 3s from circular RAM
                 4. Stream payload to Dart EventPort
                                │
                    [Signal Lost / EOT / Timeout]
                                │
                                ▼
                 [Re-enable Parallel Sentries]

```

### Sentry Activation Matrix

1. **SSTV:** Goertzel filter evaluates magnitude at $1900\text{ Hz}$. If magnitude exceeds baseline by $+14\text{ dB}$ for $\ge 250\text{ ms}$, sentry flags lock and awaits the $1200\text{ Hz}$ break.
2. **Rattlegram:** Continuous sliding autocorrelator checks for the $L=128$ sample identical halves. $M(d) > 0.85$ trips lock.
3. **APRS / AX.25:** Delay-and-multiply AFSK demodulator searches for consecutive flags (`01111110`). Two valid flags trip lock.
4. **EAS / SAME:** Mark ($2083.3\text{ Hz}$) and Space ($1562.5\text{ Hz}$) tone detectors evaluate bit-clock transitions. Sync byte sequence `0xAB 0xAB` trips lock.
5. **Continuous Unstructured Fallback (CW / Hell / PSK31):** Evaluated continuously. Text or rasters are held in a 6-character sanity buffer; if ASCII or rhythm confidence passes validation, a card appears in the chat feed.

---

## 7. Storage, Data Model, & Audio Archival Engine

Every received or transmitted signal is saved as an immutable database entity coupled to a raw 16-bit mono WAV file.

### 7.1 SQLite Database Schema (via Drift)

```sql
CREATE TABLE transmissions (
    id TEXT PRIMARY KEY NOT NULL,            -- UUID v4
    timestamp INTEGER NOT NULL,              -- Unix Epoch Milliseconds
    direction INTEGER NOT NULL,              -- 0 = RX (Incoming), 1 = TX (Outgoing)
    protocol_id TEXT NOT NULL,               -- e.g., 'sstv_robot36', 'rattlegram'
    protocol_display_name TEXT NOT NULL,     -- e.g., 'Robot 36', 'Rattlegram'
    payload_type INTEGER NOT NULL,           -- 0 = Text, 1 = Image, 2 = Binary, 3 = Unknown
    text_content TEXT,                       -- Decoded text message / callsign / APRS info
    image_file_path TEXT,                    -- Saved PNG file if SSTV/WEFAX
    raw_payload BLOB,                        -- Raw bytes of packet
    snr_db REAL,                             -- Estimated signal-to-noise ratio
    duration_ms INTEGER NOT NULL,            -- Audio burst length in ms
    audio_file_path TEXT NOT NULL,           -- Local absolute path to .wav recording
    is_identified INTEGER NOT NULL DEFAULT 1 -- 0 = Unidentified burst
);

CREATE INDEX idx_transmissions_timestamp ON transmissions(timestamp DESC);

```

### 7.2 Offline Re-Demodulation Engine

When a signal is flagged as `is_identified = 0` (or has high visual noise), users can reprocess it directly from the UI:

```cpp
extern "C" DART_EXPORT bool Native_ReprocessRecording(
    const char* wav_path,
    const char* target_modem_id,
    const char* json_params
) {
    auto modem = ModemRegistry::get().instantiate(target_modem_id);
    if (!modem) return false;

    drwav wav;
    if (!drwav_init_file(&wav, wav_path, NULL)) return false;

    modem->init(wav.sampleRate, DispatchToDart);

    std::vector<float> buffer(4096);
    while (true) {
        ma_uint64 framesRead = drwav_read_pcm_frames_f32(&wav, 4096, buffer.data());
        if (framesRead == 0) break;
        modem->process_rx(buffer.data(), framesRead);
    }

    drwav_uninit(&wav);
    return true;
}

```

---

## 8. Flutter User Interface & Component System

The UI mirrors a messaging application, replacing conversational contact names with protocol badges, SNR metrics, waterfall indicators, and embedded audio players.

### 8.1 UI Screen Hierarchy

```
MainScaffold
├── AppBar
│   ├── Frequency / Audio Profile Indicator
│   └── Live Audio Waterfall Toggle (Overlay Drawer)
├── Body: ChatLogView (ListView.builder, reverse: true)
│   ├── UnknownBurstBubble
│   │   ├── Waveform Preview
│   │   ├── Inline Audio Player
│   │   └── Mode Override Dropdown ("Re-demodulate as...")
│   ├── SstvImageBubble
│   │   ├── Rendered Image (Zoomable)
│   │   ├── Resolution / Color Mode / Slant Metrics
│   │   └── Embedded Audio Playback Bar
│   ├── TextStreamBubble (Rattlegram / PSK31 / CW)
│   │   ├── Monospace / Terminal Payload View
│   │   └── Audio Playback Bar
│   └── PacketDataBubble (APRS / SAME)
│       ├── Decoded JSON / Metadata Form Fields
│       └── Raw Hex View
└── BottomBar: TransmissionComposer
    ├── Image Attachment Preview (if staged)
    ├── Input TextField
    └── Action Button: Transmit / Mode Picker Dialog

```

### 8.2 Mode Selection Bottom Sheet Filter

The transmission composer dynamically enables or disables protocols based on the attached media type:

```dart
List<ModemProtocolPlugin> getSelectableProtocols({required bool hasImage, required bool hasText}) {
  return allRegisteredPlugins.where((plugin) {
    if (hasImage) {
      return plugin.category == PayloadCategory.image; // SSTV modes, WEFAX
    }
    if (hasText) {
      return plugin.category == PayloadCategory.textStream || 
             plugin.category == PayloadCategory.packet; // Rattlegram, CW, PSK31, APRS
    }
    return false;
  }).toList();
}

```

### 8.3 Live Waterfall Widget (CustomPainter)

A 512-bin rolling FFT spectrogram runs via a `CustomPainter` connected directly to the native DSP thread's FFT output:

```dart
class WaterfallPainter extends CustomPainter {
  final Float32List fftMagnitudes; // 512 bins, 0 to 24 kHz
  final ui.Image historyTexture;

  WaterfallPainter({required this.fftMagnitudes, required this.historyTexture});

  @override
  void paint(Canvas canvas, Size size) {
    // 1. Shift historic spectrogram down 1 scanline
    canvas.drawImageRect(
      historyTexture,
      Rect.fromLTWH(0, 0, historyTexture.width.toDouble(), historyTexture.height.toDouble() - 1),
      Rect.fromLTWH(0, 1, size.width, size.height),
      Paint(),
    );

    // 2. Paint current FFT slice at y = 0 using thermal colormap
    final paint = Paint()..strokeWidth = 1.0;
    final binWidth = size.width / fftMagnitudes.length;

    for (int i = 0; i < fftMagnitudes.length; ++i) {
      final mag = fftMagnitudes[i].clamp(0.0, 1.0);
      paint.color = _mapThermalColor(mag);
      canvas.drawPoint(Offset(i * binWidth, 0), paint);
    }
  }

  Color _mapThermalColor(double normalizedVal) {
    // Color map: Black -> Blue -> Purple -> Red -> Yellow -> White
    return HSVColor.fromAHSV(1.0, (1.0 - normalizedVal) * 240.0, 1.0, normalizedVal).toColor();
  }

  @override
  bool shouldRepaint(covariant WaterfallPainter oldDelegate) => true;
}

```

---

## 9. Implementation File Tree & Structure

```
polyglot_radio/
├── android/
│   └── app/src/main/cpp/
│       ├── CMakeLists.txt
│       └── native_bindings.cpp
├── ios/
│   └── Classes/
│       ├── AudioSessionSetup.mm
│       └── CMakeLists.txt
├── native_core/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── IModemEngine.h
│   │   ├── modem_registry.h
│   │   └── dart_api_dl.h
│   ├── src/
│   │   ├── audio_hal.cpp          // miniaudio setup and ring buffers
│   │   ├── sentry_coordinator.cpp // Parallel preamble scanner
│   │   ├── native_bridge.cpp      // Dart C-ABI exports
│   │   └── modems/
│   │       ├── sstv_engine.cpp    // Martin, Scottie, Robot, PD
│   │       ├── rattlegram.cpp     // COFDM transceiver
│   │       ├── aprs_bell202.cpp   // AX.25 HDLC engine
│   │       ├── eas_same.cpp       // Emergency Alert System
│   │       ├── cw_morse.cpp       // Adaptive pitch/speed tracker
│   │       ├── psk31.cpp          // Costas loop BPSK engine
│   │       ├── feld_hell.cpp      // 122.5 Hz dot-matrix engine
│   │       ├── olivia.cpp         // Fast Walsh-Hadamard MFSK
│   │       ├── wefax.cpp          // 120 LPM maritime fax
│   │       ├── ft8.cpp            // LDPC weak-signal engine
│   │       └── ultrasound.cpp     // 19 kHz inaudible token transfer
│   └── external/
│       ├── miniaudio.h
│       ├── dr_wav.h
│       └── liquid-dsp/            // Filter math, FFT, Reed-Solomon
└── lib/
    ├── main.dart
    ├── core/
    │   ├── ffi_bindings.dart      // Raw dynamic library wrappers
    │   ├── modem_coordinator.dart // Sentry/Mode coordinator
    │   └── raster_bridge.dart     // Zero-copy pixel sharing
    ├── database/
    │   ├── app_database.dart      // Drift ORM
    │   └── models.dart
    ├── plugins/
    │   ├── modem_plugin.dart      // Abstract Dart plugin contract
    │   └── implementations/
    │       ├── sstv_plugin.dart
    │       ├── rattlegram_plugin.dart
    │       ├── cw_plugin.dart
    │       └── ...
    └── ui/
        ├── home_screen.dart       // Chat logbook scaffold
        ├── widgets/
        │   ├── waterfall_view.dart
        │   ├── transmission_bubble.dart
        │   └── audio_player_bar.dart
        └── composer/
            ├── transmission_composer.dart
            └── protocol_picker_sheet.dart

```

---

## 10. Implementation Plan & Milestones

### Phase 1: Native Audio Infrastructure & HAL (Weeks 1–2)

* Implement `miniaudio` loop for iOS and Android.
* Configure OS session handlers (`AVAudioSessionModeMeasurement`, AAudio low-latency flags).
* Build the $5\text{-second}$ SPSC circular pre-trigger buffer and energy-based squelch circuit.
* Validate continuous 48 kHz bit-accurate playback and capture using loopback cable tests.

### Phase 2: FFI Bridge, UI Skeleton & Storage (Weeks 3–4)

* Set up `dart:ffi` dynamic loading and initialize `Dart_InitializeApiDL`.
* Implement the zero-copy shared framebuffer (`Native_GetSharedCanvasPtr`).
* Build the Drift SQLite database layer for `Transmissions` and establish the local `.wav` storage directory.
* Build the Flutter Chat Feed UI with dummy message models and interactive audio playback bars.

### Phase 3: SSTV, Rattlegram, and Visual Modes (Weeks 5–7)

* Port and integrate the SSTV engine (FM discriminator, VIS auto-detect, horizontal sync PLL, slant correction).
* Implement zero-copy scanline updates to Dart and verify image decoding over live audio.
* Integrate the Rattlegram COFDM engine (`aicodix/modem`).
* Implement the Feld Hell 122.5 Hz dot-clock rasterizer and WEFAX 120 LPM engine.

### Phase 4: Text, Packet, & Sentry Engines (Weeks 8–10)

* Implement the CW Morse adaptive pitch tracker, Schmitt trigger, and PARIS speed classifier.
* Integrate Bell 202 (APRS AX.25) and EAS/SAME AFSK demodulators.
* Integrate PSK31 (Costas loop) and Olivia MFSK (Fast Walsh-Hadamard Transform).
* Implement the multi-threaded Sentry Coordinator: run parallel preambles and test autonomous mode switching without user intervention.

### Phase 5: Transmit Pipeline & Field Hardening (Weeks 11–12)

* Implement transmit audio synthesis for all modes (CW key-click raised-cosine shaping, SSTV tone generation, COFDM IFFT).
* Connect the bottom composer UI to the mode selector sheet and verify speaker transmission.
* Implement offline re-demodulation of saved `.wav` files.
* Test acoustic resilience across varying speaker/mic distances ($10\text{ cm}$ to $5\text{ m}$), background room chatter, and multi-path reflections. Apply final threshold tuning to DSP sentry metrics.
