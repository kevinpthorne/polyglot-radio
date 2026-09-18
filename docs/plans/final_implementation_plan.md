# Implementation Plan: Polyglot Radio Universal Acoustic Modem

**Application Title:** `Polyglot Radio: Acoustic SDR`  
**Package / Library ID:** `polyglot_radio`  
**Target Platforms:** macOS (13.0+), Linux (Debian 12+), iOS (16.0+), Android (API 29+)  
**Core Stack:** Flutter (Dart 3.x), C++20, `dart:ffi`, `miniaudio`, `sqlite3` / `drift`

---

## Approved User Decisions & Legal Invariants

1. **Station ID & Legal Callsign Compliance**:
   - Station ID defaults to `null`.
   - Transmitting in amateur radio packet modes (APRS / AX.25) strictly enforces callsign presence before audio keying. If unset, the transmission composer blocks transmit and opens the **Station Settings** modal prompting the user to enter their valid government-issued amateur callsign (e.g. `W1AW`, `K6OTA-7`).
2. **Branding & Presentation**:
   - User-facing application title: **`Polyglot Radio: Acoustic SDR`**.
   - Technical, tactical SDR aesthetic with dark theme, waterfall visualization, and signal telemetry badges.
3. **Deterministic Dynamic Test Audio Synthesis**:
   - Testing will dynamically synthesize audio bursts algorithmically with fixed deterministic seeds rather than relying solely on static fixtures.
   - Allows testing across diverse signal-to-noise ratios (SNR), frequency offsets, and corrupted bits while maintaining 100% test reproducibility.
4. **Headless Virtual Loopback HAL**:
   - Dual-mode C++ Audio HAL: hardware driver (`miniaudio`) and software virtual loopback driver for headless automated test runs in CI/CD without physical audio hardware.

---

## Architecture & Directory Layout

```
polyglot_radio/
├── docs/                                  # RFC technical specifications & plans
│   ├── RFC-001_ULTRASOUND_AND_RATTLEGRAM.md
│   ├── RFC-002_SENTRY_ARBITRATION_SQUELCH.md
│   ├── RFC-003_NATIVE_FFI_AND_LOOPBACK_HAL.md
│   └── plans/
│       └── final_implementation_plan.md
├── native_core/                           # C++20 DSP Core & Audio HAL
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── IModemEngine.h
│   │   ├── modem_registry.h
│   │   ├── dart_api_dl.h
│   │   ├── audio_hal.h
│   │   ├── sentry_coordinator.h
│   │   ├── native_bridge.h
│   │   └── dsp/                           # Mathematical DSP primitives
│   │       ├── fft.h                      # Radix-2 / Bluestein FFT
│   │       ├── goertzel.h                 # Single-tone energy detector
│   │       ├── fir_filter.h               # Symmetrical FIR bandpass/lowpass
│   │       ├── costas_loop.h              # BPSK carrier phase tracker
│   │       ├── crc.h                      # CRC-16-CCITT, CRC-14
│   │       ├── varicode.h                 # PSK31 Varicode lookup tables
│   │       ├── morse_table.h              # Morse code trie tables
│   │       └── reed_solomon.h             # Reed-Solomon (15, 9) Galois field codec
│   ├── src/
│   │   ├── audio_hal.cpp                  # SPSC pre-buffer, squelch, WAV slicer
│   │   ├── sentry_coordinator.cpp         # Multi-threaded preamble sentry array
│   │   ├── native_bridge.cpp              # C-ABI, Dart_PostCObject_DL, shared canvas
│   │   └── modems/                        # 11 Protocol Transceivers
│   │       ├── sstv_engine.cpp            # Martin, Scottie, Robot, PD (640x496 canvas)
│   │       ├── rattlegram.cpp             # 88-subcarrier COFDM transceiver
│   │       ├── aprs_bell202.cpp           # Bell 202 AFSK, HDLC, AX.25 UI frames
│   │       ├── eas_same.cpp               # Emergency Alert System AFSK & SAME parser
│   │       ├── cw_morse.cpp               # Adaptive pitch/speed (5-45 WPM) tracker
│   │       ├── psk31.cpp                  # Costas loop BPSK engine
│   │       ├── feld_hell.cpp              # 122.5 Hz dot-matrix facsimile engine
│   │       ├── olivia.cpp                 # Fast Walsh-Hadamard MFSK engine
│   │       ├── wefax.cpp                  # 120 LPM maritime weather facsimile
│   │       ├── ft8.cpp                    # 8-GFSK Costas array & LDPC(174,87) framing
│   │       └── ultrasound.cpp             # 18.5-19.8 kHz near-ultrasonic token modem
│   ├── external/
│   │   ├── miniaudio.h
│   │   └── dr_wav.h
│   └── tests/
│       ├── test_dsp_math.cpp
│       ├── test_sentry_correlator.cpp
│       └── test_modem_loopbacks.cpp
├── lib/                                   # Flutter UI & Dart Application Layer
│   ├── main.dart
│   ├── core/
│   │   ├── ffi_bindings.dart
│   │   ├── modem_coordinator.dart
│   │   └── raster_bridge.dart
│   ├── database/
│   │   ├── app_database.dart
│   │   └── models.dart
│   ├── plugins/
│   │   ├── modem_plugin.dart
│   │   └── implementations/
│   │       ├── sstv_plugin.dart
│   │       ├── rattlegram_plugin.dart
│   │       ├── aprs_plugin.dart
│   │       ├── eas_same_plugin.dart
│   │       ├── cw_plugin.dart
│   │       ├── psk31_plugin.dart
│   │       ├── feld_hell_plugin.dart
│   │       ├── olivia_plugin.dart
│   │       ├── wefax_plugin.dart
│   │       ├── ft8_plugin.dart
│   │       └── ultrasound_plugin.dart
│   ├── audio/
│   │   └── audio_playback_service.dart
│   └── ui/
│       ├── home_screen.dart
│       ├── widgets/
│       │   ├── waterfall_view.dart
│       │   ├── transmission_bubble.dart
│       │   ├── audio_player_bar.dart
│       │   └── status_banner.dart
│       ├── composer/
│       │   ├── transmission_composer.dart
│       │   └── protocol_picker_sheet.dart
│       └── settings/
│           └── station_settings_dialog.dart
└── test/                                  # Automated Test Suites
    ├── unit/
    │   ├── database_test.dart
    │   ├── plugin_registry_test.dart
    │   ├── ffi_bindings_test.dart
    │   └── callsign_validation_test.dart
    ├── integration/
    │   ├── native_bridge_integration_test.dart
    │   └── ui_chat_bubble_test.dart
    └── e2e/
        └── acoustic_modem_e2e_test.dart
```

---

## Step-by-Step Implementation Sequence

### Step 1: Missing Specification Documentation (`docs/`)
- Draft **RFC-001**: Ultrasound 18.5-19.8 kHz & Rattlegram COFDM Waveform Specification.
- Draft **RFC-002**: Two-Stage Sentry Array Arbitration, Priority Hierarchy, Squelch Hysteresis, and Ring Buffer Slicing.
- Draft **RFC-003**: C-ABI FFI Protocol, Zero-Copy Shared RGBA Canvas, and Headless Loopback HAL.
- Archive **final_implementation_plan.md** in `docs/plans/`.

### Step 2: Native C++20 Core & DSP Primitives (`native_core/`)
- Setup `CMakeLists.txt` for compiling the native shared library and test binaries.
- Implement DSP headers: FFT, Goertzel, FIR, Costas Loop, CRC16/14, Varicode tables, Morse trie, Reed-Solomon codec.
- Provide single-header `miniaudio.h` and `dr_wav.h` in `external/`.
- Implement `audio_hal.cpp` with lock-free SPSC circular ring buffer (240,000 float samples = 5s), dual-state RMS squelch, WAV stream slicer, and headless virtual loopback mode.
- Implement `sentry_coordinator.cpp` with parallel preamble correlation array, lock-out arbitration, and circular buffer stitching.
- Implement all 11 protocol modem engines (`sstv_engine`, `rattlegram`, `aprs_bell202`, `eas_same`, `cw_morse`, `psk31`, `feld_hell`, `olivia`, `wefax`, `ft8`, `ultrasound`).
- Implement `native_bridge.cpp` exporting C-ABI functions and Dart event dispatch via `Dart_PostCObject_DL`.
- Build native test runners (`dsp_tests`, `test_modem_loopbacks`) and verify 100% passing DSP units and integrations.

### Step 3: Flutter Project Scaffolding & Dependencies
- Initialize Flutter project with desktop and mobile support.
- Setup `pubspec.yaml` with required dependencies: `drift`, `sqlite3`, `path_provider`, `path`, `uuid`, `ffi`, `collection`.
- Configure platform CMake/Xcode build hooks to link `polyglot_native`.

### Step 4: Dart Core, FFI, Database & Plugin Architecture (`lib/core/`, `lib/database/`, `lib/plugins/`)
- Implement `ffi_bindings.dart` binding to native C-ABI with automatic library discovery across macOS, Linux, and mobile.
- Implement `raster_bridge.dart` for zero-copy memory projection over the 640x496 C++ heap canvas.
- Implement `app_database.dart` and `models.dart` for SQLite persistence of all transmissions.
- Implement station settings storage with callsign validation rules (default null, blocks APRS TX until configured).
- Implement `ModemCoordinator` orchestrating audio HAL, sentry events, database transactions, and transmission requests.
- Implement all 11 Dart `ModemProtocolPlugin` implementations.

### Step 5: Flutter User Interface (`lib/ui/`)
- Build `home_screen.dart` featuring:
  - Top SDR status banner (Audio profile, sample rate, RX squelch level, active sentry lock indicator, station callsign badge).
  - Waterfall toggle button opening the real-time 512-bin rolling FFT spectrogram (`WaterfallPainter`).
  - Chat log feed displaying reverse chronological transmissions.
- Build dynamic message bubbles:
  - `UnknownBurstBubble`: Audio waveform preview, inline player bar, "Re-demodulate as..." dropdown.
  - `SstvImageBubble`: Full-color/grayscale decoded SSTV/WEFAX image preview, zoom modal, slant/resolution telemetry.
  - `TextStreamBubble`: Terminal monospace decoded text stream (CW, Rattlegram, PSK31, Hell, Olivia, FT8, Ultrasound).
  - `PacketDataBubble`: Structured metadata view for APRS (Callsign, coordinates, message) and EAS/SAME (Event code, emergency level, county FIPS, raw text), hex viewer.
- Build `transmission_composer.dart` and `protocol_picker_sheet.dart`:
  - Mode filtering based on attached media (Image $\to$ SSTV/WEFAX; Text $\to$ Rattlegram/CW/PSK31/APRS/etc.).
  - Callsign requirement guard for amateur radio modes.
  - Transmit progress indicator during acoustic playback.
- Build `station_settings_dialog.dart` for configuring station callsign, station symbol, EAS county code, squelch threshold, and loopback mode toggle.

### Step 6: Automated Testing Execution (Units, Integrations, E2E)
- **Native Unit & Integration Tests**: C++ test suite validating all DSP algorithms and modulation/demodulation loopbacks.
- **Dart Unit Tests**: Database CRUD, FFI bindings, plugin registry, mode filtering, and legal callsign validation.
- **Widget Integration Tests**: Chat feed bubbles, composer mode filtering, and waterfall custom painter.
- **End-to-End Automated Acoustic Modem Test**: Transmit synthetic packets through native loopback HAL, observe sentry lock and promotion, verify decoded data written to SQLite database.
