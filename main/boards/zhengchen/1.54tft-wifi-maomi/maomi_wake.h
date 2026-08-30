#pragma once

#include "device_state.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace maomi {

constexpr uint64_t kWakeCooldownMs = 2000;
constexpr uint64_t kWakePlaybackTimeoutMs = 5000;
constexpr uint64_t kWakeOutputDrainMs = 80;
constexpr uint64_t kWakeOfficialStartTimeoutMs = 500;

enum class WakeHandleResult : uint8_t {
    kPassThrough,
    kStarted,
    kSuppressed,
};

enum class WakePlaybackResult : uint8_t {
    kLocalStarted,
    kFallbackCompleted,
    kFailed,
};

struct WakePlaybackStart {
    WakePlaybackResult result = WakePlaybackResult::kFailed;
    uint32_t playback_id = 0;
};

enum class WakePhase : uint8_t {
    kIdle,
    kPlayingResponse,
    kDrainingOutput,
    kAwaitingOfficial,
};

enum class WakeLogEvent : uint8_t {
    kLocalStarted,
    kFallbackCompleted,
    kDuplicateSuppressed,
    kPlaybackFailed,
    kPlaybackTimedOut,
    kOfficialInvoked,
    kOfficialCompleted,
    kRecovered,
    kAbandonedForOfficialState,
};

struct WakeSnapshot {
    WakePhase phase = WakePhase::kIdle;
    uint32_t sequence_id = 0;
    uint32_t started_count = 0;
    uint32_t fallback_count = 0;
    uint32_t duplicate_count = 0;
    uint32_t playback_failure_count = 0;
    uint32_t official_invoke_count = 0;
    uint32_t recovery_count = 0;
    uint8_t pending_operations = 0;
};

struct WakeDependencies {
    std::function<void()> stop_voice_upload;
    std::function<WakePlaybackStart()> start_local_response;
    std::function<void()> cancel_playback;
    std::function<bool(const std::string&)> invoke_official;
    std::function<void()> abort_official;
    std::function<void()> restore_wake_detection;
    std::function<void(WakeLogEvent, const WakeSnapshot&)> logger;
};

class WakePollGate {
public:
    bool TryAcquire() {
        bool expected = false;
        return pending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    }

    void Release() { pending_.store(false, std::memory_order_release); }
    bool IsPending() const { return pending_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> pending_{false};
};

class WakeSequence {
public:
    explicit WakeSequence(WakeDependencies dependencies);
    WakeSequence(const WakeSequence&) = delete;
    WakeSequence& operator=(const WakeSequence&) = delete;

    // Runs on the application main task. Passing through preserves the official
    // behavior for setup, activation, OTA, and every non-idle state.
    WakeHandleResult HandleWakeWord(const std::string& wake_word, DeviceState state,
                                    uint64_t now_ms);
    void HandlePlaybackFinished(uint64_t now_ms, DeviceState state, uint32_t playback_id);
    void Poll(uint64_t now_ms, DeviceState state, bool playback_idle);

    bool IsBusy() const { return busy_.load(std::memory_order_acquire); }
    WakeSnapshot GetSnapshot() const { return snapshot_; }

private:
    WakeDependencies dependencies_;
    std::atomic<bool> busy_{false};
    WakeSnapshot snapshot_;
    std::string pending_wake_word_;
    uint64_t last_accepted_ms_ = 0;
    uint64_t phase_started_ms_ = 0;
    uint32_t expected_playback_id_ = 0;
    bool has_last_accepted_ = false;
    bool official_progress_seen_ = false;

    bool IsInCooldown(uint64_t now_ms) const;
    static bool HasElapsed(uint64_t now_ms, uint64_t started_ms, uint64_t duration_ms);
    void SetPhase(WakePhase phase, uint64_t now_ms);
    void BeginOfficial(uint64_t now_ms, DeviceState state);
    void CompleteOfficial();
    void Recover(DeviceState state);
    void AbandonForOfficialState();
    void Log(WakeLogEvent event) const;
};

}  // namespace maomi
