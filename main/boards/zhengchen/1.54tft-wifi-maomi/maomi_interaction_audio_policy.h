#pragma once

#include <cstdint>
#include <optional>

namespace maomi {

enum class InteractionSoundWaitDecision : uint8_t {
    kNotPending,
    kKeepWaiting,
    kTimedOut,
};

template <typename Action>
inline bool QueueLatestInteractionSound(bool sound_available, Action action, uint64_t queued_at_ms,
                                        std::optional<Action>& pending_action,
                                        uint64_t& pending_since_ms) {
    if (!sound_available) {
        return false;
    }
    pending_action = action;
    pending_since_ms = queued_at_ms;
    return true;
}

inline InteractionSoundWaitDecision DecideInteractionSoundWait(bool pending, uint64_t queued_at_ms,
                                                               uint64_t monotonic_ms,
                                                               uint64_t max_wait_ms) {
    if (!pending) {
        return InteractionSoundWaitDecision::kNotPending;
    }
    if (monotonic_ms < queued_at_ms || monotonic_ms - queued_at_ms < max_wait_ms) {
        return InteractionSoundWaitDecision::kKeepWaiting;
    }
    return InteractionSoundWaitDecision::kTimedOut;
}

}  // namespace maomi
