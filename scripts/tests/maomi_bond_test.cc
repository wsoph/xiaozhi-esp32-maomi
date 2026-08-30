#include "maomi_bond.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

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

maomi::ReliableClock ClockAt(maomi::DateTime local_time, uint64_t monotonic_ms,
                             bool trusted = true) {
    maomi::ReliableClock clock;
    clock.Observe({trusted, local_time, monotonic_ms});
    return clock;
}

maomi::PersistentState StateWithPoints(int32_t points) {
    maomi::PersistentState state;
    state.bond_points = points;
    return state;
}

maomi::PersistentState SavedState(const maomi::BondTracker& tracker,
                                  const maomi::PersistentState& current = {}) {
    return tracker.MergePersistentState(current);
}

class MemoryStorageBackend : public maomi::StorageBackend {
public:
    using Value = std::variant<int32_t, uint8_t, std::vector<uint8_t>>;

    maomi::BackendReadResult ReadInt32(const char* key, int32_t* value) override {
        return Read(key, value);
    }

    maomi::BackendReadResult ReadUInt8(const char* key, uint8_t* value) override {
        return Read(key, value);
    }

    maomi::BackendReadResult ReadBlob(const char* key, uint8_t* value, size_t capacity,
                                      size_t* size) override {
        const auto item = committed_.find(key);
        if (item == committed_.end()) {
            return maomi::BackendReadResult::kNotFound;
        }
        const auto* blob = std::get_if<std::vector<uint8_t>>(&item->second);
        if (blob == nullptr || blob->size() > capacity) {
            return maomi::BackendReadResult::kInvalidData;
        }
        std::copy(blob->begin(), blob->end(), value);
        *size = blob->size();
        return maomi::BackendReadResult::kOk;
    }

    bool WriteInt32(const char* key, int32_t value) override {
        pending_[key] = value;
        return true;
    }

    bool WriteUInt8(const char* key, uint8_t value) override {
        pending_[key] = value;
        return true;
    }

    bool WriteBlob(const char* key, const uint8_t* value, size_t size) override {
        pending_[key] = std::vector<uint8_t>(value, value + size);
        return true;
    }

    bool Commit() override {
        for (auto& [key, value] : pending_) {
            committed_[key] = std::move(value);
        }
        pending_.clear();
        return true;
    }

    template <typename T>
    void Seed(const char* key, T value) {
        committed_[key] = std::move(value);
    }

private:
    template <typename T>
    maomi::BackendReadResult Read(const char* key, T* value) {
        const auto item = committed_.find(key);
        if (item == committed_.end()) {
            return maomi::BackendReadResult::kNotFound;
        }
        const auto* stored = std::get_if<T>(&item->second);
        if (stored == nullptr) {
            return maomi::BackendReadResult::kInvalidData;
        }
        *value = *stored;
        return maomi::BackendReadResult::kOk;
    }

    std::unordered_map<std::string, Value> committed_;
    std::unordered_map<std::string, Value> pending_;
};

void TestEveryActionAwardsSpecifiedPointsAndHasIndependentCooldown() {
    struct Case {
        maomi::BondAction action;
        uint8_t points;
        uint64_t cooldown_ms;
    };
    constexpr std::array cases{
        Case{maomi::BondAction::kWake, 1, 5 * 60 * 1000},
        Case{maomi::BondAction::kConversation, 2, 10 * 60 * 1000},
        Case{maomi::BondAction::kPet, 2, 10 * 60 * 1000},
        Case{maomi::BondAction::kFeed, 2, 10 * 60 * 1000},
        Case{maomi::BondAction::kPlay, 3, 10 * 60 * 1000},
    };

    for (const auto& item : cases) {
        maomi::BondTracker tracker({});
        auto clock = ClockAt({2026, 8, 26, 9, 0, 0}, 1000);
        const auto first = tracker.Record(item.action, clock);
        CHECK(first.action_completed);
        CHECK(first.points_added == item.points);
        CHECK(!first.cooldown_active);

        clock = ClockAt({2026, 8, 26, 9, 1, 0}, 1001);
        const auto repeated = tracker.Record(item.action, clock);
        CHECK(repeated.action_completed);
        CHECK(repeated.points_added == 0);
        CHECK(repeated.cooldown_active);
        CHECK(
            tracker.GetSnapshot(clock).cooldown_remaining_ms[maomi::BondActionIndex(item.action)] ==
            item.cooldown_ms - 1);

        clock = ClockAt({2026, 8, 26, 9, 10, 0}, 1000 + item.cooldown_ms);
        const auto after_cooldown = tracker.Record(item.action, clock);
        CHECK(after_cooldown.points_added == item.points);
    }

    maomi::BondTracker tracker({});
    const auto clock = ClockAt({2026, 8, 26, 10, 0, 0}, 5000);
    CHECK(tracker.Record(maomi::BondAction::kPet, clock).points_added == 2);
    CHECK(tracker.Record(maomi::BondAction::kFeed, clock).points_added == 2);
    CHECK(tracker.Record(maomi::BondAction::kPlay, clock).points_added == 3);
}

void TestDailyLimitAllowsPartialFinalAwardAndDoesNotFailInteraction() {
    maomi::BondTracker tracker({});
    uint64_t now_ms = 0;
    auto action = [&](maomi::BondAction value) {
        const auto clock = ClockAt({2026, 8, 26, 12, 0, 0}, now_ms);
        const auto result = tracker.Record(value, clock);
        now_ms += 10 * 60 * 1000;
        return result;
    };

    for (int count = 0; count < 5; ++count) {
        CHECK(action(maomi::BondAction::kPlay).points_added == 3);
    }
    CHECK(action(maomi::BondAction::kPet).points_added == 2);
    CHECK(action(maomi::BondAction::kFeed).points_added == 2);
    const auto partial = action(maomi::BondAction::kConversation);
    CHECK(partial.action_completed);
    CHECK(partial.points_added == 1);
    CHECK(partial.daily_limit_reached);
    CHECK(SavedState(tracker).bond_points == 20);

    const auto capped = action(maomi::BondAction::kWake);
    CHECK(capped.action_completed);
    CHECK(capped.points_added == 0);
    CHECK(capped.daily_limit_reached);
    CHECK(SavedState(tracker).bond_points == 20);
}

void TestLevelBoundariesAndOneShotMilestones() {
    CHECK(maomi::BondLevelForPoints(0) == maomi::BondLevel::kAcquainted);
    CHECK(maomi::BondLevelForPoints(19) == maomi::BondLevel::kAcquainted);
    CHECK(maomi::BondLevelForPoints(20) == maomi::BondLevel::kFamiliar);
    CHECK(maomi::BondLevelForPoints(79) == maomi::BondLevel::kFamiliar);
    CHECK(maomi::BondLevelForPoints(80) == maomi::BondLevel::kClose);
    CHECK(maomi::BondLevelForPoints(199) == maomi::BondLevel::kClose);
    CHECK(maomi::BondLevelForPoints(200) == maomi::BondLevel::kInSync);
    CHECK(maomi::BondLevelForPoints(9999) == maomi::BondLevel::kInSync);

    struct Case {
        int32_t starting_points;
        maomi::BondAction action;
        maomi::BondLevel expected_level;
    };
    constexpr std::array cases{
        Case{19, maomi::BondAction::kWake, maomi::BondLevel::kFamiliar},
        Case{79, maomi::BondAction::kWake, maomi::BondLevel::kClose},
        Case{199, maomi::BondAction::kWake, maomi::BondLevel::kInSync},
    };
    for (const auto& item : cases) {
        maomi::BondTracker tracker(StateWithPoints(item.starting_points));
        auto clock = ClockAt({2026, 8, 26, 13, 0, 0}, 1000);
        const auto crossing = tracker.Record(item.action, clock);
        CHECK(crossing.milestone_triggered);
        CHECK(crossing.milestone == item.expected_level);
        CHECK(std::strlen(maomi::BondMilestoneText(crossing.milestone)) > 0);

        clock = ClockAt({2026, 8, 27, 13, 0, 0}, 700000);
        CHECK(!tracker.Record(maomi::BondAction::kConversation, clock).milestone_triggered);

        maomi::BondTracker restarted(SavedState(tracker));
        clock = ClockAt({2026, 8, 28, 13, 0, 0}, 10);
        CHECK(!restarted.Record(maomi::BondAction::kWake, clock).milestone_triggered);
    }
}

void TestCrossMidnightResetsOnlyDailyQuota() {
    maomi::BondTracker tracker({});
    uint64_t now_ms = 0;
    for (int count = 0; count < 7; ++count) {
        const auto clock = ClockAt({2026, 8, 26, 23, 30, 0}, now_ms);
        tracker.Record(maomi::BondAction::kPlay, clock);
        now_ms += 10 * 60 * 1000;
    }
    auto before = tracker.GetSnapshot(ClockAt({2026, 8, 26, 23, 59, 59}, now_ms));
    CHECK(before.total_points == 20);
    CHECK(before.today_points == 20);

    const auto next_day = ClockAt({2026, 8, 27, 0, 0, 0}, now_ms + 1);
    const auto awarded = tracker.Record(maomi::BondAction::kPet, next_day);
    CHECK(awarded.points_added == 2);
    const auto after = tracker.GetSnapshot(next_day);
    CHECK(after.total_points == 22);
    CHECK(after.today_points == 2);
    CHECK(after.companion_days == 2);
}

void TestInvalidTimeRollbackAndPowerCycleNeverReduceLongTermState() {
    maomi::BondTracker tracker({});
    auto invalid = ClockAt({2026, 8, 26, 12, 0, 0}, 1000, false);
    const auto ignored = tracker.Record(maomi::BondAction::kPlay, invalid);
    CHECK(ignored.action_completed);
    CHECK(ignored.points_added == 0);
    CHECK(ignored.time_unavailable);
    CHECK(SavedState(tracker).first_day == 0);
    CHECK(SavedState(tracker).max_companion_days == 0);

    auto day_one = ClockAt({2026, 8, 26, 12, 0, 0}, 2000);
    CHECK(tracker.ObserveTime(day_one));
    CHECK(SavedState(tracker).first_day == 20260826);
    CHECK(SavedState(tracker).max_companion_days == 1);
    CHECK(tracker.Record(maomi::BondAction::kPlay, day_one).points_added == 3);

    auto day_three = ClockAt({2026, 8, 28, 12, 0, 0}, 3000);
    CHECK(tracker.ObserveTime(day_three));
    CHECK(SavedState(tracker).max_companion_days == 3);
    const auto points_before_rollback = SavedState(tracker).bond_points;

    auto rolled_back = ClockAt({2026, 8, 27, 12, 0, 0}, 4000);
    CHECK(!tracker.ObserveTime(rolled_back));
    CHECK(SavedState(tracker).bond_points == points_before_rollback);
    CHECK(SavedState(tracker).max_companion_days == 3);
    CHECK(tracker.GetSnapshot(rolled_back).daily_date == 20260828);

    maomi::BondTracker restarted(SavedState(tracker));
    auto after_restart = ClockAt({2026, 8, 27, 12, 0, 0}, 5);
    restarted.ObserveTime(after_restart);
    CHECK(SavedState(restarted).bond_points == points_before_rollback);
    CHECK(SavedState(restarted).max_companion_days == 3);
}

void TestMaximumPointsSaturatesWithoutOverflow() {
    maomi::BondTracker tracker(StateWithPoints(9998));
    const auto clock = ClockAt({2026, 8, 26, 14, 0, 0}, 1000);
    const auto result = tracker.Record(maomi::BondAction::kPlay, clock);
    CHECK(result.points_added == 1);
    CHECK(SavedState(tracker).bond_points == 9999);
    CHECK(tracker.Record(maomi::BondAction::kFeed, clock).points_added == 0);
    CHECK(SavedState(tracker).bond_points == 9999);
}

void TestNvsPolicyRoundTripPreservesPointsDaysAndConsumedMilestone() {
    MemoryStorageBackend backend;
    backend.Seed(maomi::kSchemaVersionKey, maomi::kStorageSchemaVersion);
    backend.Seed(maomi::kFirstDayKey, int32_t{20260826});
    backend.Seed(maomi::kBondPointsKey, int32_t{19});
    backend.Seed(maomi::kMaxCompanionDaysKey, int32_t{1});
    backend.Seed(maomi::kManualQuietKey, uint8_t{0});
    backend.Seed(maomi::kRemindersKey, std::vector<uint8_t>{});

    maomi::StateStorage storage(backend);
    const auto loaded = storage.Load(0);
    CHECK(loaded.health == maomi::LoadHealth::kHealthy);

    maomi::BondTracker tracker(loaded.state);
    auto clock = ClockAt({2026, 8, 26, 15, 0, 0}, 1000);
    const auto crossing = tracker.Record(maomi::BondAction::kWake, clock);
    CHECK(crossing.milestone_triggered);
    CHECK(crossing.milestone == maomi::BondLevel::kFamiliar);
    CHECK(storage.Update(tracker.MergePersistentState(storage.GetState()),
                         maomi::WriteImportance::kNormal, 1000) == maomi::SaveResult::kDeferred);
    CHECK(storage.FlushIfDue(1000 + maomi::kNormalCommitDelayMs) == maomi::SaveResult::kCommitted);

    maomi::StateStorage after_power_cycle(backend);
    const auto restored = after_power_cycle.Load(10);
    CHECK(restored.state.bond_points == 20);
    CHECK(restored.state.first_day == 20260826);
    CHECK(restored.state.max_companion_days == 1);

    maomi::BondTracker restarted(restored.state);
    clock = ClockAt({2026, 8, 27, 15, 0, 0}, 20);
    const auto after_restart = restarted.Record(maomi::BondAction::kConversation, clock);
    CHECK(after_restart.points_added == 2);
    CHECK(!after_restart.milestone_triggered);
    CHECK(SavedState(restarted).max_companion_days == 2);
}

void TestPersistentMergeDoesNotOverwriteOtherModules() {
    maomi::PersistentState initial;
    initial.bond_points = 10;
    maomi::BondTracker tracker(initial);
    const auto clock = ClockAt({2026, 8, 26, 16, 0, 0}, 1000);
    CHECK(tracker.Record(maomi::BondAction::kWake, clock).points_added == 1);

    maomi::PersistentState current;
    current.manual_quiet = true;
    current.reminder_data[0] = 0xA5;
    current.reminder_data[1] = 0x5A;
    current.reminder_data_size = 2;
    const auto merged = tracker.MergePersistentState(current);
    CHECK(merged.first_day == 20260826);
    CHECK(merged.bond_points == 11);
    CHECK(merged.max_companion_days == 1);
    CHECK(merged.manual_quiet);
    CHECK(merged.reminder_data_size == 2);
    CHECK(merged.reminder_data[0] == 0xA5);
    CHECK(merged.reminder_data[1] == 0x5A);
}

void TestUserVisibleTextIsPositiveOrNeutral() {
    constexpr std::array levels{
        maomi::BondLevel::kAcquainted,
        maomi::BondLevel::kFamiliar,
        maomi::BondLevel::kClose,
        maomi::BondLevel::kInSync,
    };
    constexpr std::array<std::string_view, 8> forbidden{
        "责怪", "生病", "死亡", "离家", "惩罚", "讨厌", "失望", "不理你",
    };
    for (const auto level : levels) {
        const std::array texts{std::string_view(maomi::BondLevelText(level)),
                               std::string_view(maomi::BondMilestoneText(level))};
        for (const auto text : texts) {
            CHECK(!text.empty());
            for (const auto word : forbidden) {
                CHECK(text.find(word) == std::string_view::npos);
            }
        }
    }
}

}  // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<maomi::BondTracker>);
    TestEveryActionAwardsSpecifiedPointsAndHasIndependentCooldown();
    TestDailyLimitAllowsPartialFinalAwardAndDoesNotFailInteraction();
    TestLevelBoundariesAndOneShotMilestones();
    TestCrossMidnightResetsOnlyDailyQuota();
    TestInvalidTimeRollbackAndPowerCycleNeverReduceLongTermState();
    TestMaximumPointsSaturatesWithoutOverflow();
    TestNvsPolicyRoundTripPreservesPointsDaysAndConsumedMilestone();
    TestPersistentMergeDoesNotOverwriteOtherModules();
    TestUserVisibleTextIsPositiveOrNeutral();
    std::cout << "maomi bond tests passed" << std::endl;
    return 0;
}
