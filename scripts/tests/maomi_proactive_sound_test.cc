#include "maomi_autonomy.h"

#include <cstdint>
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

maomi::ClockSnapshot ClockAt(int year, int month, int day, int hour, int minute,
                             uint64_t monotonic_ms, bool valid = true) {
    maomi::ClockSnapshot clock;
    clock.valid = valid;
    clock.local_time = {year, month, day, hour, minute, 0};
    clock.monotonic_ms = monotonic_ms;
    clock.date_key = valid ? year * 10'000 + month * 100 + day : 0;
    return clock;
}

maomi::AutonomyInputs EligibleInputs(const maomi::ClockSnapshot& clock) {
    maomi::AutonomyInputs inputs;
    inputs.official_idle = true;
    inputs.battery_level = 100;
    inputs.clock = clock;
    return inputs;
}

uint32_t SimulateMinutes(maomi::AutonomyController* controller, int start_hour,
                         int duration_minutes, maomi::AutonomyInputs inputs) {
    uint32_t meows = 0;
    for (int elapsed = 0; elapsed <= duration_minutes; ++elapsed) {
        const int total_minutes = start_hour * 60 + elapsed;
        const int day = 26 + total_minutes / (24 * 60);
        const int minute_of_day = total_minutes % (24 * 60);
        const uint64_t monotonic_ms = static_cast<uint64_t>(elapsed) * 60'000;
        inputs.clock = ClockAt(2026, 8, day, minute_of_day / 60, minute_of_day % 60, monotonic_ms,
                               inputs.clock.valid);
        const auto decision = controller->Update(monotonic_ms, inputs);
        if (decision.started_sound == maomi::AutonomySound::kPlayLocalMeow) {
            ++meows;
        }
    }
    return meows;
}

void TestQuietHoursAndInvalidTimeNeverMeow() {
    CHECK(maomi::kProactiveMeowChancePercent == 25);
    CHECK(maomi::kProactiveMeowFirstEvaluationMs == 60 * 60 * 1000);
    CHECK(maomi::kProactiveMeowEvaluationIntervalMs == 15 * 60 * 1000);

    maomi::AutonomyController night(101u);
    auto night_inputs = EligibleInputs(ClockAt(2026, 8, 26, 22, 0, 0));
    CHECK(SimulateMinutes(&night, 22, 10 * 60, night_inputs) == 0);

    maomi::AutonomyController invalid_time(101u);
    auto invalid_inputs = EligibleInputs(ClockAt(2026, 8, 26, 9, 0, 0, false));
    CHECK(SimulateMinutes(&invalid_time, 9, 8 * 60, invalid_inputs) == 0);

    maomi::AutonomyController inconsistent_time(101u);
    auto inconsistent_inputs = EligibleInputs(ClockAt(2026, 8, 26, 9, 0, 0));
    uint32_t inconsistent_meows = 0;
    for (int elapsed = 0; elapsed <= 8 * 60; ++elapsed) {
        const uint64_t now_ms = static_cast<uint64_t>(elapsed) * 60'000;
        inconsistent_inputs.clock = ClockAt(2026, 8, 26, 9 + elapsed / 60, elapsed % 60, now_ms);
        inconsistent_inputs.clock.date_key = 0;
        inconsistent_meows += inconsistent_time.Update(now_ms, inconsistent_inputs).started_sound ==
                                      maomi::AutonomySound::kPlayLocalMeow
                                  ? 1u
                                  : 0u;
    }
    CHECK(inconsistent_meows == 0);
}

void TestEveryEligibilityBlockerSuppressesMeow() {
    auto base = EligibleInputs(ClockAt(2026, 8, 26, 9, 0, 0));

    maomi::AutonomyController charging(202u);
    auto charging_inputs = base;
    charging_inputs.charging = true;
    CHECK(SimulateMinutes(&charging, 9, 8 * 60, charging_inputs) == 0);

    maomi::AutonomyController low_battery(202u);
    auto low_inputs = base;
    low_inputs.battery_level = 20;
    CHECK(SimulateMinutes(&low_battery, 9, 8 * 60, low_inputs) == 0);

    maomi::AutonomyController unknown_battery(202u);
    auto unknown_inputs = base;
    unknown_inputs.battery_level = -1;
    CHECK(SimulateMinutes(&unknown_battery, 9, 8 * 60, unknown_inputs) == 0);

    maomi::AutonomyController manual_quiet(202u);
    auto quiet_inputs = base;
    quiet_inputs.manual_quiet = true;
    CHECK(SimulateMinutes(&manual_quiet, 9, 8 * 60, quiet_inputs) == 0);

    maomi::AutonomyController high_priority(202u);
    auto priority_inputs = base;
    priority_inputs.higher_priority_active = true;
    CHECK(SimulateMinutes(&high_priority, 9, 8 * 60, priority_inputs) == 0);

    maomi::AutonomyController official_busy(202u);
    auto busy_inputs = base;
    busy_inputs.official_idle = false;
    CHECK(SimulateMinutes(&official_busy, 9, 8 * 60, busy_inputs) == 0);
}

void TestInteractionUnderSixtyMinutesAndQuietDisableDoNotMeowImmediately() {
    maomi::AutonomyController active(303u);
    auto inputs = EligibleInputs(ClockAt(2026, 8, 26, 9, 0, 0));
    for (int elapsed = 0; elapsed <= 6 * 60; ++elapsed) {
        const uint64_t now_ms = static_cast<uint64_t>(elapsed) * 60'000;
        inputs.clock = ClockAt(2026, 8, 26, 9 + elapsed / 60, elapsed % 60, now_ms);
        inputs.activity = elapsed % 30 == 0 ? maomi::ActivitySource::kPetInteraction
                                            : maomi::ActivitySource::kNone;
        CHECK(active.Update(now_ms, inputs).started_sound != maomi::AutonomySound::kPlayLocalMeow);
    }

    maomi::AutonomyController quiet(404u);
    inputs = EligibleInputs(ClockAt(2026, 8, 26, 9, 0, 0));
    inputs.manual_quiet = true;
    CHECK(SimulateMinutes(&quiet, 9, 120, inputs) == 0);
    inputs.manual_quiet = false;
    inputs.clock = ClockAt(2026, 8, 26, 11, 1, 121 * 60'000);
    CHECK(quiet.Update(121 * 60'000, inputs).started_sound != maomi::AutonomySound::kPlayLocalMeow);
    CHECK(quiet.GetSnapshot().next_meow_evaluation_ms >= 136 * 60'000);
}

void TestEligibleDaytimeEventuallyProducesOnlyLocalMeowCommand() {
    maomi::AutonomyController controller(505u);
    auto inputs = EligibleInputs(ClockAt(2026, 8, 26, 9, 0, 0));
    const uint32_t meows = SimulateMinutes(&controller, 9, 10 * 60, inputs);
    CHECK(meows > 0);
    CHECK(controller.GetSnapshot().proactive_meows_started == meows);
}

void TestMinimumSpacingAndDailyLimit() {
    maomi::AutonomyController controller(505u);
    auto inputs = EligibleInputs(ClockAt(2026, 8, 26, 8, 0, 0));
    uint32_t meows = 0;
    uint64_t previous_meow_ms = 0;
    for (int elapsed = 0; elapsed < 14 * 60; ++elapsed) {
        const uint64_t now_ms = static_cast<uint64_t>(elapsed) * 60'000;
        inputs.clock = ClockAt(2026, 8, 26, 8 + elapsed / 60, elapsed % 60, now_ms);
        const auto decision = controller.Update(now_ms, inputs);
        if (decision.started_sound != maomi::AutonomySound::kPlayLocalMeow) {
            continue;
        }
        if (meows != 0) {
            CHECK(now_ms - previous_meow_ms >= maomi::kProactiveMeowMinimumIntervalMs);
        }
        previous_meow_ms = now_ms;
        ++meows;
    }
    CHECK(meows == maomi::kProactiveMeowDailyLimit);
    CHECK(controller.GetSnapshot().proactive_meows_today == meows);
}

void TestNewDayResetsBudgetButRollbackCannotBypassIt() {
    maomi::AutonomyController controller(606u);
    auto inputs = EligibleInputs(ClockAt(2026, 8, 26, 8, 0, 0));
    CHECK(SimulateMinutes(&controller, 8, 14 * 60 - 1, inputs) == maomi::kProactiveMeowDailyLimit);

    uint32_t rollback_meows = 0;
    for (int elapsed = 0; elapsed < 14 * 60; ++elapsed) {
        const uint64_t now_ms = static_cast<uint64_t>(24 * 60 + elapsed) * 60'000;
        inputs.clock = ClockAt(2026, 8, 25, 8 + elapsed / 60, elapsed % 60, now_ms);
        const auto decision = controller.Update(now_ms, inputs);
        rollback_meows += decision.started_sound == maomi::AutonomySound::kPlayLocalMeow ? 1u : 0u;
    }
    CHECK(rollback_meows == 0);
    CHECK(controller.GetSnapshot().proactive_meows_today == maomi::kProactiveMeowDailyLimit);

    uint32_t next_day_meows = 0;
    for (int elapsed = 0; elapsed < 14 * 60; ++elapsed) {
        const uint64_t now_ms = static_cast<uint64_t>(48 * 60 + elapsed) * 60'000;
        inputs.clock = ClockAt(2026, 8, 27, 8 + elapsed / 60, elapsed % 60, now_ms);
        const auto decision = controller.Update(now_ms, inputs);
        next_day_meows += decision.started_sound == maomi::AutonomySound::kPlayLocalMeow ? 1u : 0u;
    }
    CHECK(next_day_meows == maomi::kProactiveMeowDailyLimit);
    CHECK(controller.GetSnapshot().proactive_meow_budget_date == 20260827);
}

void TestContinuousDayAcrossMidnightStaysQuietThenUsesNewBudget() {
    maomi::AutonomyController controller(808u);
    auto inputs = EligibleInputs(ClockAt(2026, 8, 26, 21, 0, 0));
    uint32_t first_day_meows = 0;
    uint32_t second_day_meows = 0;
    for (int elapsed = 0; elapsed < 25 * 60; ++elapsed) {
        const int total_minutes = 21 * 60 + elapsed;
        const int day = 26 + total_minutes / (24 * 60);
        const int minute_of_day = total_minutes % (24 * 60);
        const uint64_t now_ms = static_cast<uint64_t>(elapsed) * 60'000;
        inputs.clock = ClockAt(2026, 8, day, minute_of_day / 60, minute_of_day % 60, now_ms);
        if (controller.Update(now_ms, inputs).started_sound !=
            maomi::AutonomySound::kPlayLocalMeow) {
            continue;
        }
        if (day == 26) {
            ++first_day_meows;
        } else {
            ++second_day_meows;
        }
    }
    CHECK(first_day_meows == 0);
    CHECK(second_day_meows == maomi::kProactiveMeowDailyLimit);
}

void TestLocalMeowImmediatelyYieldsToWakeAndPriority() {
    auto run_interruption = [](bool use_activity) {
        maomi::AutonomyController controller(707u);
        auto inputs = EligibleInputs(ClockAt(2026, 8, 26, 9, 0, 0));
        uint64_t meow_ms = 0;
        for (int elapsed = 0; elapsed <= 10 * 60; ++elapsed) {
            const uint64_t now_ms = static_cast<uint64_t>(elapsed) * 60'000;
            inputs.clock = ClockAt(2026, 8, 26, 9 + elapsed / 60, elapsed % 60, now_ms);
            if (controller.Update(now_ms, inputs).started_sound ==
                maomi::AutonomySound::kPlayLocalMeow) {
                meow_ms = now_ms;
                break;
            }
        }
        CHECK(meow_ms != 0);
        inputs.activity =
            use_activity ? maomi::ActivitySource::kWakeWord : maomi::ActivitySource::kNone;
        inputs.higher_priority_active = !use_activity;
        const auto interrupted = controller.Update(meow_ms + 1, inputs);
        CHECK(interrupted.stopped_sound == maomi::AutonomySound::kPlayLocalMeow);
        CHECK(interrupted.started_sound == maomi::AutonomySound::kNone);
    };

    run_interruption(true);
    run_interruption(false);
}

}  // namespace

int main() {
    TestQuietHoursAndInvalidTimeNeverMeow();
    TestEveryEligibilityBlockerSuppressesMeow();
    TestInteractionUnderSixtyMinutesAndQuietDisableDoNotMeowImmediately();
    TestEligibleDaytimeEventuallyProducesOnlyLocalMeowCommand();
    TestMinimumSpacingAndDailyLimit();
    TestNewDayResetsBudgetButRollbackCannotBypassIt();
    TestContinuousDayAcrossMidnightStaysQuietThenUsesNewBudget();
    TestLocalMeowImmediatelyYieldsToWakeAndPriority();
    std::cout << "maomi proactive sound tests passed" << std::endl;
    return 0;
}
