#include "maomi_pet_core.h"

#include <atomic>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {

using maomi::Event;
using maomi::LogEvent;
using maomi::PetPriority;
using maomi::PetState;
using maomi::SubmitResult;

class FakeScheduler {
public:
    void Schedule(std::function<void()>&& callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(std::move(callback));
    }

    size_t Pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return callbacks_.size();
    }

    void Drain() {
        while (true) {
            std::function<void()> callback;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (callbacks_.empty()) {
                    return;
                }
                callback = std::move(callbacks_.front());
                callbacks_.pop_front();
            }
            callback();
        }
    }

private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> callbacks_;
};

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

maomi::PetCore MakeCore(FakeScheduler& scheduler, std::vector<LogEvent>* logs = nullptr) {
    return maomi::PetCore(
        [&scheduler](std::function<void()>&& callback) { scheduler.Schedule(std::move(callback)); },
        [logs](LogEvent event, const maomi::Snapshot&) {
            if (logs != nullptr) {
                logs->push_back(event);
            }
        },
        kDeviceStateIdle);
}

void TestStateAndPriorityTable() {
    struct Case {
        PetState state;
        PetPriority priority;
    };
    const Case cases[] = {
        {PetState::kCurious, PetPriority::kAutonomous},
        {PetState::kBlinking, PetPriority::kAutonomous},
        {PetState::kSleepy, PetPriority::kAutonomous},
        {PetState::kSleeping, PetPriority::kAutonomous},
        {PetState::kHappy, PetPriority::kInteraction},
        {PetState::kBeingPetted, PetPriority::kInteraction},
        {PetState::kEating, PetPriority::kInteraction},
        {PetState::kPlaying, PetPriority::kInteraction},
        {PetState::kCharging, PetPriority::kPower},
        {PetState::kFull, PetPriority::kPower},
        {PetState::kLowBattery, PetPriority::kPower},
        {PetState::kReminding, PetPriority::kReminder},
    };

    for (const auto& item : cases) {
        FakeScheduler scheduler;
        auto core = MakeCore(scheduler);
        CHECK(core.RequestExpression(item.state, item.priority, true) == SubmitResult::kQueued);
        CHECK(core.GetSnapshot().state == PetState::kIdle);
        scheduler.Drain();
        auto snapshot = core.GetSnapshot();
        CHECK(snapshot.state == item.state);
        CHECK(snapshot.priority == item.priority);
        CHECK(core.ReleaseExpression(item.priority) == SubmitResult::kQueued);
        scheduler.Drain();
        CHECK(core.GetSnapshot().state == PetState::kIdle);
    }
}

void TestInvalidRequestsAndPriorityPreemption() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);

    CHECK(core.RequestExpression(PetState::kIdle, PetPriority::kAutonomous, true) ==
          SubmitResult::kRejected);
    CHECK(core.RequestExpression(static_cast<PetState>(99), PetPriority::kAutonomous, true) ==
          SubmitResult::kRejected);
    CHECK(core.RequestExpression(PetState::kHappy, static_cast<PetPriority>(99), true) ==
          SubmitResult::kRejected);
    CHECK(core.RequestExpression(PetState::kReminding, PetPriority::kAutonomous, true) ==
          SubmitResult::kRejected);
    CHECK(core.RequestExpression(PetState::kCharging, PetPriority::kInteraction, true) ==
          SubmitResult::kRejected);
    CHECK(core.RequestExpression(PetState::kSleeping, PetPriority::kReminder, true) ==
          SubmitResult::kRejected);
    CHECK(core.ReleaseExpression(PetPriority::kCritical) == SubmitResult::kRejected);
    CHECK(core.ReleaseExpression(PetPriority::kOfficial) == SubmitResult::kRejected);
    CHECK(core.ReleaseExpression(PetPriority::kIdle) == SubmitResult::kRejected);
    CHECK(core.ReleaseExpression(static_cast<PetPriority>(99)) == SubmitResult::kRejected);
    CHECK(core.Submit(Event::Interaction(PetState::kIdle)) == SubmitResult::kRejected);
    CHECK(core.Submit(Event::Interaction(PetState::kSleeping)) == SubmitResult::kRejected);
    CHECK(core.Submit(Event::BatteryChanged(-1)) == SubmitResult::kRejected);
    CHECK(core.Submit(Event::BatteryChanged(101)) == SubmitResult::kRejected);
    CHECK(core.Submit(Event::OfficialStateChanged(static_cast<DeviceState>(-1))) ==
          SubmitResult::kRejected);
    CHECK(core.Submit(Event::OfficialStateChanged(static_cast<DeviceState>(99))) ==
          SubmitResult::kRejected);
    auto invalid_event = Event::Tick();
    invalid_event.type = static_cast<maomi::EventType>(99);
    CHECK(core.Submit(invalid_event) == SubmitResult::kRejected);
    CHECK(scheduler.Pending() == 0);

    CHECK(core.RequestExpression(PetState::kSleeping, PetPriority::kAutonomous, true) ==
          SubmitResult::kQueued);
    CHECK(core.RequestExpression(PetState::kEating, PetPriority::kInteraction, true) ==
          SubmitResult::kQueued);
    CHECK(core.RequestExpression(PetState::kReminding, PetPriority::kReminder, true) ==
          SubmitResult::kQueued);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kReminding);

    core.ReleaseExpression(PetPriority::kReminder);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kEating);
    core.ReleaseExpression(PetPriority::kInteraction);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kSleeping);
}

void TestActualEventMappingsAndPowerOrdering() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);

    CHECK(core.Submit(Event::UserWake()) == SubmitResult::kQueued);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kCurious);
    CHECK(core.GetSnapshot().priority == PetPriority::kInteraction);

    CHECK(core.Submit(Event::ConversationFinished()) == SubmitResult::kQueued);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kHappy);

    const PetState interactions[] = {PetState::kBeingPetted, PetState::kHappy, PetState::kEating,
                                     PetState::kPlaying};
    for (const auto state : interactions) {
        CHECK(core.Submit(Event::Interaction(state)) == SubmitResult::kQueued);
        scheduler.Drain();
        CHECK(core.GetSnapshot().state == state);
        CHECK(core.GetSnapshot().priority == PetPriority::kInteraction);
    }

    CHECK(core.Submit(Event::ChargingChanged(true)) == SubmitResult::kQueued);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kCharging);
    CHECK(core.Submit(Event::BatteryChanged(100)) == SubmitResult::kQueued);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kFull);
    CHECK(core.Submit(Event::BatteryChanged(10)) == SubmitResult::kQueued);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kLowBattery);

    FakeScheduler ordering_scheduler;
    auto ordering_core = MakeCore(ordering_scheduler);
    CHECK(ordering_core.Submit(Event::BatteryChanged(100)) == SubmitResult::kQueued);
    CHECK(ordering_core.Submit(Event::ChargingChanged(true)) == SubmitResult::kQueued);
    CHECK(ordering_core.Submit(Event::BatteryChanged(10)) == SubmitResult::kCoalesced);
    ordering_scheduler.Drain();
    CHECK(ordering_core.GetSnapshot().battery_level == 10);
    CHECK(ordering_core.GetSnapshot().state == PetState::kLowBattery);

    FakeScheduler reverse_scheduler;
    auto reverse_core = MakeCore(reverse_scheduler);
    reverse_core.Submit(Event::BatteryChanged(10));
    reverse_core.Submit(Event::ChargingChanged(true));
    reverse_scheduler.Drain();
    CHECK(reverse_core.GetSnapshot().state == PetState::kLowBattery);

    CHECK(core.Submit(Event::TimeValidityChanged(true)) == SubmitResult::kQueued);
    scheduler.Drain();
    CHECK(core.GetSnapshot().time_valid);
    CHECK(core.Submit(Event::TimeValidityChanged(false)) == SubmitResult::kQueued);
    scheduler.Drain();
    CHECK(!core.GetSnapshot().time_valid);
}

void TestCompletePriorityRestoreChain() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);

    core.RequestExpression(PetState::kSleeping, PetPriority::kAutonomous, true);
    core.Submit(Event::Interaction(PetState::kPlaying));
    core.Submit(Event::BatteryChanged(10));
    core.Submit(Event::ReminderDue());
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kReminding);

    core.ReleaseExpression(PetPriority::kReminder);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kLowBattery);
    core.Submit(Event::BatteryChanged(50));
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kPlaying);
    core.ReleaseExpression(PetPriority::kInteraction);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kSleeping);
    core.ReleaseExpression(PetPriority::kAutonomous);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kIdle);
}

void TestEveryOfficialNonIdleStatePreempts() {
    const DeviceState official_states[] = {
        kDeviceStateUnknown,    kDeviceStateStarting,   kDeviceStateWifiConfiguring,
        kDeviceStateConnecting, kDeviceStateSpeaking,   kDeviceStateNotifying,
        kDeviceStateUpgrading,  kDeviceStateActivating, kDeviceStateAudioTesting,
        kDeviceStateFatalError,
    };

    for (const auto official_state : official_states) {
        FakeScheduler scheduler;
        auto core = MakeCore(scheduler);
        core.RequestExpression(PetState::kSleeping, PetPriority::kAutonomous, true);
        scheduler.Drain();
        CHECK(core.GetSnapshot().state == PetState::kSleeping);

        core.Submit(Event::OfficialStateChanged(official_state));
        scheduler.Drain();
        auto busy = core.GetSnapshot();
        CHECK(busy.paused_by_official_state);
        CHECK(busy.state == PetState::kIdle);
        CHECK(busy.official_state == official_state);

        core.Submit(Event::OfficialStateChanged(kDeviceStateIdle));
        scheduler.Drain();
        auto idle = core.GetSnapshot();
        CHECK(!idle.paused_by_official_state);
        CHECK(idle.state == PetState::kSleeping);
    }

    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);
    core.RequestExpression(PetState::kCurious, PetPriority::kAutonomous, false);
    scheduler.Drain();
    core.Submit(Event::OfficialStateChanged(kDeviceStateListening));
    core.Submit(Event::OfficialStateChanged(kDeviceStateIdle));
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kIdle);

    FakeScheduler sequence_scheduler;
    auto sequence_core = MakeCore(sequence_scheduler);
    sequence_core.Submit(Event::OfficialStateChanged(kDeviceStateListening));
    sequence_core.Submit(Event::UserWake());
    sequence_core.Submit(Event::OfficialStateChanged(kDeviceStateIdle));
    sequence_scheduler.Drain();
    CHECK(!sequence_core.GetSnapshot().paused_by_official_state);
    CHECK(sequence_core.GetSnapshot().wake_signals == 1);
    CHECK(sequence_core.GetSnapshot().state == PetState::kIdle);

    FakeScheduler untrusted_flag_scheduler;
    auto untrusted_flag_core = MakeCore(untrusted_flag_scheduler);
    auto forged_busy_event = Event::OfficialStateChanged(kDeviceStateSpeaking);
    forged_busy_event.flag = false;
    untrusted_flag_core.Submit(forged_busy_event);
    untrusted_flag_scheduler.Drain();
    CHECK(untrusted_flag_core.GetSnapshot().paused_by_official_state);
}

void TestListeningKeepsResumableInteractionVisible() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);

    core.Submit(Event::OfficialStateChanged(kDeviceStateListening));
    core.Submit(Event::Interaction(PetState::kBeingPetted));
    scheduler.Drain();

    const auto listening = core.GetSnapshot();
    CHECK(!listening.paused_by_official_state);
    CHECK(listening.official_state == kDeviceStateListening);
    CHECK(listening.state == PetState::kBeingPetted);
    CHECK(listening.priority == PetPriority::kInteraction);

    core.Submit(Event::OfficialStateChanged(kDeviceStateSpeaking));
    scheduler.Drain();
    const auto speaking = core.GetSnapshot();
    CHECK(speaking.paused_by_official_state);
    CHECK(speaking.state == PetState::kIdle);
}

void TestObserverRunsOnlyOnScheduledDrain() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);
    const auto main_thread = std::this_thread::get_id();
    std::atomic<int> calls{0};
    std::atomic<bool> wrong_thread{false};
    const int observer_id = core.AddObserver([&](const maomi::Snapshot&) {
        calls.fetch_add(1);
        if (std::this_thread::get_id() != main_thread) {
            wrong_thread.store(true);
        }
    });
    CHECK(observer_id >= 0);

    std::thread worker([&]() {
        CHECK(core.Submit(Event::UserWake()) == SubmitResult::kQueued);
        CHECK(calls.load() == 0);
    });
    worker.join();
    CHECK(calls.load() == 0);
    scheduler.Drain();
    CHECK(calls.load() == 1);
    CHECK(!wrong_thread.load());
    CHECK(core.RemoveObserver(observer_id));

    core.Submit(Event::ConversationFinished());
    scheduler.Drain();
    CHECK(calls.load() == 1);
}

void TestObserverCapacityIsBoundedAndReusable() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);
    int observer_ids[maomi::kObserverCapacity];
    for (size_t i = 0; i < maomi::kObserverCapacity; ++i) {
        observer_ids[i] = core.AddObserver([](const maomi::Snapshot&) {});
        CHECK(observer_ids[i] >= 0);
    }
    CHECK(core.AddObserver([](const maomi::Snapshot&) {}) == -1);
    CHECK(core.AddObserver({}) == -1);
    CHECK(!core.RemoveObserver(-1));
    CHECK(core.RemoveObserver(observer_ids[3]));
    CHECK(core.AddObserver([](const maomi::Snapshot&) {}) >= 0);
}

void TestOneThousandCoalescedEventsFromWorker() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);
    std::atomic<bool> failed{false};

    std::thread worker([&]() {
        for (uint32_t i = 0; i < 1000; ++i) {
            const auto expected = i == 0 ? SubmitResult::kQueued : SubmitResult::kCoalesced;
            if (core.Submit(Event::Tick(i)) != expected) {
                failed.store(true);
            }
        }
    });
    worker.join();

    CHECK(!failed.load());
    CHECK(scheduler.Pending() == 1);
    auto queued = core.GetSnapshot();
    CHECK(queued.queue_depth == 1);
    CHECK(queued.submitted == 1000);
    CHECK(queued.coalesced == 999);
    CHECK(queued.rejected == 0);

    scheduler.Drain();
    auto drained = core.GetSnapshot();
    CHECK(drained.queue_depth == 0);
    CHECK(drained.processed == 1);
    CHECK(drained.last_tick == 999);
}

void TestFullQueueRejectsOrdinaryButKeepsImportantEvents() {
    FakeScheduler scheduler;
    std::vector<LogEvent> logs;
    auto core = MakeCore(scheduler, &logs);

    for (size_t i = 0; i < maomi::kEventQueueCapacity; ++i) {
        CHECK(core.Submit(Event::ConversationFinished()) == SubmitResult::kQueued);
    }
    CHECK(core.GetSnapshot().queue_depth == maomi::kEventQueueCapacity);
    CHECK(core.Submit(Event::Tick(1)) == SubmitResult::kRejected);
    CHECK(core.Submit(Event::ReminderDue()) == SubmitResult::kQueued);
    CHECK(core.GetSnapshot().queue_depth == maomi::kEventQueueCapacity);
    CHECK(core.GetSnapshot().evicted == 1);

    scheduler.Drain();
    auto snapshot = core.GetSnapshot();
    CHECK(snapshot.state == PetState::kReminding);
    CHECK(snapshot.queue_depth == 0);
    CHECK(logs.size() == 1);
    CHECK(logs[0] == LogEvent::kQueuePressure);

    FakeScheduler official_scheduler;
    auto official_core = MakeCore(official_scheduler);
    official_core.RequestExpression(PetState::kSleeping, PetPriority::kAutonomous, false);
    official_scheduler.Drain();
    for (size_t i = 0; i < maomi::kEventQueueCapacity; ++i) {
        CHECK(official_core.Submit(Event::ConversationFinished()) == SubmitResult::kQueued);
    }
    CHECK(official_core.Submit(Event::OfficialStateChanged(kDeviceStateSpeaking)) ==
          SubmitResult::kQueued);
    official_scheduler.Drain();
    CHECK(official_core.GetSnapshot().paused_by_official_state);
    CHECK(official_core.GetSnapshot().state == PetState::kIdle);

    FakeScheduler wake_scheduler;
    auto wake_core = MakeCore(wake_scheduler);
    for (size_t i = 0; i < maomi::kEventQueueCapacity; ++i) {
        CHECK(wake_core.Submit(Event::ConversationFinished()) == SubmitResult::kQueued);
    }
    CHECK(wake_core.Submit(Event::UserWake()) == SubmitResult::kQueued);
    CHECK(wake_core.GetSnapshot().evicted == 1);
    wake_scheduler.Drain();
    CHECK(wake_core.GetSnapshot().wake_signals == 1);
}

void TestImportantSignalsCoalesceWithoutLoss() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);
    CHECK(core.Submit(Event::ReminderDue()) == SubmitResult::kQueued);
    for (int i = 0; i < 99; ++i) {
        CHECK(core.Submit(Event::ReminderDue()) == SubmitResult::kCoalesced);
    }
    CHECK(core.GetSnapshot().queue_depth == 1);
    scheduler.Drain();
    CHECK(core.GetSnapshot().state == PetState::kReminding);
    CHECK(core.GetSnapshot().processed == 1);
    CHECK(core.GetSnapshot().coalesced == 99);
    CHECK(core.GetSnapshot().reminder_signals == 100);

    FakeScheduler saturation_scheduler;
    auto saturation_core = MakeCore(saturation_scheduler);
    auto many_reminders = Event::ReminderDue();
    many_reminders.occurrences = std::numeric_limits<uint32_t>::max();
    CHECK(saturation_core.Submit(many_reminders) == SubmitResult::kQueued);
    CHECK(saturation_core.Submit(Event::ReminderDue()) == SubmitResult::kCoalesced);
    saturation_scheduler.Drain();
    CHECK(saturation_core.GetSnapshot().reminder_signals == std::numeric_limits<uint32_t>::max());
}

void TestLatestOfficialStateSurvivesAnImportantOnlyFullQueue() {
    FakeScheduler scheduler;
    auto core = MakeCore(scheduler);

    for (int i = 0; i < 14; ++i) {
        CHECK(core.Submit(Event::OfficialStateChanged(kDeviceStateListening)) ==
              SubmitResult::kQueued);
        const auto signal_result =
            i % 2 == 0 ? core.Submit(Event::UserWake()) : core.Submit(Event::ReminderDue());
        CHECK(signal_result == (i < 2 ? SubmitResult::kQueued : SubmitResult::kCoalesced));
    }
    CHECK(core.GetSnapshot().queue_depth == maomi::kEventQueueCapacity);
    CHECK(core.Submit(Event::OfficialStateChanged(kDeviceStateIdle)) == SubmitResult::kQueued);
    CHECK(core.GetSnapshot().evicted == 1);

    scheduler.Drain();
    const auto snapshot = core.GetSnapshot();
    CHECK(snapshot.official_state == kDeviceStateIdle);
    CHECK(!snapshot.paused_by_official_state);
    CHECK(snapshot.wake_signals == 7);
    CHECK(snapshot.reminder_signals == 7);
}

void TestMissingSchedulerRejectsWithoutQueuedWork() {
    maomi::PetCore core({}, {}, kDeviceStateIdle);
    CHECK(core.Submit(Event::UserWake()) == SubmitResult::kRejected);
    const auto snapshot = core.GetSnapshot();
    CHECK(snapshot.submitted == 1);
    CHECK(snapshot.rejected == 1);
    CHECK(snapshot.queue_depth == 0);
    CHECK(snapshot.wake_signals == 0);
}

}  // namespace

int main() {
    TestStateAndPriorityTable();
    TestInvalidRequestsAndPriorityPreemption();
    TestActualEventMappingsAndPowerOrdering();
    TestCompletePriorityRestoreChain();
    TestEveryOfficialNonIdleStatePreempts();
    TestListeningKeepsResumableInteractionVisible();
    TestObserverRunsOnlyOnScheduledDrain();
    TestObserverCapacityIsBoundedAndReusable();
    TestOneThousandCoalescedEventsFromWorker();
    TestFullQueueRejectsOrdinaryButKeepsImportantEvents();
    TestImportantSignalsCoalesceWithoutLoss();
    TestLatestOfficialStateSurvivesAnImportantOnlyFullQueue();
    TestMissingSchedulerRejectsWithoutQueuedWork();
    std::cout << "maomi_pet_core tests passed" << std::endl;
    return 0;
}
