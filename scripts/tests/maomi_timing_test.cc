#include "maomi_timing.h"

#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void Fail(const char* expression, int line) {
    std::cerr << "FAILED line " << line << ": " << expression << std::endl;
    std::exit(1);
}

#define CHECK(expression)                \
    do {                                 \
        if (!(expression)) {             \
            Fail(#expression, __LINE__); \
        }                                \
    } while (false)

void TestStopwatchStartsAtZeroAndCountsMonotonicTime() {
    maomi::Stopwatch stopwatch;
    CHECK(stopwatch.Start(1000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Get(1000).active);
    CHECK(stopwatch.Get(1000).state == maomi::TimingState::kRunning);
    CHECK(stopwatch.Get(1000).elapsed_ms == 0);
    CHECK(stopwatch.Get(4500).elapsed_ms == 3500);
    CHECK(stopwatch.Start(5000) == maomi::TimingStatus::kInvalidState);
}

void TestPauseResumeExcludesPausedTime() {
    maomi::Stopwatch stopwatch;
    CHECK(stopwatch.Pause(0) == maomi::TimingStatus::kInvalidState);
    CHECK(stopwatch.Start(1000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Pause(4000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Get(9000).state == maomi::TimingState::kPaused);
    CHECK(stopwatch.Get(9000).elapsed_ms == 3000);
    CHECK(stopwatch.Pause(9000) == maomi::TimingStatus::kInvalidState);
    CHECK(stopwatch.Resume(10000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Get(12000).state == maomi::TimingState::kRunning);
    CHECK(stopwatch.Get(12000).elapsed_ms == 5000);
    CHECK(stopwatch.Resume(12000) == maomi::TimingStatus::kInvalidState);
}

void TestResetPreservesRunningOrPausedState() {
    maomi::Stopwatch stopwatch;
    CHECK(stopwatch.Reset(0) == maomi::TimingStatus::kInvalidState);
    CHECK(stopwatch.Start(1000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Reset(4000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Get(4000).state == maomi::TimingState::kRunning);
    CHECK(stopwatch.Get(4500).elapsed_ms == 500);
    CHECK(stopwatch.Pause(5000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Reset(6000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Get(9000).state == maomi::TimingState::kPaused);
    CHECK(stopwatch.Get(9000).elapsed_ms == 0);
}

void TestStopClearsSessionAndAllowsRestart() {
    maomi::Stopwatch stopwatch;
    CHECK(stopwatch.Stop() == maomi::TimingStatus::kInvalidState);
    CHECK(stopwatch.Start(1000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Stop() == maomi::TimingStatus::kAccepted);
    CHECK(!stopwatch.Get(5000).active);
    CHECK(stopwatch.Get(5000).state == maomi::TimingState::kStopped);
    CHECK(stopwatch.Get(5000).elapsed_ms == 0);
    CHECK(stopwatch.Start(6000) == maomi::TimingStatus::kAccepted);
}

void TestClockRollbackCannotUnderflowAndElapsedSaturates() {
    maomi::Stopwatch stopwatch;
    CHECK(stopwatch.Start(1000) == maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Get(500).elapsed_ms == 0);
    CHECK(stopwatch.Get(1000 + maomi::kMaximumStopwatchElapsedMs + 1000).elapsed_ms ==
          maomi::kMaximumStopwatchElapsedMs);
    CHECK(stopwatch.Pause(1000 + maomi::kMaximumStopwatchElapsedMs + 1000) ==
          maomi::TimingStatus::kAccepted);
    CHECK(stopwatch.Get(UINT64_MAX).elapsed_ms == maomi::kMaximumStopwatchElapsedMs);
}

}  // namespace

int main() {
    TestStopwatchStartsAtZeroAndCountsMonotonicTime();
    TestPauseResumeExcludesPausedTime();
    TestResetPreservesRunningOrPausedState();
    TestStopClearsSessionAndAllowsRestart();
    TestClockRollbackCannotUnderflowAndElapsedSaturates();
    std::cout << "maomi timing tests passed" << std::endl;
    return 0;
}
