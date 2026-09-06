#pragma once

#include <cstdint>

namespace maomi {

constexpr uint64_t kMaximumStopwatchElapsedMs = (99ULL * 60 * 60 + 59 * 60 + 59) * 1000;

enum class TimingState : uint8_t {
    kStopped,
    kRunning,
    kPaused,
};

enum class TimingStatus : uint8_t {
    kAccepted,
    kInvalidState,
};

struct StopwatchSnapshot {
    bool active = false;
    TimingState state = TimingState::kStopped;
    uint64_t elapsed_ms = 0;
};

class Stopwatch {
public:
    TimingStatus Start(uint64_t monotonic_ms);
    TimingStatus Pause(uint64_t monotonic_ms);
    TimingStatus Resume(uint64_t monotonic_ms);
    TimingStatus Reset(uint64_t monotonic_ms);
    TimingStatus Stop();

    StopwatchSnapshot Get(uint64_t monotonic_ms) const;

private:
    bool active_ = false;
    TimingState state_ = TimingState::kRunning;
    uint64_t started_at_ms_ = 0;
    uint64_t accumulated_ms_ = 0;
};

}  // namespace maomi
