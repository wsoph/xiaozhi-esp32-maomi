#include "maomi_wake.h"

#include <utility>

namespace maomi {

WakeSequence::WakeSequence(WakeDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

WakeHandleResult WakeSequence::HandleWakeWord(const std::string& wake_word, DeviceState state,
                                              uint64_t now_ms) {
    if (state != kDeviceStateIdle) {
        return WakeHandleResult::kPassThrough;
    }

    if (IsBusy()) {
        ++snapshot_.duplicate_count;
        Log(WakeLogEvent::kDuplicateSuppressed);
        return WakeHandleResult::kSuppressed;
    }

    if (IsInCooldown(now_ms)) {
        ++snapshot_.duplicate_count;
        if (dependencies_.restore_wake_detection) {
            dependencies_.restore_wake_detection();
        }
        Log(WakeLogEvent::kDuplicateSuppressed);
        return WakeHandleResult::kSuppressed;
    }

    if (!dependencies_.stop_voice_upload || !dependencies_.start_local_response ||
        !dependencies_.invoke_official || !dependencies_.restore_wake_detection) {
        if (dependencies_.restore_wake_detection) {
            dependencies_.restore_wake_detection();
        }
        return WakeHandleResult::kSuppressed;
    }

    has_last_accepted_ = true;
    last_accepted_ms_ = now_ms;
    pending_wake_word_ = wake_word;
    official_progress_seen_ = false;
    expected_playback_id_ = 0;
    ++snapshot_.sequence_id;
    ++snapshot_.started_count;
    SetPhase(WakePhase::kPlayingResponse, now_ms);

    dependencies_.stop_voice_upload();
    const auto playback = dependencies_.start_local_response();
    expected_playback_id_ = playback.playback_id;
    if (playback.result == WakePlaybackResult::kFallbackCompleted) {
        ++snapshot_.fallback_count;
        Log(WakeLogEvent::kFallbackCompleted);
        BeginOfficial(now_ms, state);
    } else if (playback.playback_id == 0) {
        ++snapshot_.playback_failure_count;
        Log(WakeLogEvent::kPlaybackFailed);
        Recover(state);
    } else if (playback.result == WakePlaybackResult::kLocalStarted) {
        Log(WakeLogEvent::kLocalStarted);
    } else {
        ++snapshot_.playback_failure_count;
        Log(WakeLogEvent::kPlaybackFailed);
        Recover(state);
    }
    return WakeHandleResult::kStarted;
}

void WakeSequence::HandlePlaybackFinished(uint64_t now_ms, DeviceState state,
                                          uint32_t playback_id) {
    if (snapshot_.phase != WakePhase::kPlayingResponse || playback_id == 0 ||
        playback_id != expected_playback_id_) {
        return;
    }
    expected_playback_id_ = 0;
    if (state != kDeviceStateIdle) {
        AbandonForOfficialState();
        return;
    }
    SetPhase(WakePhase::kDrainingOutput, now_ms);
}

void WakeSequence::Poll(uint64_t now_ms, DeviceState state, bool playback_idle) {
    if (snapshot_.phase == WakePhase::kPlayingResponse) {
        // Playback completion is delivered separately by its matching completion
        // callback. The queue may already look idle while that callback is waiting
        // on the main task, so idle alone is not a playback failure signal.
        (void)playback_idle;
        if (HasElapsed(now_ms, phase_started_ms_, kWakePlaybackTimeoutMs)) {
            ++snapshot_.playback_failure_count;
            if (dependencies_.cancel_playback) {
                dependencies_.cancel_playback();
            }
            Log(WakeLogEvent::kPlaybackTimedOut);
            Recover(state);
        }
        return;
    }

    if (snapshot_.phase == WakePhase::kDrainingOutput) {
        if (state != kDeviceStateIdle) {
            AbandonForOfficialState();
        } else if (HasElapsed(now_ms, phase_started_ms_, kWakeOutputDrainMs)) {
            BeginOfficial(now_ms, state);
        }
        return;
    }

    if (snapshot_.phase != WakePhase::kAwaitingOfficial) {
        return;
    }

    if (state == kDeviceStateConnecting) {
        official_progress_seen_ = true;
        if (HasElapsed(now_ms, phase_started_ms_, kWakeOfficialStartTimeoutMs)) {
            if (dependencies_.abort_official) {
                dependencies_.abort_official();
            }
            Recover(kDeviceStateIdle);
        }
    } else if (state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        CompleteOfficial();
    } else if (state == kDeviceStateIdle) {
        if (official_progress_seen_ ||
            HasElapsed(now_ms, phase_started_ms_, kWakeOfficialStartTimeoutMs)) {
            Recover(state);
        }
    } else {
        AbandonForOfficialState();
    }
}

bool WakeSequence::IsInCooldown(uint64_t now_ms) const {
    if (!has_last_accepted_) {
        return false;
    }
    if (now_ms < last_accepted_ms_) {
        return true;
    }
    return now_ms - last_accepted_ms_ < kWakeCooldownMs;
}

bool WakeSequence::HasElapsed(uint64_t now_ms, uint64_t started_ms, uint64_t duration_ms) {
    return now_ms >= started_ms && now_ms - started_ms >= duration_ms;
}

void WakeSequence::SetPhase(WakePhase phase, uint64_t now_ms) {
    snapshot_.phase = phase;
    snapshot_.pending_operations = phase == WakePhase::kIdle ? 0 : 1;
    phase_started_ms_ = now_ms;
    busy_.store(phase != WakePhase::kIdle, std::memory_order_release);
}

void WakeSequence::BeginOfficial(uint64_t now_ms, DeviceState state) {
    SetPhase(WakePhase::kAwaitingOfficial, now_ms);
    ++snapshot_.official_invoke_count;
    Log(WakeLogEvent::kOfficialInvoked);
    if (!dependencies_.invoke_official(pending_wake_word_)) {
        Recover(state);
    }
}

void WakeSequence::CompleteOfficial() {
    pending_wake_word_.clear();
    expected_playback_id_ = 0;
    official_progress_seen_ = false;
    SetPhase(WakePhase::kIdle, phase_started_ms_);
    Log(WakeLogEvent::kOfficialCompleted);
}

void WakeSequence::Recover(DeviceState state) {
    pending_wake_word_.clear();
    expected_playback_id_ = 0;
    official_progress_seen_ = false;
    SetPhase(WakePhase::kIdle, phase_started_ms_);
    ++snapshot_.recovery_count;
    if (state == kDeviceStateIdle && dependencies_.restore_wake_detection) {
        dependencies_.restore_wake_detection();
    }
    Log(WakeLogEvent::kRecovered);
}

void WakeSequence::AbandonForOfficialState() {
    pending_wake_word_.clear();
    expected_playback_id_ = 0;
    official_progress_seen_ = false;
    SetPhase(WakePhase::kIdle, phase_started_ms_);
    Log(WakeLogEvent::kAbandonedForOfficialState);
}

void WakeSequence::Log(WakeLogEvent event) const {
    if (dependencies_.logger) {
        dependencies_.logger(event, snapshot_);
    }
}

}  // namespace maomi
