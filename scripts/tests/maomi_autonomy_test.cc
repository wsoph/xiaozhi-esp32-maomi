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

maomi::AutonomyInputs IdleInputs() {
    maomi::AutonomyInputs inputs;
    inputs.official_idle = true;
    return inputs;
}

void CheckDecisionEqual(const maomi::AutonomyDecision& left, const maomi::AutonomyDecision& right) {
    CHECK(left.started_action == right.started_action);
    CHECK(left.stopped_action == right.stopped_action);
    CHECK(left.enter_low_brightness == right.enter_low_brightness);
    CHECK(left.restore_display == right.restore_display);
}

void CheckSnapshotEqual(const maomi::AutonomySnapshot& left, const maomi::AutonomySnapshot& right) {
    CHECK(left.active_action == right.active_action);
    CHECK(left.paused == right.paused);
    CHECK(left.drowsy == right.drowsy);
    CHECK(left.low_brightness == right.low_brightness);
    CHECK(left.last_activity_ms == right.last_activity_ms);
    CHECK(left.next_blink_ms == right.next_blink_ms);
    CHECK(left.next_look_ms == right.next_look_ms);
    CHECK(left.next_breath_ms == right.next_breath_ms);
    CHECK(left.actions_started == right.actions_started);
    CHECK(left.actions_cancelled == right.actions_cancelled);
    CHECK(left.random_draws == right.random_draws);
    CHECK(left.maximum_concurrent_actions == right.maximum_concurrent_actions);
}

void TestIntervalsAreBoundedAndOnlyOneActionRuns() {
    maomi::AutonomyController controller(0x12345678u);
    auto inputs = IdleInputs();
    controller.Update(1'000, inputs);

    auto snapshot = controller.GetSnapshot();
    CHECK(snapshot.next_blink_ms >= 1'000 + maomi::kBlinkMinimumIntervalMs);
    CHECK(snapshot.next_blink_ms <= 1'000 + maomi::kBlinkMaximumIntervalMs);
    CHECK(snapshot.next_look_ms >= 1'000 + maomi::kLookMinimumIntervalMs);
    CHECK(snapshot.next_look_ms <= 1'000 + maomi::kLookMaximumIntervalMs);

    const uint64_t first_due = snapshot.next_blink_ms < snapshot.next_look_ms
                                   ? snapshot.next_blink_ms
                                   : snapshot.next_look_ms;
    const auto started = controller.Update(first_due, inputs);
    CHECK(started.started_action == maomi::AutonomyAction::kBlink ||
          started.started_action == maomi::AutonomyAction::kLookAround);
    snapshot = controller.GetSnapshot();
    CHECK(snapshot.active_action == started.started_action);
    CHECK(snapshot.maximum_concurrent_actions == 1);

    const auto while_active = controller.Update(first_due + 1, inputs);
    CHECK(while_active.started_action == maomi::AutonomyAction::kNone);
    CHECK(controller.GetSnapshot().maximum_concurrent_actions == 1);
}

void TestSixtySecondsEntersDrowsyAndBreathes() {
    maomi::AutonomyController controller(7u);
    auto inputs = IdleInputs();
    controller.Update(10'000, inputs);

    const auto before = controller.Update(10'000 + maomi::kDrowsyAfterMs - 1, inputs);
    CHECK(!before.enter_low_brightness);
    CHECK(!controller.GetSnapshot().drowsy);

    const auto drowsy = controller.Update(10'000 + maomi::kDrowsyAfterMs, inputs);
    CHECK(drowsy.started_action == maomi::AutonomyAction::kBecomeSleepy);
    CHECK(drowsy.enter_low_brightness);
    auto snapshot = controller.GetSnapshot();
    CHECK(snapshot.drowsy);
    CHECK(snapshot.low_brightness);

    const auto first_breath =
        controller.Update(10'000 + maomi::kDrowsyAfterMs + maomi::kSleepyActionDurationMs, inputs);
    CHECK(first_breath.started_action == maomi::AutonomyAction::kSleepBreath);
    snapshot = controller.GetSnapshot();
    CHECK(snapshot.active_action == maomi::AutonomyAction::kSleepBreath);
    CHECK(controller.GetSnapshot().low_brightness);
}

void TestEveryActivityRestoresAndRestartsIdleTiming() {
    const maomi::ActivitySource sources[] = {
        maomi::ActivitySource::kWakeWord,
        maomi::ActivitySource::kButton,
        maomi::ActivitySource::kReminder,
        maomi::ActivitySource::kPetInteraction,
    };

    for (const auto source : sources) {
        maomi::AutonomyController controller(11u);
        auto inputs = IdleInputs();
        controller.Update(0, inputs);
        controller.Update(maomi::kDrowsyAfterMs, inputs);
        CHECK(controller.GetSnapshot().low_brightness);

        inputs.activity = source;
        const auto restored = controller.Update(maomi::kDrowsyAfterMs + 1, inputs);
        CHECK(restored.restore_display);
        CHECK(restored.stopped_action == maomi::AutonomyAction::kBecomeSleepy);
        const auto snapshot = controller.GetSnapshot();
        CHECK(!snapshot.drowsy);
        CHECK(!snapshot.low_brightness);
        CHECK(snapshot.last_activity_ms == maomi::kDrowsyAfterMs + 1);
        CHECK(snapshot.next_blink_ms >= snapshot.last_activity_ms + maomi::kBlinkMinimumIntervalMs);
        CHECK(snapshot.next_look_ms >= snapshot.last_activity_ms + maomi::kLookMinimumIntervalMs);
    }
}

void TestOfficialAndHigherPriorityStatePreemptImmediately() {
    maomi::AutonomyController controller(29u);
    auto inputs = IdleInputs();
    controller.Update(1'000, inputs);
    const uint64_t blink_at = controller.GetSnapshot().next_blink_ms;
    CHECK(controller.Update(blink_at, inputs).started_action == maomi::AutonomyAction::kBlink);

    inputs.higher_priority_active = true;
    const auto priority_stop = controller.Update(blink_at + 199, inputs);
    CHECK(priority_stop.stopped_action == maomi::AutonomyAction::kBlink);
    CHECK(controller.GetSnapshot().active_action == maomi::AutonomyAction::kNone);
    CHECK(controller.GetSnapshot().paused);

    inputs.higher_priority_active = false;
    controller.Update(blink_at + 200, inputs);
    auto snapshot = controller.GetSnapshot();
    const uint64_t look_at = snapshot.next_look_ms;
    controller.Update(look_at, inputs);
    snapshot = controller.GetSnapshot();
    CHECK(snapshot.active_action == maomi::AutonomyAction::kBlink ||
          snapshot.active_action == maomi::AutonomyAction::kLookAround);

    inputs.official_idle = false;
    const auto official_stop = controller.Update(look_at + 1, inputs);
    CHECK(official_stop.stopped_action != maomi::AutonomyAction::kNone);
    CHECK(controller.GetSnapshot().paused);
    CHECK(controller.GetSnapshot().active_action == maomi::AutonomyAction::kNone);
}

void TestMonotonicRollbackRecoversSafely() {
    maomi::AutonomyController controller(37u);
    auto inputs = IdleInputs();
    controller.Update(90'000, inputs);
    controller.Update(95'000, inputs);
    const auto after_rollback = controller.Update(1'000, inputs);
    CHECK(after_rollback.started_action == maomi::AutonomyAction::kNone);
    const auto snapshot = controller.GetSnapshot();
    CHECK(snapshot.last_activity_ms == 1'000);
    CHECK(snapshot.next_blink_ms >= 1'000 + maomi::kBlinkMinimumIntervalMs);
    CHECK(snapshot.next_look_ms >= 1'000 + maomi::kLookMinimumIntervalMs);
}

void TestActionsRemainVisibleForCompleteAnimationCyclesAndSleepIsContinuous() {
    static_assert(maomi::kBlinkActionDurationMs >= 2 * 3 * 140);
    static_assert(maomi::kLookActionDurationMs >= 2 * 4 * 420);
    static_assert(maomi::kBreathActionDurationMs >= 2 * 4 * 700);

    maomi::AutonomyController controller(43u);
    auto inputs = IdleInputs();
    controller.Update(0, inputs);

    const auto sleepy = controller.Update(maomi::kDrowsyAfterMs, inputs);
    CHECK(sleepy.started_action == maomi::AutonomyAction::kBecomeSleepy);

    const auto breathing =
        controller.Update(maomi::kDrowsyAfterMs + maomi::kSleepyActionDurationMs, inputs);
    CHECK(breathing.started_action == maomi::AutonomyAction::kSleepBreath);
    CHECK(controller.GetSnapshot().active_action == maomi::AutonomyAction::kSleepBreath);

    const auto next_cycle = controller.Update(
        maomi::kDrowsyAfterMs + maomi::kSleepyActionDurationMs + maomi::kBreathActionDurationMs,
        inputs);
    CHECK(next_cycle.started_action == maomi::AutonomyAction::kSleepBreath);
    CHECK(controller.GetSnapshot().active_action == maomi::AutonomyAction::kSleepBreath);
}

void TestFixedSeedTenThousandStepSimulationIsRepeatableAndBounded() {
    maomi::AutonomyController first(0x5A17u);
    maomi::AutonomyController second(0x5A17u);
    auto first_inputs = IdleInputs();
    auto second_inputs = IdleInputs();
    uint32_t blink_count = 0;
    uint32_t look_count = 0;
    uint32_t sleepy_count = 0;
    uint32_t breath_count = 0;

    for (uint32_t step = 0; step < 10'000; ++step) {
        const uint64_t now_ms = static_cast<uint64_t>(step) * 100;
        first_inputs.activity = maomi::ActivitySource::kNone;
        second_inputs.activity = maomi::ActivitySource::kNone;
        first_inputs.official_idle = second_inputs.official_idle = (step % 997) >= 2;
        first_inputs.higher_priority_active = second_inputs.higher_priority_active =
            (step % 2111) == 0;
        if (step != 0 && step % 1300 == 0) {
            first_inputs.activity = second_inputs.activity = maomi::ActivitySource::kPetInteraction;
        }

        const auto first_decision = first.Update(now_ms, first_inputs);
        const auto second_decision = second.Update(now_ms, second_inputs);
        CheckDecisionEqual(first_decision, second_decision);
        CheckSnapshotEqual(first.GetSnapshot(), second.GetSnapshot());

        switch (first_decision.started_action) {
            case maomi::AutonomyAction::kBlink:
                ++blink_count;
                break;
            case maomi::AutonomyAction::kLookAround:
                ++look_count;
                break;
            case maomi::AutonomyAction::kBecomeSleepy:
                ++sleepy_count;
                break;
            case maomi::AutonomyAction::kSleepBreath:
                ++breath_count;
                break;
            case maomi::AutonomyAction::kNone:
                break;
        }

        const auto snapshot = first.GetSnapshot();
        CHECK(snapshot.maximum_concurrent_actions <= 1);
        CHECK(snapshot.active_action >= maomi::AutonomyAction::kNone);
        CHECK(snapshot.active_action <= maomi::AutonomyAction::kSleepBreath);
        if (!snapshot.drowsy && !snapshot.paused) {
            CHECK(snapshot.next_blink_ms >=
                  snapshot.last_activity_ms + maomi::kBlinkMinimumIntervalMs);
            CHECK(snapshot.next_look_ms >=
                  snapshot.last_activity_ms + maomi::kLookMinimumIntervalMs);
        }
    }

    CHECK(blink_count > 0);
    CHECK(look_count > 0);
    CHECK(sleepy_count > 0);
    CHECK(breath_count > 0);
    CHECK(first.GetSnapshot().actions_started < 10'000);
}

}  // namespace

int main() {
    TestIntervalsAreBoundedAndOnlyOneActionRuns();
    TestSixtySecondsEntersDrowsyAndBreathes();
    TestEveryActivityRestoresAndRestartsIdleTiming();
    TestOfficialAndHigherPriorityStatePreemptImmediately();
    TestMonotonicRollbackRecoversSafely();
    TestActionsRemainVisibleForCompleteAnimationCyclesAndSleepIsContinuous();
    TestFixedSeedTenThousandStepSimulationIsRepeatableAndBounded();
    std::cout << "maomi autonomy tests passed" << std::endl;
    return 0;
}
