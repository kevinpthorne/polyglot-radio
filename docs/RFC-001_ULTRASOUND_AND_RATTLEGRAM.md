# RFC-001: Ultrasound & Rattlegram Acoustic Waveform Specifications

**Status:** Approved  
**Author:** Polyglot Radio Architecture Team  
**Updated:** 2026-09-18  

---

## 1. Abstract

This document specifies the precise physical-layer modulation, subcarrier allocations, preamble synchronizations, framing structures, and forward error correction (FEC) schemes for two modern acoustic modem modes in Polyglot Radio:
1. **Ultrasound Modem (`ultrasound`)**: Near-ultrasonic (18.5 kHz – 19.8 kHz) inaudible acoustic token transfer.
2. **Rattlegram COFDM Modem (`rattlegram`)**: Multi-carrier orthogonal frequency-division multiplexing with Schmidl-Cox synchronization and frequency-domain channel equalization.

---

## 2. Ultrasound Inaudible Modem (`ultrasound`)

### 2.1 Acoustic Spectrum & Human Audibility Invariant
To guarantee silent operation on standard consumer smartphone loudspeakers and microphones without triggering annoyance in humans or pets, the ultrasound modem operates in the band from $18{,}000\text{ Hz}$ to $20{,}000\text{ Hz}$. Modern $48\text{ kHz}$ DACs provide a Nyquist limit of $24\text{ kHz}$, giving a $4\text{ kHz}$ guard margin.

### 2.2 Chirp Preamble Synchronization
* **Waveform:** Linear frequency-modulated (LFM) up-chirp.
* **Frequency Range:** $f_{start} = 18{,}000\text{ Hz} \to f_{end} = 20{,}000\text{ Hz}$.
* **Duration:** $T_{chirp} = 0.050\text{ s}$ ($50\text{ ms}$, corresponding to 2,400 samples at $48\text{ kHz}$).
* **Chirp Function:**
$$s_{chirp}(t) = \cos\left(2\pi \left(f_{start} \cdot t + \frac{f_{end} - f_{start}}{2 \cdot T_{chirp}} \cdot t^2\right)\right) \cdot w(t)$$
where $w(t)$ is a 10% Tukey taper preventing key-clicks.
* **Detector:** Matched filter (cross-correlation with the reference conjugate chirp). Detection threshold trips when normalized correlation peak $\ge 0.72$.

### 2.3 Modulation & Symbol Encoding
* **Modulation Scheme:** 16-ary Orthogonal Frequency-Shift Keying (16-FSK). Each symbol represents 4 bits (1 nibble).
* **Tone Allocation:**
  * Base tone frequency $f_0 = 18{,}500\text{ Hz}$.
  * Subcarrier spacing $\Delta f = 80.0\text{ Hz}$.
  * Tone frequencies:
$$f_k = f_0 + k \cdot \Delta f \quad (k = 0, 1, \dots, 15)$$
    * $f_0 = 18{,}500\text{ Hz}, \dots, f_{15} = 19{,}700\text{ Hz}$.
* **Symbol Duration:** $T_{sym} = 12.5\text{ ms}$ (600 samples at $48\text{ kHz}$).
* **Raw Baud Rate:** $80\text{ symbols/sec} \times 4\text{ bits/sym} = 320\text{ bps}$.

### 2.4 Framing & Forward Error Correction
* **Sync Nibbles:** 2 fixed sync nibbles (`0x5`, `0xA`) immediately following the preamble chirp.
* **Length Field:** 2 nibbles (1 byte) specifying payload length $L \in [1, 32]$ bytes.
* **Payload:** $L$ bytes ($2L$ nibbles).
* **Error Correction:** Reed-Solomon $RS(15, 9)$ over $GF(2^4)$ with generator polynomial:
$$g(x) = \prod_{i=0}^{5} (x - \alpha^i)$$
Capable of correcting up to 3 corrupted nibbles per 15-nibble codeword block.

---

## 3. Rattlegram COFDM Modem (`rattlegram`)

### 3.1 Subcarrier Structure
* **System Sampling Rate:** $f_s = 48{,}000\text{ Hz}$.
* **FFT Size:** $N = 1024$ points ($\Delta f = 46.875\text{ Hz}$).
* **Cyclic Prefix:** $N_{cp} = 128$ samples ($2.67\text{ ms}$), providing multi-path delay spread resilience in indoor room environments.
* **Total Symbol Length:** $N_{total} = N + N_{cp} = 1152$ samples ($24.0\text{ ms}$).
* **Active Subcarriers:** 88 subcarriers placed symmetrically within the acoustic band $1200\text{ Hz} – 2200\text{ Hz}$ (subcarrier indices $k \in [26, 113]$).

### 3.2 Preamble Synchronization (Schmidl-Cox)
* A training symbol consisting of two identical halves in the time domain:
$$s_{pre}(n) = s_{pre}(n + L), \quad L = 512, \quad n \in [0, 511]$$
* Formed by generating pseudo-random BPSK symbols on even subcarrier indices ($2k$) and zero on odd indices.
* Receiver sliding correlation:
$$P(d) = \sum_{m=0}^{L-1} r^*(d+m) \cdot r(d+m+L)$$
$$R(d) = \sum_{m=0}^{L-1} |r(d+m+L)|^2$$
$$M(d) = \frac{|P(d)|^2}{(R(d))^2}$$
A peak of $M(d) \ge 0.85$ establishes symbol timing boundaries and Carrier Frequency Offset (CFO).

### 3.3 Pilot Subcarriers & Channel Equalization
* Every 4th active subcarrier ($k \bmod 4 = 0$) carries known pilot symbols ($+1$).
* The receiver estimates the complex room transfer function $H_k$ at pilot positions and performs linear frequency interpolation across data subcarriers.
* Single-tap Frequency-Domain Equalizer:
$$\hat{X}_k = \frac{Y_k}{H_k}$$

### 3.4 Constellation & Payload Bitrate
* Default constellation: QPSK (Gray-coded).
* Active data subcarriers: $88 - 22\text{ (pilots)} = 66$ data subcarriers.
* Bits per symbol: $66 \times 2 = 132\text{ bits}$ per $24\text{ ms}$ OFDM symbol ($\approx 5.5\text{ kbps}$ unencoded raw, $\approx 1.2\text{ kbps}$ with convolutional $1/2$ rate coding and interleaving).
