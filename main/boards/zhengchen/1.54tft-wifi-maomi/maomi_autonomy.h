#pragma once

#include <cstdint>

namespace maomi {

constexpr uint64_t kBlinkMinimumIntervalMs = 8'000;
constexpr uint64_t kBlinkMaximumIntervalMs = 20'000;
constexpr uint64_t kLookMinimumIntervalMs = 20'000;
constexpr uint64_t kLookMaximumIntervalMs = 60'000;
constexpr uint64_t kDrowsyAfterMs = 60'000;
constexpr uint64_t kBreathMinimumIntervalMs = 4'000;
constexpr uint64_t kBreathMaximumIntervalMs = 8'000;
constexpr uint64_t kBlinkActionDurationMs = 400;
constexpr uint64_t kLookActionDurationMs = 1'200;
constexpr uint64_t kSleepyActionDurationMs = 1'500;
constexpr uint64_t kBreathActionDurationMs = 1'600;

enum class AutonomyAction : uint8_t {
    kNone,
    kBlink,
    kLookAround,
    kBecomeSleepy,
    kSleepBreath,
};

enum class ActivitySource : uint8_t {
    kNone,
    kWakeWord,
    kButton,
    kReminder,
    kPetInteraction,
};

struct AutonomyInputs {
    bool official_idle = false;
    bool higher_priority_active = false;
    ActivitySource activity = ActivitySource::kNone;
};

struct AutonomyDecision {
    AutonomyAction started_action = AutonomyAction::kNone;
    AutonomyAction stopped_action = AutonomyAction::kNone;
    bool enter_low_brightness = false;
    bool restore_display = false;
};

struct AutonomySnapshot {
    AutonomyAction active_action = AutonomyAction::kNone;
    bool paused = true;
    bool drowsy = false;
    bool low_brightness = false;
    uint64_t last_activity_ms = 0;
    uint64_t next_blink_ms = 0;
    uint64_t next_look_ms = 0;
    uint64_t next_breath_ms = 0;
    uint32_t actions_started = 0;
    uint32_t actions_cancelled = 0;
    uint32_t random_draws = 0;
    uint8_t maximum_concurrent_actions = 0;
};

// Main-task-owned, allocation-free policy for silent autonomous animation. Call Update from
// activity and priority-state events as well as from a periodic poll no slower than 200 ms.
// The returned decision is a command for the board adapter; this policy does not touch the UI,
// audio path, wake detector, or power-saving hardware directly.
class AutonomyController {
public:
    explicit AutonomyController(uint32_t random_seed);
    AutonomyController(const AutonomyController&) = delete;
    AutonomyController& operator=(const AutonomyController&) = delete;

    AutonomyDecision Update(uint64_t monotonic_ms, const AutonomyInputs& inputs);
    AutonomySnapshot GetSnapshot() const;

private:
    uint32_t random_state_ = 0;
    bool initialized_ = false;
    uint64_t last_update_ms_ = 0;
    uint64_t active_until_ms_ = 0;
    AutonomySnapshot snapshot_;

    static bool IsValidActivity(ActivitySource source);
    static uint64_t SafeAdd(uint64_t value, uint64_t increment);
    static bool HasElapsed(uint64_t now_ms, uint64_t start_ms, uint64_t duration_ms);
    static uint64_t DurationFor(AutonomyAction action);

    uint32_t NextRandom();
    uint64_t RandomInterval(uint64_t minimum_ms, uint64_t maximum_ms);
    void ResetIdleTiming(uint64_t now_ms);
    void StartAction(AutonomyAction action, uint64_t now_ms, AutonomyDecision* decision);
    void CancelAction(AutonomyDecision* decision);
    void FinishAction(uint64_t now_ms);
};

}  // namespace maomi
