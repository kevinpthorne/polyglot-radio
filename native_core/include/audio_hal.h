#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>

namespace hal {

// Fixed Invariants from SPEC.md
constexpr int kSampleRate = 48000;
constexpr int kRingBufferCapacity = 262144; // Power-of-two >= 240,000 (5.46 seconds)
constexpr int kPreTriggerSamples = 144000;   // 3.0 seconds at 48 kHz
constexpr int kHangoverSamples = 72000;      // 1.5 seconds at 48 kHz

class SPSCFloatBuffer {
public:
    SPSCFloatBuffer() : head_(0), tail_(0), buffer_(kRingBufferCapacity, 0.0f) {}

    void write(const float* data, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            buffer_[head_.load(std::memory_order_relaxed) & (kRingBufferCapacity - 1)] = data[i];
            head_.fetch_add(1, std::memory_order_release);
        }
    }

    size_t read(float* dest, size_t max_count) {
        size_t available = available_read();
        size_t to_read = (max_count < available) ? max_count : available;
        size_t tail = tail_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < to_read; ++i) {
            dest[i] = buffer_[(tail + i) & (kRingBufferCapacity - 1)];
        }
        tail_.fetch_add(to_read, std::memory_order_release);
        return to_read;
    }

    // Read without advancing tail (for pre-trigger extraction)
    void peek_rewind(float* dest, size_t count, size_t rewind_offset) {
        size_t head = head_.load(std::memory_order_acquire);
        size_t start_pos = head - rewind_offset;
        for (size_t i = 0; i < count; ++i) {
            dest[i] = buffer_[(start_pos + i) & (kRingBufferCapacity - 1)];
        }
    }

    size_t available_read() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0;
    }

    void reset() {
        head_.store(0, std::memory_order_release);
        tail_.store(0, std::memory_order_release);
    }

private:
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
    std::vector<float> buffer_;
};

using AudioFrameCallback = std::function<void(const float* samples, size_t count)>;

class AudioHAL {
public:
    static AudioHAL& get() {
        static AudioHAL instance;
        return instance;
    }

    bool init(int sample_rate = kSampleRate, bool enable_loopback = false);
    void shutdown();
    bool is_running() const { return is_running_.load(); }

    bool start_hardware_device(int sample_rate = kSampleRate);
    void set_loopback_mode(bool enabled);
    bool is_loopback_mode() const { return loopback_mode_.load(); }

    void set_squelch_threshold_db(float db) { squelch_threshold_db_.store(db); }
    float get_squelch_threshold_db() const { return squelch_threshold_db_.load(); }
    float get_current_rms_db() const { return current_rms_db_.load(); }
    bool is_squelch_open() const { return squelch_open_.load(); }

    // Register callback for DSP processing of incoming audio frames
    void set_rx_callback(AudioFrameCallback cb) { rx_callback_ = cb; }

    // Mute Controls (TX output mute, RX input mute)
    void set_output_muted(bool muted) { is_output_muted_.store(muted); }
    bool is_output_muted() const { return is_output_muted_.load(); }
    void set_input_muted(bool muted) { is_input_muted_.store(muted); }
    bool is_input_muted() const { return is_input_muted_.load(); }

    // Audio Output / TX Queue
    bool queue_tx_samples(const float* samples, size_t count);
    size_t pull_tx_samples(float* output, size_t count);
    bool is_tx_active() const;
    void abort_tx();

    // Audio Playback (for recorded transmissions)
    bool play_audio_file(const std::string& path);
    void pause_audio_playback();
    void resume_audio_playback();
    void stop_audio_playback();
    bool is_audio_playing() const;
    float get_audio_playback_position() const;
    float get_audio_playback_duration() const;
    void seek_audio_playback(float seconds);
    size_t pull_playback_samples(float* output, size_t count);

    // Simulation / Direct injection (for testing)
    void inject_rx_samples(const float* samples, size_t count);

    // Ring buffer access for pre-trigger retrieval
    SPSCFloatBuffer& get_ring_buffer() { return ring_buffer_; }

    // Rolling FFT buffer access
    void get_latest_fft(float* out_magnitudes, size_t count);

private:
    AudioHAL();
    ~AudioHAL();

    std::atomic<bool> is_running_{false};
    std::atomic<bool> loopback_mode_{false};
    std::atomic<float> squelch_threshold_db_{-45.0f}; // Default trip: -45 dBFS
    std::atomic<float> current_rms_db_{-90.0f};
    std::atomic<float> noise_floor_db_{-60.0f};
    std::atomic<bool> squelch_open_{false};

    SPSCFloatBuffer ring_buffer_;
    AudioFrameCallback rx_callback_;

    // TX playback buffer
    std::vector<float> tx_buffer_;
    size_t tx_playback_index_{0};
    std::atomic<bool> is_transmitting_{false};
    mutable std::mutex tx_mutex_;

    // Recorded audio playback buffer
    std::vector<float> playback_buffer_;
    size_t playback_index_{0};
    std::atomic<bool> is_playback_active_{false};
    std::atomic<bool> is_playback_paused_{false};
    mutable std::mutex playback_mutex_;

    // Mute flags
    std::atomic<bool> is_output_muted_{false};
    std::atomic<bool> is_input_muted_{false};

    // Real-time FFT snapshot
    std::vector<float> latest_fft_magnitudes_;
    std::vector<float> fft_accum_buffer_;
    std::mutex fft_mutex_;

    // Miniaudio device handle pointer (void* to avoid exposing miniaudio in header)
    void* ma_device_handle_{nullptr};
};

} // namespace hal
