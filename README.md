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

Polyglot Radio supports 11 analog and digital acoustic waveforms across the audible and near-ultrasound spectrum.

### 📊 Modem Transmit (TX) & Receive (RX) Status Matrix

| Modem Protocol | TX Status | RX Status | Sentry Auto-Detect | Notes & Operational Status |
| :--- | :---: | :---: | :---: | :--- |
| **SSTV (Slow Scan TV)** | ✅ Operational | ✅ Operational | ✅ Active | Robot 36, Robot 72, Martin 1/2, Scottie 1/2, PD-120. Real-time line-by-line canvas streaming with VIS sync. |
| **Ultrasound (19 kHz)** | ✅ Operational | ✅ Operational | ✅ Active | Inaudible 18.5–19.8 kHz 16-FSK with linear chirp preamble and Reed-Solomon RS(15,9) error correction. |
| **APRS / AX.25 (Bell 202)** | ✅ Operational | ✅ Operational | ✅ Active | 1200-baud Bell 202 AFSK packet framing. TX strictly blocked until legal amateur callsign is configured. |
| **CW Morse Code** | ✅ Operational | ⚠️ Experimental | ✅ Active | TX 5–45 WPM with smooth envelope. RX operates on clean tones; sensitive to pitch jitter in free air. |
| **PSK31** | ✅ Operational | ⚠️ Experimental | ⚠️ Fallback | 31.25-baud BPSK with Varicode. Loopback verified; requires precise center frequency lock. |
| **Olivia MFSK** | ✅ Operational | ⚠️ Experimental | ⚠️ Fallback | 16-tone MFSK with Walsh-Hadamard transform. Loopback verified. |
| **HF WEFAX** | ✅ Operational | ⚠️ Experimental | ⚠️ Fallback | 120 LPM maritime weather facsimile onto shared 640×496 RGBA canvas. |
| **FT8** | ✅ Operational | ⚠️ Experimental | ⚠️ Fallback | 8-GFSK Costas array framing. Loopback verified. |
| **EAS / SAME** | ✅ Operational | ❌ Broken / Inactive | ⚠️ Inactive | TX generates standard SAME headers & dual-tone attention signal. Live acoustic RX detection currently fails. |
| **Feld-Hell (Hellschreiber)** | ✅ Operational | ❌ Broken / Inactive | ⚠️ Inactive | TX generates 122.5-baud dot-matrix facsimile. Double-trace raster engine implemented, but live RX detection fails. |
| **Rattlegram (COFDM)** | ⚠️ Experimental | ❌ Tabled | ❌ Disabled | Sentry auto-detection tabled to prevent false triggers. 1st-party app uses 256 subcarriers & Polar codes. |

> **Status Legend:**
> - ✅ **Operational**: Verified working in both automated test suites and real-world acoustic transmissions.
> - ⚠️ **Experimental**: Verified in native loopback and synthetic tests; may require manual frequency tuning or direct audio connection.
> - ❌ **Broken / Tabled**: Live acoustic RX is currently unreliable or tabled to prevent cross-modem sentry interference.

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

---

## 📄 License & Privacy

- **License**: Polyglot Radio is licensed under the [GNU Affero General Public License v3.0 (AGPLv3)](LICENSE).
- **Privacy Policy**: See [PRIVACY_POLICY.md](PRIVACY_POLICY.md) for our zero-data-collection policy.
