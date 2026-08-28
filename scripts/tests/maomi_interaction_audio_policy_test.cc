#include "maomi_interaction_audio_policy.h"

#include <cassert>
#include <cstdint>
#include <optional>

namespace {

enum class TestAction : uint8_t {
    kPet,
    kFeed,
};

}  // namespace

int main() {
    using maomi::InteractionSoundWaitDecision;

    constexpr uint64_t kMaxWaitMs = 15'000;
    constexpr uint64_t kQueuedAtMs = 1'000;

    assert(maomi::DecideInteractionSoundWait(false, kQueuedAtMs, 20'000, kMaxWaitMs) ==
           InteractionSoundWaitDecision::kNotPending);

    // Reproduces RC2: the cloud reply keeps the audio queue busy beyond the old
    // four-second animation window. The local meow must remain pending.
    assert(maomi::DecideInteractionSoundWait(true, kQueuedAtMs, 6'000, kMaxWaitMs) ==
           InteractionSoundWaitDecision::kKeepWaiting);

    assert(maomi::DecideInteractionSoundWait(true, kQueuedAtMs, 16'000, kMaxWaitMs) ==
           InteractionSoundWaitDecision::kTimedOut);

    // A monotonic clock rollback must not turn into an unsigned timeout.
    assert(maomi::DecideInteractionSoundWait(true, kQueuedAtMs, 999, kMaxWaitMs) ==
           InteractionSoundWaitDecision::kKeepWaiting);

    // A newer accepted interaction supersedes an older sound request. Keeping
    // the old action would no longer match the newly displayed animation and
    // would make both requests time out without a meow.
    std::optional<TestAction> pending_action = TestAction::kPet;
    uint64_t pending_since_ms = kQueuedAtMs;
    assert(maomi::QueueLatestInteractionSound(true, TestAction::kFeed, 2'000, pending_action,
                                              pending_since_ms));
    assert(pending_action == TestAction::kFeed);
    assert(pending_since_ms == 2'000);
    return 0;
}
