#include "maomi_autonomy.h"

#include <limits>

namespace maomi {
namespace {

constexpr uint32_t kDefaultRandomSeed = 0xA341316Cu;

}  // namespace

AutonomyController::AutonomyController(uint32_t random_seed)
    : random_state_(random_seed == 0 ? kDefaultRandomSeed : random_seed) {}

AutonomyDecision AutonomyController::Update(uint64_t monotonic_ms, const AutonomyInputs& inputs) {
    AutonomyDecision decision;
    const bool eligible = inputs.official_idle && !inputs.higher_priority_active;

    if (!initialized_) {
        initialized_ = true;
        last_update_ms_ = monotonic_ms;
        ResetIdleTiming(monotonic_ms);
        snapshot_.paused = !eligible;
        if (!eligible) {
            snapshot_.next_blink_ms = 0;
            snapshot_.next_look_ms = 0;
        }
    } else if (monotonic_ms < last_update_ms_) {
        CancelAction(&decision);
        decision.restore_display = snapshot_.low_brightness;
        ResetIdleTiming(monotonic_ms);
        snapshot_.paused = !eligible;
        if (!eligible) {
            snapshot_.next_blink_ms = 0;
            snapshot_.next_look_ms = 0;
        }
        last_update_ms_ = monotonic_ms;
        return decision;
    }
    last_update_ms_ = monotonic_ms;

    if (inputs.activity != ActivitySource::kNone) {
        if (!IsValidActivity(inputs.activity)) {
            return decision;
        }
        CancelAction(&decision);
        decision.restore_display = snapshot_.low_brightness;
        ResetIdleTiming(monotonic_ms);
        snapshot_.paused = !eligible;
        if (!eligible) {
            snapshot_.next_blink_ms = 0;
            snapshot_.next_look_ms = 0;
        }
        return decision;
    }

    if (!eligible) {
        CancelAction(&decision);
        decision.restore_display = snapshot_.low_brightness;
        if (!snapshot_.paused) {
            snapshot_.drowsy = false;
            snapshot_.low_brightness = false;
            snapshot_.next_blink_ms = 0;
            snapshot_.next_look_ms = 0;
            snapshot_.next_breath_ms = 0;
        }
        snapshot_.paused = true;
        snapshot_.last_activity_ms = monotonic_ms;
        return decision;
    }

    if (snapshot_.paused) {
        ResetIdleTiming(monotonic_ms);
        snapshot_.paused = false;
        return decision;
    }

    if (!snapshot_.drowsy && HasElapsed(monotonic_ms, snapshot_.last_activity_ms, kDrowsyAfterMs)) {
        CancelAction(&decision);
        snapshot_.drowsy = true;
        snapshot_.low_brightness = true;
        snapshot_.next_blink_ms = 0;
        snapshot_.next_look_ms = 0;
        snapshot_.next_breath_ms = 0;
        decision.enter_low_brightness = true;
        StartAction(AutonomyAction::kBecomeSleepy, monotonic_ms, &decision);
        return decision;
    }

    FinishAction(monotonic_ms);
    if (snapshot_.active_action != AutonomyAction::kNone) {
        return decision;
    }

    if (snapshot_.drowsy) {
        if (snapshot_.next_breath_ms != 0 && monotonic_ms >= snapshot_.next_breath_ms) {
            StartAction(AutonomyAction::kSleepBreath, monotonic_ms, &decision);
        }
        return decision;
    }

    const bool blink_due = monotonic_ms >= snapshot_.next_blink_ms;
    const bool look_due = monotonic_ms >= snapshot_.next_look_ms;
    if (blink_due && (!look_due || snapshot_.next_blink_ms <= snapshot_.next_look_ms)) {
        snapshot_.next_blink_ms =
            SafeAdd(monotonic_ms, RandomInterval(kBlinkMinimumIntervalMs, kBlinkMaximumIntervalMs));
        StartAction(AutonomyAction::kBlink, monotonic_ms, &decision);
    } else if (look_due) {
        snapshot_.next_look_ms =
            SafeAdd(monotonic_ms, RandomInterval(kLookMinimumIntervalMs, kLookMaximumIntervalMs));
        StartAction(AutonomyAction::kLookAround, monotonic_ms, &decision);
    }
    return decision;
}

AutonomySnapshot AutonomyController::GetSnapshot() const { return snapshot_; }

bool AutonomyController::IsValidActivity(ActivitySource source) {
    return source >= ActivitySource::kWakeWord && source <= ActivitySource::kPetInteraction;
}

uint64_t AutonomyController::SafeAdd(uint64_t value, uint64_t increment) {
    if (increment > std::numeric_limits<uint64_t>::max() - value) {
        return std::numeric_limits<uint64_t>::max();
    }
    return value + increment;
}

bool AutonomyController::HasElapsed(uint64_t now_ms, uint64_t start_ms, uint64_t duration_ms) {
    return now_ms >= start_ms && now_ms - start_ms >= duration_ms;
}

uint64_t AutonomyController::DurationFor(AutonomyAction action) {
    switch (action) {
        case AutonomyAction::kBlink:
            return kBlinkActionDurationMs;
        case AutonomyAction::kLookAround:
            return kLookActionDurationMs;
        case AutonomyAction::kBecomeSleepy:
            return kSleepyActionDurationMs;
        case AutonomyAction::kSleepBreath:
            return kBreathActionDurationMs;
        case AutonomyAction::kNone:
            return 0;
    }
    return 0;
}

uint32_t AutonomyController::NextRandom() {
    uint32_t value = random_state_;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state_ = value;
    ++snapshot_.random_draws;
    return value;
}

uint64_t AutonomyController::RandomInterval(uint64_t minimum_ms, uint64_t maximum_ms) {
    const uint64_t span = maximum_ms - minimum_ms + 1;
    return minimum_ms + static_cast<uint64_t>(NextRandom()) % span;
}

void AutonomyController::ResetIdleTiming(uint64_t now_ms) {
    snapshot_.drowsy = false;
    snapshot_.low_brightness = false;
    snapshot_.last_activity_ms = now_ms;
    snapshot_.next_blink_ms =
        SafeAdd(now_ms, RandomInterval(kBlinkMinimumIntervalMs, kBlinkMaximumIntervalMs));
    snapshot_.next_look_ms =
        SafeAdd(now_ms, RandomInterval(kLookMinimumIntervalMs, kLookMaximumIntervalMs));
    snapshot_.next_breath_ms = 0;
}

void AutonomyController::StartAction(AutonomyAction action, uint64_t now_ms,
                                     AutonomyDecision* decision) {
    snapshot_.active_action = action;
    active_until_ms_ = SafeAdd(now_ms, DurationFor(action));
    ++snapshot_.actions_started;
    snapshot_.maximum_concurrent_actions = 1;
    decision->started_action = action;
}

void AutonomyController::CancelAction(AutonomyDecision* decision) {
    if (snapshot_.active_action == AutonomyAction::kNone) {
        return;
    }
    decision->stopped_action = snapshot_.active_action;
    snapshot_.active_action = AutonomyAction::kNone;
    active_until_ms_ = 0;
    ++snapshot_.actions_cancelled;
}

void AutonomyController::FinishAction(uint64_t now_ms) {
    if (snapshot_.active_action == AutonomyAction::kNone || now_ms < active_until_ms_) {
        return;
    }
    const auto completed = snapshot_.active_action;
    snapshot_.active_action = AutonomyAction::kNone;
    active_until_ms_ = 0;
    if (completed == AutonomyAction::kBecomeSleepy || completed == AutonomyAction::kSleepBreath) {
        snapshot_.next_breath_ms =
            SafeAdd(now_ms, RandomInterval(kBreathMinimumIntervalMs, kBreathMaximumIntervalMs));
    }
}

}  // namespace maomi
