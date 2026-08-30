#pragma once

#include "device_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

namespace maomi {

constexpr size_t kEventQueueCapacity = 16;
constexpr size_t kObserverCapacity = 8;

enum class PetState : uint8_t {
    kIdle,
    kCurious,
    kBlinking,
    kSleepy,
    kSleeping,
    kHappy,
    kBeingPetted,
    kEating,
    kPlaying,
    kCharging,
    kFull,
    kLowBattery,
    kReminding,
};

// Matches the approved global display/audio priority table. Smaller values win.
enum class PetPriority : uint8_t {
    kCritical = 1,
    kOfficial = 2,
    kReminder = 3,
    kPower = 4,
    kInteraction = 5,
    kAutonomous = 6,
    kIdle = 7,
};

enum class EventType : uint8_t {
    kUserWake,
    kConversationFinished,
    kInteraction,
    kChargingChanged,
    kBatteryChanged,
    kTimeValidityChanged,
    kTick,
    kReminderDue,
    kOfficialStateChanged,
    kRequestExpression,
    kReleaseExpression,
};

enum class SubmitResult : uint8_t {
    kQueued,
    kCoalesced,
    kRejected,
};

enum class LogEvent : uint8_t {
    kQueuePressure,
};

struct Event {
    EventType type = EventType::kTick;
    int32_t value = 0;
    PetState state = PetState::kIdle;
    PetPriority priority = PetPriority::kIdle;
    bool flag = false;
    uint32_t occurrences = 1;

    static Event UserWake();
    static Event ConversationFinished();
    static Event Interaction(PetState state);
    static Event ChargingChanged(bool charging);
    static Event BatteryChanged(int level);
    static Event TimeValidityChanged(bool valid);
    static Event Tick(uint32_t tick = 0);
    static Event ReminderDue();
    static Event OfficialStateChanged(DeviceState state);
    static Event RequestExpression(PetState state, PetPriority priority,
                                   bool resume_after_official);
    static Event ReleaseExpression(PetPriority priority);
};

struct Snapshot {
    PetState state = PetState::kIdle;
    PetPriority priority = PetPriority::kIdle;
    DeviceState official_state = kDeviceStateUnknown;
    bool paused_by_official_state = true;
    bool charging = false;
    bool time_valid = false;
    int battery_level = -1;
    uint32_t last_tick = 0;
    size_t queue_depth = 0;
    uint32_t submitted = 0;
    uint32_t processed = 0;
    uint32_t coalesced = 0;
    uint32_t rejected = 0;
    uint32_t evicted = 0;
    uint32_t wake_signals = 0;
    uint32_t reminder_signals = 0;
};

// Continuous listening is a transparent official state for explicit pet interactions and
// reminders. All other non-idle states keep ownership of the screen and local presentation.
bool AllowsMaomiLocalPresentation(DeviceState state);

/*
 * Deterministic state/event table:
 * - wake -> curious; conversation finished -> happy;
 * - interaction -> being_petted/happy/eating/playing;
 * - charging/battery -> charging/full/low_battery;
 * - reminder -> reminding; tick/time-valid only update metadata;
 * - speaking and other official busy states pause pet output; continuous listening keeps
 *   resumable local expressions available; official idle restores resumable slots.
 *
 * Priority slots are reminder > power > interaction > autonomous. Releasing a
 * slot reveals the next active lower-priority slot. Invalid enum values, idle as
 * a requested expression, and invalid interaction targets are rejected.
 */
class PetCore {
public:
    // The scheduler must defer callbacks to the application main task and must not throw.
    // PetCore must outlive every scheduled callback (the board owns it for firmware lifetime).
    using MainTaskScheduler = std::function<void(std::function<void()>&&)>;
    using Observer = std::function<void(const Snapshot&)>;
    using Logger = std::function<void(LogEvent, const Snapshot&)>;

    explicit PetCore(MainTaskScheduler scheduler, Logger logger = {},
                     DeviceState initial_official_state = kDeviceStateUnknown);
    PetCore(const PetCore&) = delete;
    PetCore& operator=(const PetCore&) = delete;

    SubmitResult Submit(const Event& event);
    SubmitResult RequestExpression(PetState state, PetPriority priority,
                                   bool resume_after_official);
    SubmitResult ReleaseExpression(PetPriority priority);

    int AddObserver(Observer observer);
    bool RemoveObserver(int observer_id);
    Snapshot GetSnapshot() const;

private:
    struct ExpressionSlot {
        bool active = false;
        bool resume_after_official = false;
        PetState state = PetState::kIdle;
    };

    struct ObserverSlot {
        int id = -1;
        Observer callback;
    };

    MainTaskScheduler scheduler_;
    Logger logger_;
    mutable std::mutex mutex_;
    std::array<Event, kEventQueueCapacity> queue_{};
    size_t queue_size_ = 0;
    bool drain_scheduled_ = false;
    bool queue_warning_pending_ = false;
    bool has_logged_queue_pressure_ = false;
    uint32_t last_logged_pressure_count_ = 0;
    std::array<ExpressionSlot, 4> expressions_{};
    std::array<ObserverSlot, kObserverCapacity> observers_{};
    int next_observer_id_ = 0;
    Snapshot snapshot_;

    static bool IsValidState(PetState state);
    static bool IsExpressionPriority(PetPriority priority);
    static bool IsValidExpression(PetState state, PetPriority priority);
    static bool IsImportant(EventType type);
    static bool IsCoalescible(EventType type);
    static bool IsValidEvent(const Event& event);
    static size_t PriorityIndex(PetPriority priority);

    void RemoveQueuedEvent(size_t index);
    void DrainOnMainTask();
    void ProcessOnMainTask(const Event& event);
    void SetExpression(PetPriority priority, PetState state, bool resume_after_official);
    void ClearExpression(PetPriority priority);
    void RecomputeVisibleState();
    bool VisibleSnapshotChanged(const Snapshot& before) const;
};

}  // namespace maomi
