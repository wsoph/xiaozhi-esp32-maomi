#pragma once

#include <cstddef>
#include <cstdint>

struct NonBlockingSoundLimits {
    static constexpr size_t kMaxPackets = 600;
    static constexpr uint32_t kMaxDurationMs = 3000;
    static constexpr size_t kMaxPayloadBytes = 128 * 1024;
    static constexpr size_t kMaxContainerBytes = 128 * 1024;

    static constexpr bool AcceptsContainerSize(size_t size) {
        return size > 0 && size <= kMaxContainerBytes;
    }
};

class NonBlockingSoundBudget {
public:
    bool TryAddPacket(size_t payload_bytes, uint32_t duration_ms) {
        if (duration_ms == 0 || packet_count_ >= NonBlockingSoundLimits::kMaxPackets ||
            duration_ms > NonBlockingSoundLimits::kMaxDurationMs - duration_ms_ ||
            payload_bytes > NonBlockingSoundLimits::kMaxPayloadBytes - payload_bytes_) {
            return false;
        }
        ++packet_count_;
        duration_ms_ += duration_ms;
        payload_bytes_ += payload_bytes;
        return true;
    }

private:
    size_t packet_count_ = 0;
    uint32_t duration_ms_ = 0;
    size_t payload_bytes_ = 0;
};
