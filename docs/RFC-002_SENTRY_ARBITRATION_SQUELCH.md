# RFC-002: Autonomous Sentry Array Arbitration, Squelch Hysteresis, and Ring Buffer Slicing

**Status:** Approved  
**Author:** Polyglot Radio Architecture Team  
**Updated:** 2026-09-18  

---

## 1. Abstract

This RFC details the operational behavior, arbitration logic, energy squelch hysteresis, and memory-mapped pre-buffer audio slicing implemented in the Native C++20 Core of Polyglot Radio. It ensures completely autonomous, hands-free protocol detection across all 11 modems without missing transmission preambles or saving empty dead-air files.

---

## 2. Ring Pre-Buffer & Audio Slicer

### 2.1 Single-Producer Single-Consumer (SPSC) Architecture
* **Buffer Length:** 5.0 seconds at $48{,}000\text{ Hz}$ mono floating point ($240{,}000$ elements, $\approx 960\text{ KB}$).
* **Memory Invariant:** Lock-free, power-of-two capacity masked indices (`idx & (CAPACITY - 1)`), zero heap allocation in real-time callback.
* **Producer:** `miniaudio` or Virtual Loopback HAL thread.
* **Consumer:** Native DSP Worker thread.

### 2.2 Dual-State Squelch Circuit
The squelch evaluates signal energy in overlapping windows of $N = 1024$ samples ($21.33\text{ ms}$):
$$\text{RMS} = \sqrt{\frac{1}{N}\sum_{i=0}^{N-1} x[i]^2}$$
$$\text{Level}_{\text{dBFS}} = 20 \log_{10}(\text{RMS} + 10^{-9})$$

* **Noise Floor Tracking:** Moving average over silence frames:
$$\text{Floor}_{\text{dBFS}} \leftarrow (1 - \alpha) \cdot \text{Floor}_{\text{dBFS}} + \alpha \cdot \text{Level}_{\text{dBFS}} \quad (\alpha = 0.01)$$
* **Open Threshold:** Trip open when $\text{Level}_{\text{dBFS}} > \text{Floor}_{\text{dBFS}} + 6.0\text{ dB}$, or when any sentry reports confidence $> 0.70$.
* **Hangover Time:** Squelch stays open for $1.5\text{ seconds}$ ($72{,}000$ samples) after energy drops below threshold to avoid clipping stop bits or trailing carrier tones.

### 2.3 Audio Slicing & File Generation
When squelch trips:
1. Slicer opens an immutable `.wav` container via `dr_wav` in the app's audio directory (`<documents>/transmissions/<uuid>.wav`).
2. Slicer rewinds $3.0\text{ seconds}$ ($144{,}000$ samples) in the SPSC ring buffer and writes this pre-trigger window as the beginning of the WAV file.
3. Slicer appends live incoming audio frames until carrier loss + hangover expiration.
4. Slicer finalizes WAV header with exact sample count and notifies Dart coordinator with the absolute file path.

---

## 3. Sentry Array & Autonomous Promotion State Machine

### 3.1 State Transitions

```
               [IDLE (Squelch Closed)]
                         │
                         │ Energy > Floor + 6dB
                         ▼
             [EVALUATING (Parallel Sentries)]
                         │
                         │ Sentry Confidence > Threshold
                         ▼
        [PROMOTED (Active Modem Demodulating)]
                         │
                         │ Carrier Lost / EOT / 1.5s Hangover
                         ▼
             [TEARDOWN (WAV Finalized)]
                         │
                         ▼
               [IDLE (Squelch Closed)]
```

### 3.2 Sentry Correlation Matrix & Priority Hierarchy
When multiple sentry detectors exceed threshold within the same time window, arbitration follows strict priority:

| Priority | Protocol Sentry | Detection Method | Trigger Threshold | Holdoff Time |
|---|---|---|---|---|
| 1 | `sstv_engine` | 1900 Hz Goertzel tone detector | Mag $> \text{floor} + 14\text{ dB}$ | $\ge 250\text{ ms}$ |
| 2 | `rattlegram` | Schmidl-Cox conjugate correlation | $M(d) > 0.85$ | Instantaneous |
| 3 | `aprs_packet` | Bell 202 AFSK delay-and-multiply | 2 consecutive `0x7E` HDLC flags | 16 bits |
| 4 | `eas_same` | Dual-tone Mark/Space transition | Sync bytes `0xAB 0xAB` | 16 bits |
| 5 | `ultrasound` | 18-20 kHz linear chirp matched filter | Peak correlation $\ge 0.72$ | $50\text{ ms}$ |
| 6 | `hf_wefax` | 1200 Hz start tone Goertzel | Mag $> \text{floor} + 12\text{ dB}$ | $\ge 1.0\text{ s}$ |
| 7 | Continuous Fallback | CW / PSK31 / Feld Hell / Olivia / FT8 | Text Sanity Buffer ($\ge 5$ valid ASCII) | Continuous |

### 3.3 Engine Coordinator Lockout & Promotion
1. Once a primary sentry trips, all competing sentries are muted immediately.
2. The winning modem engine is instantiated or attached as the **Active Demodulator**.
3. The coordinator feeds the preceding $3.0\text{ seconds}$ of PCM from the pre-trigger buffer into the promoted engine's `process_rx`.
4. Subsequent PCM frames stream exclusively to the active engine until carrier loss.
5. If an active demodulator hangs or encounters continuous silence for $> 8.0\text{ seconds}$, a watchdog timer forces a reset back to `IDLE` state.
