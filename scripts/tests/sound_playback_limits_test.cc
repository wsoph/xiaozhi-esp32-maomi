#include "audio/sound_playback_limits.h"

#include <cassert>
#include <iostream>

int main() {
    assert(NonBlockingSoundLimits::AcceptsContainerSize(1));
    assert(
        NonBlockingSoundLimits::AcceptsContainerSize(NonBlockingSoundLimits::kMaxContainerBytes));
    assert(!NonBlockingSoundLimits::AcceptsContainerSize(0));
    assert(!NonBlockingSoundLimits::AcceptsContainerSize(
        NonBlockingSoundLimits::kMaxContainerBytes + 1));

    NonBlockingSoundBudget packet_budget;
    for (size_t packet = 0; packet < NonBlockingSoundLimits::kMaxPackets; ++packet) {
        assert(packet_budget.TryAddPacket(1, 5));
    }
    assert(!packet_budget.TryAddPacket(1, 5));

    NonBlockingSoundBudget duration_budget;
    for (uint32_t duration = 0; duration < NonBlockingSoundLimits::kMaxDurationMs;
         duration += 120) {
        assert(duration_budget.TryAddPacket(1, 120));
    }
    assert(!duration_budget.TryAddPacket(1, 5));

    NonBlockingSoundBudget payload_budget;
    assert(payload_budget.TryAddPacket(NonBlockingSoundLimits::kMaxPayloadBytes, 5));
    assert(!payload_budget.TryAddPacket(1, 5));
    std::cout << "sound_playback_limits tests passed" << std::endl;
    return 0;
}
