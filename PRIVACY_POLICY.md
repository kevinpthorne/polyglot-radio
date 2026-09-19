# Privacy Policy for Polyglot Radio

**Last Updated:** September 19, 2026  
**Effective Date:** September 19, 2026

Polyglot Radio ("we", "our", or "the app") is committed to protecting your privacy. This Privacy Policy explains our practices regarding data collection, usage, and disclosure when you use the Polyglot Radio application across supported platforms (macOS, iOS, Android, Linux, and Windows).

---

## 1. Summary: Zero Data Collection

**Polyglot Radio does not collect, transmit, sell, or share any personal information, telemetry, or user data.**

All signal processing, audio analysis, demodulation, image synthesis, and data decoding take place strictly on your local device. No user information or audio data is ever transmitted to our servers or any third-party services.

---

## 2. Information We Do Not Collect

- **Personal Information:** We do not collect names, email addresses, phone numbers, postal addresses, or government identifiers.
- **Account Data:** The application does not require user accounts, logins, passwords, or authentication.
- **Usage & Telemetry Data:** We do not track how you use the app, session lengths, feature usage, or interaction analytics.
- **Crash & Diagnostic Logs:** We do not automatically send crash logs or diagnostic reports to external servers.
- **Location Data:** We do not access or collect GPS or coarse location information.
- **Financial & Payment Information:** We do not process or store payment details.

---

## 3. Device Permissions & On-Device Processing

Polyglot Radio requests certain device permissions strictly to deliver its core software-defined acoustic radio modem functionality. These features operate entirely offline on your local device:

### A. Microphone (`RECORD_AUDIO` / `NSMicrophoneUsageDescription`)
- **Purpose:** Used to capture audio via your device's microphone or line-in input for real-time acoustic signal processing, modem detection (Parallel Sentry), spectrogram/waterfall visualization, and demodulation of supported waveforms.
- **Handling:** Audio streams are processed in-memory using the native on-device C++ DSP engine. Raw audio streams are **never** uploaded, streamed to cloud services, or transmitted over the internet.

### B. Photos, Media, and Storage Access
- **Purpose:** Used only when you explicitly choose to:
  - Select an image from your device to transmit via visual modems (such as SSTV or HF WEFAX).
  - Save received/decoded images from acoustic transmissions directly to your device storage.
  - Import external audio recordings (`.wav` files) for offline demodulation, or export locally recorded audio bursts.
- **Handling:** File access is limited to the specific files you select or save. No files are accessed in the background or transferred off your device.

---

## 4. Local Storage and Data Retention

All data generated during application usage is stored locally on your device in the app's sandboxed storage directory (e.g., local SQLite database and cached media files):

- **Station Settings:** Configuration options, such as your amateur radio callsign, audio squelch thresholds, and theme preferences.
- **Message History & Timeline:** Decoded text messages, packet frames, and received raster image sweeps.
- **Audio Burst Archives:** Audio recordings from squelch trigger events or manual recordings.

### Managing and Deleting Your Data
Because all data resides exclusively on your local device:
- You can clear recorded bursts and message history directly within the application.
- You can permanently remove all stored application data at any time by clearing the application data/cache in your operating system settings or by uninstalling Polyglot Radio.

---

## 5. Third-Party Services and Analytics

- **No Third-Party SDKs:** Polyglot Radio does not integrate third-party analytics (e.g., Google Analytics, Firebase), user tracking tools, or advertising SDKs.
- **No Third-Party Data Sharing:** Because we do not collect any data, no data is sold, rented, or shared with third parties or data brokers.

---

## 6. Network Communications

Polyglot Radio operates as an offline, local signal processing application. The application does not make outbound network connections to transmit user telemetry or personal data. 

*(Note: If you use the app to transmit or receive digital radio packet frames such as APRS/AX.25 over acoustic frequencies, those signals are broadcast over the air via sound waves according to amateur radio conventions and are unencrypted by design and regulation.)*

---

## 7. Children's Privacy

Polyglot Radio does not collect personal information from anyone, including children under the age of 13 (or under the applicable age of digital consent in your jurisdiction). 

---

## 8. Changes to This Privacy Policy

We may update this Privacy Policy from time to time if new features require changes in data handling. Any updates will be reflected with a revised "Last Updated" date at the top of this document. We encourage you to review this policy periodically.

---

## 9. Contact Us

If you have any questions, concerns, or feedback regarding this Privacy Policy or the privacy practices of Polyglot Radio, please contact us:

- **Project Repository:** [GitHub Repository](https://github.com/) *(replace with repository URL)*
- **Email:** `contact@example.com` *(replace with project contact email)*
