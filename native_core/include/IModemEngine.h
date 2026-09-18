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
