#pragma once
#include "IModemEngine.h"
#include "modem_registry.h"
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

namespace core {

enum class CoordinatorState {
    Idle,
    Evaluating,
    Demodulating,
    Hangover
};

class SentryCoordinator {
public:
    static SentryCoordinator& get() {
        static SentryCoordinator instance;
        return instance;
    }

    void init(int sample_rate, EventCallback callback);
    void reset();

    // Process incoming audio buffer from HAL
    void process_audio(const float* samples, size_t count);

    // Manual / offline reprocessing
    bool reprocess_wav(const char* wav_path, const char* target_modem_id, const char* json_params);

    CoordinatorState get_state() const { return state_.load(); }
    const char* get_active_protocol_id() const;

    void set_storage_directory(const std::string& dir) { storage_dir_ = dir; }
    const std::string& get_storage_directory() const { return storage_dir_; }

private:
    SentryCoordinator();
    ~SentryCoordinator();

    void trigger_promotion(const std::string& protocol_id, float snr);
    void finish_reception();

    std::atomic<CoordinatorState> state_{CoordinatorState::Idle};
    int sample_rate_{48000};
    EventCallback event_callback_{nullptr};

    std::vector<std::unique_ptr<IModemEngine>> sentry_engines_;
    std::unique_ptr<IModemEngine> active_engine_;
    std::string active_protocol_id_;

    size_t silence_samples_count_{0};
    size_t active_burst_samples_{0};
    std::string current_wav_path_;
    void* current_wav_handle_{nullptr}; // drwav*

    std::string storage_dir_;
    mutable std::mutex coordinator_mutex_;
};

} // namespace core
