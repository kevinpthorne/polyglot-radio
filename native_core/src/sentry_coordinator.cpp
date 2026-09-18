#include "sentry_coordinator.h"
#include "audio_hal.h"
#include "dr_wav.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace core {

SentryCoordinator::SentryCoordinator() = default;

SentryCoordinator::~SentryCoordinator() {
    reset();
}

void SentryCoordinator::init(int sample_rate, EventCallback callback) {
    std::lock_guard<std::mutex> lock(coordinator_mutex_);
    sample_rate_ = sample_rate;
    event_callback_ = callback;

    // Instantiate all registered modems into sentry array
    sentry_engines_.clear();
    const auto& factories = ModemRegistry::get().all();
    for (const auto& kv : factories) {
        auto engine = kv.second();
        if (engine) {
            engine->init(sample_rate_, event_callback_);
            sentry_engines_.push_back(std::move(engine));
        }
    }

    state_.store(CoordinatorState::Idle);
}

void SentryCoordinator::reset() {
    std::lock_guard<std::mutex> lock(coordinator_mutex_);
    if (current_wav_handle_) {
        drwav* wav = static_cast<drwav*>(current_wav_handle_);
        drwav_uninit(wav);
        delete wav;
        current_wav_handle_ = nullptr;
    }
    active_engine_.reset();
    active_protocol_id_.clear();
    state_.store(CoordinatorState::Idle);
    silence_samples_count_ = 0;
    active_burst_samples_ = 0;
}

void SentryCoordinator::process_audio(const float* samples, size_t count) {
    std::lock_guard<std::mutex> lock(coordinator_mutex_);

    CoordinatorState cur_state = state_.load();

    if (cur_state == CoordinatorState::Idle || cur_state == CoordinatorState::Evaluating) {
        // Run parallel sentry correlation
        float best_confidence = 0.0f;
        std::string best_modem_id;

        for (auto& sentry : sentry_engines_) {
            float conf = sentry->check_sentry_confidence(samples, count);
            if (conf > best_confidence) {
                best_confidence = conf;
                best_modem_id = sentry->get_id();
            }
        }

        if (best_confidence >= 0.70f) {
            trigger_promotion(best_modem_id, best_confidence * 25.0f);
        }
    } else if (cur_state == CoordinatorState::Demodulating) {
        active_burst_samples_ += count;

        // Feed to active engine
        if (active_engine_) {
            active_engine_->process_rx(samples, count);
        }

        // Stream into active WAV file if recording
        if (current_wav_handle_) {
            drwav* wav = static_cast<drwav*>(current_wav_handle_);
            // Convert f32 to int16 for disk efficiency
            std::vector<int16_t> pcm16(count);
            for (size_t i = 0; i < count; ++i) {
                float s = std::clamp(samples[i], -1.0f, 1.0f);
                pcm16[i] = static_cast<int16_t>(s * 32767.0f);
            }
            drwav_write_pcm_frames(wav, count, pcm16.data());
        }

        // Check for carrier loss / squelch closure
        bool squelch_open = hal::AudioHAL::get().is_squelch_open();
        if (!squelch_open) {
            silence_samples_count_ += count;
            if (silence_samples_count_ >= hal::kHangoverSamples) {
                finish_reception();
            }
        } else {
            silence_samples_count_ = 0;
        }

        // Watchdog timeout (e.g. 15 seconds)
        if (active_burst_samples_ > static_cast<size_t>(sample_rate_ * 15)) {
            finish_reception();
        }
    }
}

void SentryCoordinator::trigger_promotion(const std::string& protocol_id, float snr) {
    active_protocol_id_ = protocol_id;
    active_engine_ = ModemRegistry::get().instantiate(protocol_id);
    if (!active_engine_) return;

    active_engine_->init(sample_rate_, event_callback_);
    state_.store(CoordinatorState::Demodulating);
    silence_samples_count_ = 0;
    active_burst_samples_ = 0;

    // Generate unique WAV recording file path
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << (storage_dir_.empty() ? "/tmp" : storage_dir_) << "/burst_" << now << ".wav";
    current_wav_path_ = oss.str();

    // Initialize WAV container
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = sample_rate_;
    format.bitsPerSample = 16;

    drwav* wav = new drwav();
    if (drwav_init_file_write(wav, current_wav_path_.c_str(), &format, NULL)) {
        current_wav_handle_ = wav;
    } else {
        delete wav;
        current_wav_handle_ = nullptr;
    }

    // 1. Retrieve preceding 3.0 seconds (144,000 samples) from SPSC ring pre-buffer
    size_t pre_samples = hal::kPreTriggerSamples;
    std::vector<float> pre_buffer(pre_samples, 0.0f);
    hal::AudioHAL::get().get_ring_buffer().peek_rewind(pre_buffer.data(), pre_samples, pre_samples);

    // 2. Prepend pre-trigger buffer to WAV recording
    if (current_wav_handle_) {
        std::vector<int16_t> pcm16(pre_samples);
        for (size_t i = 0; i < pre_samples; ++i) {
            float s = std::clamp(pre_buffer[i], -1.0f, 1.0f);
            pcm16[i] = static_cast<int16_t>(s * 32767.0f);
        }
        drwav_write_pcm_frames(wav, pre_samples, pcm16.data());
    }

    // 3. Feed pre-trigger buffer into promoted engine
    active_engine_->process_rx(pre_buffer.data(), pre_samples);

    // 4. Dispatch CarrierDetected event to Dart
    if (event_callback_) {
        NativeModemEvent ev{};
        ev.event_type = static_cast<int32_t>(EventType::CarrierDetected);
        ev.protocol_id = active_protocol_id_.c_str();
        ev.snr_db = snr;
        ev.center_freq = 1500;
        ev.payload = reinterpret_cast<const uint8_t*>(current_wav_path_.c_str());
        ev.payload_len = current_wav_path_.size();
        ev.metadata_int = 0;
        event_callback_(&ev);
    }
}

void SentryCoordinator::finish_reception() {
    // Finalize WAV file
    if (current_wav_handle_) {
        drwav* wav = static_cast<drwav*>(current_wav_handle_);
        drwav_uninit(wav);
        delete wav;
        current_wav_handle_ = nullptr;
    }

    // Dispatch CarrierLost event to Dart
    if (event_callback_) {
        int duration_ms = static_cast<int>((active_burst_samples_ * 1000) / sample_rate_);
        NativeModemEvent ev{};
        ev.event_type = static_cast<int32_t>(EventType::CarrierLost);
        ev.protocol_id = active_protocol_id_.c_str();
        ev.snr_db = 0.0f;
        ev.center_freq = 0;
        ev.payload = reinterpret_cast<const uint8_t*>(current_wav_path_.c_str());
        ev.payload_len = current_wav_path_.size();
        ev.metadata_int = duration_ms;
        event_callback_(&ev);
    }

    active_engine_.reset();
    active_protocol_id_.clear();
    state_.store(CoordinatorState::Idle);
    silence_samples_count_ = 0;
    active_burst_samples_ = 0;
}

bool SentryCoordinator::reprocess_wav(const char* wav_path, const char* target_modem_id, const char* json_params) {
    if (!wav_path || !target_modem_id) return false;

    drwav wav;
    if (!drwav_init_file(&wav, wav_path, NULL)) return false;

    // "auto" or "sentry": Stream through Parallel Sentry correlators
    if (std::string(target_modem_id) == "auto" || std::string(target_modem_id) == "sentry") {
        std::vector<float> buffer(4096);
        while (true) {
            drwav_uint64 framesRead = drwav_read_pcm_frames_f32(&wav, 4096, buffer.data());
            if (framesRead == 0) break;
            hal::AudioHAL::get().inject_rx_samples(buffer.data(), framesRead);
        }
        drwav_uninit(&wav);
        return true;
    }

    auto modem = ModemRegistry::get().instantiate(target_modem_id);
    if (!modem) {
        drwav_uninit(&wav);
        return false;
    }

    modem->init(wav.sampleRate, event_callback_);

    std::vector<float> buffer(4096);
    while (true) {
        drwav_uint64 framesRead = drwav_read_pcm_frames_f32(&wav, 4096, buffer.data());
        if (framesRead == 0) break;
        modem->process_rx(buffer.data(), framesRead);
    }

    drwav_uninit(&wav);
    return true;
}

const char* SentryCoordinator::get_active_protocol_id() const {
    return active_protocol_id_.c_str();
}

} // namespace core
