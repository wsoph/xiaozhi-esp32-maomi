#pragma once

#include "maomi_clock.h"
#include "maomi_storage.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace maomi {

constexpr uint8_t kDailyBondPointLimit = 20;
constexpr uint64_t kWakeBondCooldownMs = 5 * 60 * 1000;
constexpr uint64_t kRegularBondCooldownMs = 10 * 60 * 1000;

enum class BondAction : uint8_t {
    kWake,
    kConversation,
    kPet,
    kFeed,
    kPlay,
    kCount,
};

enum class BondLevel : uint8_t {
    kAcquainted,
    kFamiliar,
    kClose,
    kInSync,
};

constexpr size_t kBondActionCount = static_cast<size_t>(BondAction::kCount);

constexpr size_t BondActionIndex(BondAction action) { return static_cast<size_t>(action); }

BondLevel BondLevelForPoints(int32_t points);
const char* BondLevelText(BondLevel level);
const char* BondMilestoneText(BondLevel level);

struct BondSnapshot {
    int32_t total_points = 0;
    BondLevel level = BondLevel::kAcquainted;
    uint32_t companion_days = 0;
    uint8_t today_points = 0;
    int32_t daily_date = 0;
    std::array<uint64_t, kBondActionCount> cooldown_remaining_ms{};
};

struct BondUpdateResult {
    bool action_completed = false;
    uint8_t points_added = 0;
    bool cooldown_active = false;
    bool daily_limit_reached = false;
    bool time_unavailable = false;
    bool milestone_triggered = false;
    BondLevel milestone = BondLevel::kAcquainted;
    bool persistent_state_changed = false;
};

// Main-task-owned relationship policy. It consumes the existing reliable clock and a copy of the
// persisted state. Callers remain responsible for passing changed state to StateStorage, which
// preserves its bounded-write policy.
class BondTracker {
public:
    explicit BondTracker(const PersistentState& persistent_state);
    BondTracker(const BondTracker&) = delete;
    BondTracker& operator=(const BondTracker&) = delete;

    bool ObserveTime(const ReliableClock& clock);
    BondUpdateResult Record(BondAction action, const ReliableClock& clock);
    BondSnapshot GetSnapshot(const ReliableClock& clock) const;
    PersistentState MergePersistentState(const PersistentState& current) const;

private:
    int32_t first_day_ = 0;
    int32_t bond_points_ = 0;
    uint32_t max_companion_days_ = 0;
    int32_t daily_date_ = 0;
    uint8_t today_points_ = 0;
    std::array<uint64_t, kBondActionCount> last_action_ms_{};
    std::array<bool, kBondActionCount> has_last_action_{};

    static bool IsValidAction(BondAction action);
    static uint8_t PointsFor(BondAction action);
    static uint64_t CooldownFor(BondAction action);
    uint64_t CooldownRemaining(BondAction action, uint64_t monotonic_ms) const;
};

}  // namespace maomi
