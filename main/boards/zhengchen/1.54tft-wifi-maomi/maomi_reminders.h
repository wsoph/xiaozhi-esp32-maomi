#pragma once

#include "maomi_clock.h"
#include "maomi_storage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace maomi {

constexpr size_t kMaxPersistentReminders = 8;
constexpr size_t kMaxTemporaryReminders = 4;
constexpr size_t kMaxActiveReminders = kMaxPersistentReminders + kMaxTemporaryReminders;
constexpr size_t kMaxReminderLabelBytes = 32;
constexpr uint64_t kMaximumBusyDeferralMs = 5 * 60 * 1000;
constexpr int64_t kMaximumRestartAlarmLatenessSeconds = 10 * 60;

enum class ReminderKind : uint8_t {
    kCountdown,
    kAlarm,
    kWater,
    kSedentary,
    kPomodoro,
};

enum class ReminderPhase : uint8_t {
    kNone,
    kWork,
    kBreak,
};

enum class ReminderStatus : uint8_t {
    kAccepted,
    kCancelled,
    kInvalidArgument,
    kCapacityReached,
    kPomodoroActive,
    kNotFound,
    kPersistenceUnavailable,
};

enum class RestoreStatus : uint8_t {
    kEmpty,
    kRestored,
    kRecovered,
    kUnavailable,
};

enum class ReminderEventState : uint8_t {
    kNone,
    kTriggered,
    kMissed,
};

struct ReminderSnapshot {
    uint16_t id = 0;
    ReminderKind kind = ReminderKind::kCountdown;
    ReminderPhase phase = ReminderPhase::kNone;
    bool persistent = false;
    uint64_t remaining_ms = 0;
    int64_t next_wall_time_seconds = 0;
    uint32_t interval_seconds = 0;
    uint8_t completed_cycles = 0;
    uint8_t total_cycles = 0;
    std::array<char, kMaxReminderLabelBytes + 1> label{};
};

struct ReminderResult {
    ReminderStatus status = ReminderStatus::kInvalidArgument;
    uint16_t id = 0;
    ReminderKind kind = ReminderKind::kCountdown;
    bool persistent = false;
    SaveResult save_result = SaveResult::kNoChanges;
};

struct RestoreResult {
    RestoreStatus status = RestoreStatus::kEmpty;
    SaveResult save_result = SaveResult::kNoChanges;
    size_t restored_count = 0;
};

struct ReminderList {
    std::array<ReminderSnapshot, kMaxActiveReminders> items{};
    size_t count = 0;
};

struct ReminderTick {
    ClockSnapshot clock;
    bool device_busy = false;
    bool low_battery = false;
};

struct ReminderEvent {
    ReminderEventState state = ReminderEventState::kNone;
    uint16_t id = 0;
    ReminderKind kind = ReminderKind::kCountdown;
    ReminderPhase phase = ReminderPhase::kNone;
    bool audible = false;
    bool persistence_pending = false;
};

// Main-task-owned, fixed-capacity reminder scheduler. External callbacks must schedule all
// mutations onto the application main task before calling this class.
class ReminderEngine {
public:
    explicit ReminderEngine(StateStorage& storage);
    ReminderEngine(const ReminderEngine&) = delete;
    ReminderEngine& operator=(const ReminderEngine&) = delete;

    RestoreResult Restore(const ClockSnapshot& clock);
    ReminderResult StartCountdown(uint32_t duration_seconds, std::string_view label,
                                  const ClockSnapshot& clock);
    ReminderResult SetAlarm(const DateTime& target, std::string_view label,
                            const ClockSnapshot& clock);
    ReminderResult StartInterval(ReminderKind kind, uint32_t interval_minutes,
                                 std::string_view label, const ClockSnapshot& clock);
    ReminderResult StartPomodoro(uint32_t work_minutes, uint32_t break_minutes, uint32_t cycles,
                                 const ClockSnapshot& clock);
    ReminderResult Cancel(uint16_t id, const ClockSnapshot& clock);

    ReminderEvent Update(const ReminderTick& tick);
    ReminderList List(const ClockSnapshot& clock) const;
    const ReminderSnapshot* Find(uint16_t id) const;
    size_t ActiveCount() const;
    size_t PersistentCount() const;

    static int64_t CivilSeconds(const DateTime& value);
    static DateTime DateTimeFromCivilSeconds(int64_t value);

private:
    struct Entry {
        ReminderSnapshot snapshot;
        bool active = false;
        bool due_pending = false;
        bool persistence_retry_pending = false;
        bool persistence_retry_missed = false;
        uint64_t deadline_ms = 0;
        uint64_t pending_since_ms = 0;
        uint32_t work_seconds = 0;
        uint32_t break_seconds = 0;
    };

    StateStorage& storage_;
    std::array<Entry, kMaxActiveReminders> entries_{};
    uint16_t next_id_ = 1;

    ReminderResult Create(ReminderKind kind, bool persistent, std::string_view label,
                          const ClockSnapshot& clock);
    uint16_t AllocateId();
    Entry* FindEntry(uint16_t id);
    const Entry* FindEntry(uint16_t id) const;
    Entry* FindFreeEntry();
    bool HasPomodoro() const;
    bool ValidateLabel(std::string_view label) const;
    static bool IsIntervalKind(ReminderKind kind);
    static bool IsPersistenceFailure(SaveResult result);
    static bool IsPersistencePending(SaveResult result);
    static bool IsQuietWallTime(int64_t wall_seconds);
    static uint64_t AddMilliseconds(uint64_t value, uint64_t delta);
    static int64_t AddSeconds(int64_t value, int64_t delta);

    SaveResult Persist(const ClockSnapshot& clock);
    bool Serialize(PersistentState* state) const;
    bool Deserialize(const PersistentState& state);
    bool AdvanceInterval(Entry* entry, int64_t now_seconds, const ClockSnapshot& clock,
                         SaveResult* save_result);
    bool RemovePersistent(Entry* entry, const ClockSnapshot& clock, SaveResult* save_result);
    ReminderEvent FinishDue(Entry* entry, const ReminderTick& tick, bool missed);
};

}  // namespace maomi
