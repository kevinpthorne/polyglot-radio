# Polyglot Radio: Acoustic SDR

**Polyglot Radio** is a cross-platform (macOS, Linux, iOS, Android) Acoustic Software-Defined Radio (SDR) modem application built with Flutter and a high-performance native C++20 DSP core. It transforms standard device microphones and speakers into a multi-protocol acoustic modem transceiver capable of autonomously detecting, arbitrating, receiving, and transmitting 11 distinct digital and analog communication waveforms over audio frequencies (and near-ultrasound).

---

## 📻 Key Features & Architecture

- **Parallel Sentry Architecture**: Continuously computes matched-filter and energy bank correlations across all supported protocols simultaneously. Detects preambles, VIS codes, and sync words to autonomously promote the highest-confidence modem without manual mode switching.
- **RMS Squelch with Pre-Buffer Slicing**: Dynamically monitors input RMS noise floor with hysteresis (-60 dB default). Maintains a rolling 5-second pre-buffer so preamble bursts occurring before threshold crossing are never lost.
- **Unidentified Burst Archival & Offline Reprocessing**: When an acoustic carrier exceeds squelch but fails immediate demodulation, it is archived to local WAV storage and flagged as "Unknown Burst" in the timeline. Users can selectively trigger offline re-demodulation against any modem engine.
- **Zero-Copy Shared Canvas**: 640×496 RGBA32 memory-mapped canvas shared across the C-ABI FFI boundary for rendering real-time raster sweeps (SSTV, HF WEFAX, and Hellschreiber) with zero memory copying overhead.
- **Live 512-Bin Spectrogram Waterfall**: Rolling audio frequency spectrogram painter polling native DSP FFT magnitudes at 30 FPS.
- **Audio Recording Import & Offline Demodulation**: Upload or import pre-recorded audio files (`.wav`) via the top app bar with native file browsing, manual path entry, or built-in synthetic waveform test presets. Files are archived into the timeline and immediately decoded either via Parallel Sentry auto-detection or a specific target modem.
- **Legal Compliance Invariant**: Station ID defaults to `null`. Strict client-side and native gating blocks transmission of amateur radio packet modes (APRS / AX.25) until the user explicitly configures a valid amateur radio callsign format in Station Settings.
- **Headless Virtual Loopback HAL**: Allows automated CI/CD and integration testing by routing transmitted audio directly into the receiver pipeline without requiring physical speakers or microphones.

---

## 📡 Supported Modem Protocols

1. **SSTV (Slow Scan TV)**: Martin 1/2, Scottie 1/2, Robot 36, and PD-120 analog color sweeps with 1900 Hz sync, 1200 Hz VIS pulse decoding, and FM discrimination.
2. **Rattlegram (COFDM)**: 88-subcarrier orthogonal frequency-division multiplexing with Schmidl-Cox preamble sync and 1-tap frequency equalizer.
3. **APRS / AX.25 (Bell 202)**: 1200-baud AFSK (1200/2200 Hz) with non-coherent delay-line discriminator, NRZI destuffing, CRC-16-CCITT, and AX.25 UI frame parsing. *(Requires Amateur Radio Callsign)*
4. **EAS / SAME**: Emergency Alert System / Specific Area Message Encoding AFSK (2083.3 / 1562.5 Hz, 520.83 baud) with bit synchronization and ZCZC header parsing.
5. **CW Morse Code**: Continuous wave telegraphy (5–45 WPM) with smoothed envelope follower and Schmitt-trigger symbol decoder.
6. **PSK31**: Narrowband 31.25-baud BPSK with Costas carrier-recovery loop and official 128-entry G3PLX Varicode codec.
7. **Feld Hell (Hellschreiber)**: 122.5 Hz dot-matrix facsimile telegraphy rendered directly onto the shared raster canvas.
8. **Olivia MFSK**: 16-tone multi-frequency shift keying with Hadamard transform correlation detector.
9. **HF WEFAX (Weather Fax)**: 120 LPM maritime weather facsimile demodulator with line synchronization.
10. **FT8**: Weak-signal 8-GFSK with $7\times 7$ Costas arrays and LDPC(174,87) framing.
11. **Ultrasound (19 kHz Silent)**: Inaudible near-ultrasonic 18.5–19.8 kHz 16-FSK modem with linear chirp preamble and Reed-Solomon RS(15,9) forward error correction over $\text{GF}(2^4)$.

---

## 📂 RFC Documentation

Detailed design specifications and architectural decisions are documented in the `docs/` directory:

- [RFC-001: Ultrasound & Rattlegram Specifications](docs/RFC-001_ULTRASOUND_AND_RATTLEGRAM.md)
- [RFC-002: Sentry Arbitration State Machine & RMS Squelch](docs/RFC-002_SENTRY_ARBITRATION_SQUELCH.md)
- [RFC-003: Native FFI C-ABI & Loopback Audio HAL](docs/RFC-003_NATIVE_FFI_AND_LOOPBACK_HAL.md)
- [Final Implementation Plan](docs/plans/final_implementation_plan.md)

---

## 🛠️ Building & Verification

### Prerequisites
- **Flutter SDK**: 3.41+ with Dart 3.11+
- **CMake**: 3.20+
- **C++20 Compiler**: Clang (Xcode / Apple Clang on macOS) or GCC 11+ on Linux

### 1. Build the Native C++20 Core
```bash
cd native_core
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j4
```
This builds `libpolyglot_native.dylib` (or `.so`) along with the native DSP math, sentry, and loopback test suites.

### 2. Run Native Test Suites
```bash
./dsp_tests
./sentry_tests
./loopback_tests
```

### 3. Run Automated Flutter Tests (Unit, Integration, E2E)
```bash
cd ../..
flutter test
```
The test suite validates:
- Callsign regex validation and legal transmission blocking
- SQLite persistence, ordering, and reactive streaming
- Plugin registry and media payload capability filtering
- Native FFI bindings, Audio HAL loopback, and shared canvas projection
- Home screen widget hierarchy, status banner, and settings dialog
- End-to-end acoustic loopback transmission and demodulation

### 4. Run the Application
```bash
flutter run -d macos    # or linux / ios / android
```
