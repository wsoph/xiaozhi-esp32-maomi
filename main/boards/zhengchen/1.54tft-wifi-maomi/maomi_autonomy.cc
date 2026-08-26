#include "maomi_autonomy.h"

#include <limits>

namespace maomi {
namespace {

constexpr uint32_t kDefaultRandomSeed = 0xA341316Cu;
constexpr uint32_t kSoundSeedSalt = 0x9E3779B9u;

}  // namespace

AutonomyController::AutonomyController(uint32_t random_seed)
    : random_state_(random_seed == 0 ? kDefaultRandomSeed : random_seed),
      sound_random_state_(random_state_ ^ kSoundSeedSalt) {
    if (sound_random_state_ == 0) {
        sound_random_state_ = kDefaultRandomSeed;
    }
}

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
        CancelSound(&decision);
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

    bool quiet_just_disabled = false;
    if (!has_manual_quiet_state_) {
        has_manual_quiet_state_ = true;
    } else {
        quiet_just_disabled = previous_manual_quiet_ && !inputs.manual_quiet;
    }
    previous_manual_quiet_ = inputs.manual_quiet;
    UpdateProactiveMeowBudget(inputs.clock);
    if (quiet_just_disabled) {
        snapshot_.next_meow_evaluation_ms =
            SafeAdd(monotonic_ms, kProactiveMeowEvaluationIntervalMs);
    }

    if (inputs.activity != ActivitySource::kNone) {
        if (!IsValidActivity(inputs.activity)) {
            return decision;
        }
        CancelAction(&decision);
        CancelSound(&decision);
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
        CancelSound(&decision);
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
        CancelSound(&decision);
        snapshot_.drowsy = true;
        snapshot_.low_brightness = true;
        snapshot_.next_blink_ms = 0;
        snapshot_.next_look_ms = 0;
        snapshot_.next_breath_ms = 0;
        decision.enter_low_brightness = true;
        StartAction(AutonomyAction::kBecomeSleepy, monotonic_ms, &decision);
        return decision;
    }

    if (!quiet_just_disabled && MaybeStartProactiveMeow(monotonic_ms, inputs, &decision)) {
        return decision;
    }

    FinishAction(monotonic_ms);
    FinishSound(monotonic_ms);
    if (snapshot_.active_action != AutonomyAction::kNone ||
        snapshot_.active_sound != AutonomySound::kNone) {
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

bool AutonomyController::IsTrustedClock(const ClockSnapshot& clock) {
    return clock.valid && clock.local_time.year >= ReliableClock::kMinimumTrustedYear &&
           clock.date_key == ReliableClock::EncodeDate(clock.local_time);
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

uint32_t AutonomyController::NextSoundRandom() {
    uint32_t value = sound_random_state_;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    sound_random_state_ = value;
    return value;
}

uint64_t AutonomyController::RandomInterval(uint64_t minimum_ms, uint64_t maximum_ms) {
    const uint64_t span = maximum_ms - minimum_ms + 1;
    return minimum_ms + static_cast<uint64_t>(NextRandom()) % span;
}

bool AutonomyController::IsEligibleForProactiveMeow(uint64_t now_ms,
                                                    const AutonomyInputs& inputs) const {
    return IsTrustedClock(inputs.clock) &&
           ReliableClock::IsWithinDailyWindow(inputs.clock.local_time, 8, 0, 22, 0) &&
           !inputs.charging && inputs.battery_level > 20 && !inputs.manual_quiet &&
           snapshot_.proactive_meows_today < kProactiveMeowDailyLimit &&
           (!has_proactive_meow_ || HasElapsed(now_ms, snapshot_.last_proactive_meow_ms,
                                               kProactiveMeowMinimumIntervalMs)) &&
           HasElapsed(now_ms, snapshot_.last_activity_ms, kProactiveMeowFirstEvaluationMs);
}

void AutonomyController::UpdateProactiveMeowBudget(const ClockSnapshot& clock) {
    if (!IsTrustedClock(clock) || clock.date_key <= snapshot_.proactive_meow_budget_date) {
        return;
    }
    snapshot_.proactive_meow_budget_date = clock.date_key;
    snapshot_.proactive_meows_today = 0;
}

bool AutonomyController::MaybeStartProactiveMeow(uint64_t now_ms, const AutonomyInputs& inputs,
                                                 AutonomyDecision* decision) {
    if (snapshot_.next_meow_evaluation_ms == 0 || now_ms < snapshot_.next_meow_evaluation_ms) {
        return false;
    }
    snapshot_.next_meow_evaluation_ms = SafeAdd(now_ms, kProactiveMeowEvaluationIntervalMs);
    if (!IsEligibleForProactiveMeow(now_ms, inputs)) {
        return false;
    }
    ++snapshot_.proactive_meow_evaluations;
    if (NextSoundRandom() % 100 >= kProactiveMeowChancePercent) {
        return false;
    }
    CancelAction(decision);
    StartSound(AutonomySound::kPlayLocalMeow, now_ms, decision);
    has_proactive_meow_ = true;
    snapshot_.last_proactive_meow_ms = now_ms;
    ++snapshot_.proactive_meows_today;
    ++snapshot_.proactive_meows_started;
    return true;
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
    snapshot_.next_meow_evaluation_ms = SafeAdd(now_ms, kProactiveMeowFirstEvaluationMs);
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

void AutonomyController::StartSound(AutonomySound sound, uint64_t now_ms,
                                    AutonomyDecision* decision) {
    snapshot_.active_sound = sound;
    sound_until_ms_ = SafeAdd(now_ms, kLocalMeowActionDurationMs);
    decision->started_sound = sound;
}

void AutonomyController::CancelSound(AutonomyDecision* decision) {
    if (snapshot_.active_sound == AutonomySound::kNone) {
        return;
    }
    decision->stopped_sound = snapshot_.active_sound;
    snapshot_.active_sound = AutonomySound::kNone;
    sound_until_ms_ = 0;
}

void AutonomyController::FinishSound(uint64_t now_ms) {
    if (snapshot_.active_sound == AutonomySound::kNone || now_ms < sound_until_ms_) {
        return;
    }
    snapshot_.active_sound = AutonomySound::kNone;
    sound_until_ms_ = 0;
    if (snapshot_.drowsy) {
        snapshot_.next_breath_ms =
            SafeAdd(now_ms, RandomInterval(kBreathMinimumIntervalMs, kBreathMaximumIntervalMs));
    }
}

}  // namespace maomi
