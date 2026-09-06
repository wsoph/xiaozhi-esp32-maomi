#include "maomi_timing.h"

#include <algorithm>

namespace maomi {

TimingStatus Stopwatch::Start(uint64_t monotonic_ms) {
    if (active_) {
        return TimingStatus::kInvalidState;
    }
    active_ = true;
    state_ = TimingState::kRunning;
    started_at_ms_ = monotonic_ms;
    accumulated_ms_ = 0;
    return TimingStatus::kAccepted;
}

TimingStatus Stopwatch::Pause(uint64_t monotonic_ms) {
    if (!active_ || state_ != TimingState::kRunning) {
        return TimingStatus::kInvalidState;
    }
    accumulated_ms_ = Get(monotonic_ms).elapsed_ms;
    state_ = TimingState::kPaused;
    return TimingStatus::kAccepted;
}

TimingStatus Stopwatch::Resume(uint64_t monotonic_ms) {
    if (!active_ || state_ != TimingState::kPaused) {
        return TimingStatus::kInvalidState;
    }
    started_at_ms_ = monotonic_ms;
    state_ = TimingState::kRunning;
    return TimingStatus::kAccepted;
}

TimingStatus Stopwatch::Reset(uint64_t monotonic_ms) {
    if (!active_) {
        return TimingStatus::kInvalidState;
    }
    accumulated_ms_ = 0;
    started_at_ms_ = monotonic_ms;
    return TimingStatus::kAccepted;
}

TimingStatus Stopwatch::Stop() {
    if (!active_) {
        return TimingStatus::kInvalidState;
    }
    active_ = false;
    state_ = TimingState::kStopped;
    started_at_ms_ = 0;
    accumulated_ms_ = 0;
    return TimingStatus::kAccepted;
}

StopwatchSnapshot Stopwatch::Get(uint64_t monotonic_ms) const {
    if (!active_) {
        return {};
    }
    uint64_t elapsed_ms = accumulated_ms_;
    if (state_ == TimingState::kRunning && monotonic_ms >= started_at_ms_) {
        const uint64_t running_ms = monotonic_ms - started_at_ms_;
        elapsed_ms = running_ms > kMaximumStopwatchElapsedMs -
                                      std::min(accumulated_ms_, kMaximumStopwatchElapsedMs)
                         ? kMaximumStopwatchElapsedMs
                         : accumulated_ms_ + running_ms;
    }
    return {
        .active = true,
        .state = state_,
        .elapsed_ms = std::min(elapsed_ms, kMaximumStopwatchElapsedMs),
    };
}

}  // namespace maomi
