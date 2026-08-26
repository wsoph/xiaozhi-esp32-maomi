#include "maomi_bond.h"

#include <algorithm>

namespace maomi {

BondLevel BondLevelForPoints(int32_t points) {
    if (points >= 200) {
        return BondLevel::kInSync;
    }
    if (points >= 80) {
        return BondLevel::kClose;
    }
    if (points >= 20) {
        return BondLevel::kFamiliar;
    }
    return BondLevel::kAcquainted;
}

const char* BondLevelText(BondLevel level) {
    switch (level) {
        case BondLevel::kAcquainted:
            return "初识";
        case BondLevel::kFamiliar:
            return "熟悉";
        case BondLevel::kClose:
            return "亲近";
        case BondLevel::kInSync:
            return "默契";
    }
    return "";
}

const char* BondMilestoneText(BondLevel level) {
    switch (level) {
        case BondLevel::kAcquainted:
            return "很高兴认识你，喵！";
        case BondLevel::kFamiliar:
            return "我们越来越熟悉啦，喵！";
        case BondLevel::kClose:
            return "和你待在一起真开心，喵！";
        case BondLevel::kInSync:
            return "我们越来越有默契啦，喵！";
    }
    return "";
}

BondTracker::BondTracker(const PersistentState& persistent_state)
    : first_day_(persistent_state.first_day),
      bond_points_(std::clamp(persistent_state.bond_points, int32_t{0}, kMaximumBondPoints)),
      max_companion_days_(std::min(persistent_state.max_companion_days, kMaximumCompanionDays)) {}

bool BondTracker::ObserveTime(const ReliableClock& clock) {
    const auto snapshot = clock.GetSnapshot();
    if (!snapshot.valid) {
        return false;
    }

    if (daily_date_ == 0 || snapshot.date_key > daily_date_) {
        daily_date_ = snapshot.date_key;
        today_points_ = 0;
    }

    bool changed = false;
    if (first_day_ == 0) {
        first_day_ = snapshot.date_key;
        changed = true;
    }

    const uint32_t companion_days = clock.CompanionDays(first_day_, max_companion_days_);
    if (companion_days > max_companion_days_) {
        max_companion_days_ = companion_days;
        changed = true;
    }
    return changed;
}

BondUpdateResult BondTracker::Record(BondAction action, const ReliableClock& clock) {
    BondUpdateResult result;
    if (!IsValidAction(action)) {
        return result;
    }
    result.action_completed = true;
    result.persistent_state_changed = ObserveTime(clock);

    const auto clock_snapshot = clock.GetSnapshot();
    if (!clock_snapshot.valid) {
        result.time_unavailable = true;
        return result;
    }

    if (CooldownRemaining(action, clock_snapshot.monotonic_ms) > 0) {
        result.cooldown_active = true;
        return result;
    }

    const size_t action_index = BondActionIndex(action);
    has_last_action_[action_index] = true;
    last_action_ms_[action_index] = clock_snapshot.monotonic_ms;

    const int daily_available = kDailyBondPointLimit - today_points_;
    const int total_available = kMaximumBondPoints - bond_points_;
    const int points_added =
        std::min({static_cast<int>(PointsFor(action)), daily_available, total_available});
    if (points_added > 0) {
        const BondLevel previous_level = BondLevelForPoints(bond_points_);
        bond_points_ += points_added;
        today_points_ += points_added;
        result.points_added = static_cast<uint8_t>(points_added);
        result.persistent_state_changed = true;

        const BondLevel current_level = BondLevelForPoints(bond_points_);
        if (current_level != previous_level) {
            result.milestone_triggered = true;
            result.milestone = current_level;
        }
    }

    result.daily_limit_reached = today_points_ >= kDailyBondPointLimit;
    return result;
}

BondSnapshot BondTracker::GetSnapshot(const ReliableClock& clock) const {
    BondSnapshot result;
    result.total_points = bond_points_;
    result.level = BondLevelForPoints(bond_points_);
    result.companion_days = max_companion_days_;
    result.today_points = today_points_;
    result.daily_date = daily_date_;

    const uint64_t monotonic_ms = clock.GetSnapshot().monotonic_ms;
    for (size_t index = 0; index < kBondActionCount; ++index) {
        result.cooldown_remaining_ms[index] =
            CooldownRemaining(static_cast<BondAction>(index), monotonic_ms);
    }
    return result;
}

PersistentState BondTracker::MergePersistentState(const PersistentState& current) const {
    PersistentState merged = current;
    merged.first_day = first_day_;
    merged.bond_points = bond_points_;
    merged.max_companion_days = max_companion_days_;
    return merged;
}

bool BondTracker::IsValidAction(BondAction action) {
    return BondActionIndex(action) < kBondActionCount;
}

uint8_t BondTracker::PointsFor(BondAction action) {
    switch (action) {
        case BondAction::kWake:
            return 1;
        case BondAction::kConversation:
        case BondAction::kPet:
        case BondAction::kFeed:
            return 2;
        case BondAction::kPlay:
            return 3;
        case BondAction::kCount:
            break;
    }
    return 0;
}

uint64_t BondTracker::CooldownFor(BondAction action) {
    return action == BondAction::kWake ? kWakeBondCooldownMs : kRegularBondCooldownMs;
}

uint64_t BondTracker::CooldownRemaining(BondAction action, uint64_t monotonic_ms) const {
    if (!IsValidAction(action)) {
        return 0;
    }
    const size_t index = BondActionIndex(action);
    if (!has_last_action_[index]) {
        return 0;
    }

    const uint64_t cooldown_ms = CooldownFor(action);
    if (monotonic_ms < last_action_ms_[index]) {
        return cooldown_ms;
    }
    const uint64_t elapsed_ms = monotonic_ms - last_action_ms_[index];
    return elapsed_ms >= cooldown_ms ? 0 : cooldown_ms - elapsed_ms;
}

}  // namespace maomi
