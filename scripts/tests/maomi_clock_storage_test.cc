#include "maomi_clock.h"
#include "maomi_storage.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {

static_assert(!std::is_copy_constructible_v<maomi::StateStorage>);

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

class FakeStorageBackend : public maomi::StorageBackend {
public:
    using Value = std::variant<int32_t, uint8_t, std::vector<uint8_t>>;

    maomi::BackendReadResult ReadInt32(const char* key, int32_t* value) override {
        return ReadValue(key, value);
    }

    maomi::BackendReadResult ReadUInt8(const char* key, uint8_t* value) override {
        return ReadValue(key, value);
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

    bool WriteInt32(const char* key, int32_t value) override { return Stage(key, value); }

    bool WriteUInt8(const char* key, uint8_t value) override { return Stage(key, value); }

    bool WriteBlob(const char* key, const uint8_t* value, size_t size) override {
        if (fail_writes_) {
            return false;
        }
        ++write_calls_;
        pending_[key] = std::vector<uint8_t>(value, value + size);
        return true;
    }

    bool Commit() override {
        ++commit_attempts_;
        if (fail_commit_) {
            return false;
        }
        for (auto& [key, value] : pending_) {
            committed_[key] = std::move(value);
        }
        pending_.clear();
        ++commit_count_;
        return true;
    }

    template <typename T>
    void Seed(const char* key, T value) {
        committed_[key] = std::move(value);
    }

    int commit_count() const { return commit_count_; }
    int commit_attempts() const { return commit_attempts_; }
    int write_calls() const { return write_calls_; }
    void set_fail_writes(bool fail) { fail_writes_ = fail; }
    void set_fail_commit(bool fail) { fail_commit_ = fail; }

private:
    template <typename T>
    maomi::BackendReadResult ReadValue(const char* key, T* value) {
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

    template <typename T>
    bool Stage(const char* key, T value) {
        if (fail_writes_) {
            return false;
        }
        ++write_calls_;
        pending_[key] = value;
        return true;
    }

    std::unordered_map<std::string, Value> committed_;
    std::unordered_map<std::string, Value> pending_;
    int commit_count_ = 0;
    int commit_attempts_ = 0;
    int write_calls_ = 0;
    bool fail_writes_ = false;
    bool fail_commit_ = false;
};

maomi::TimeSample Sample(bool server_time_set, maomi::DateTime local_time, uint64_t monotonic_ms) {
    return maomi::TimeSample{server_time_set, local_time, monotonic_ms};
}

void TestTrustedTimeRequiresServerSignalAndValidCivilTime() {
    maomi::ReliableClock clock;

    clock.Observe(Sample(false, {2026, 8, 25, 12, 0, 0}, 1000));
    CHECK(!clock.GetSnapshot().valid);

    clock.Observe(Sample(true, {2023, 12, 31, 23, 59, 59}, 2000));
    CHECK(!clock.GetSnapshot().valid);

    clock.Observe(Sample(true, {2026, 2, 29, 12, 0, 0}, 3000));
    CHECK(!clock.GetSnapshot().valid);

    clock.Observe(Sample(true, {2024, 2, 29, 12, 0, 0}, 4000));
    const auto snapshot = clock.GetSnapshot();
    CHECK(snapshot.valid);
    CHECK(snapshot.date_key == 20240229);
}

void TestCrossMidnightQuietWindow() {
    CHECK(maomi::ReliableClock::IsWithinDailyWindow({2026, 8, 25, 22, 0, 0}, 22, 0, 8, 0));
    CHECK(maomi::ReliableClock::IsWithinDailyWindow({2026, 8, 26, 7, 59, 59}, 22, 0, 8, 0));
    CHECK(!maomi::ReliableClock::IsWithinDailyWindow({2026, 8, 26, 8, 0, 0}, 22, 0, 8, 0));
    CHECK(!maomi::ReliableClock::IsWithinDailyWindow({2026, 8, 25, 21, 59, 59}, 22, 0, 8, 0));

    CHECK(maomi::ReliableClock::IsWithinDailyWindow({2026, 8, 25, 9, 30, 0}, 9, 0, 10, 0));
    CHECK(!maomi::ReliableClock::IsWithinDailyWindow({2026, 8, 25, 10, 0, 0}, 9, 0, 10, 0));
    CHECK(!maomi::ReliableClock::IsWithinDailyWindow({2026, 13, 25, 9, 30, 0}, 9, 0, 10, 0));
}

void TestAbsoluteAlarmDoesNotFireEarlyAfterRollback() {
    maomi::ReliableClock clock;
    const maomi::DateTime alarm{2026, 8, 26, 8, 0, 0};

    clock.Observe(Sample(true, {2026, 8, 26, 7, 59, 59}, 1000));
    CHECK(!clock.HasReached(alarm));

    clock.Observe(Sample(true, {2026, 8, 25, 20, 0, 0}, 2000));
    CHECK(clock.GetSnapshot().wall_clock_rollback_count == 1);
    CHECK(!clock.HasReached(alarm));

    clock.Observe(Sample(true, {2026, 8, 26, 8, 0, 0}, 3000));
    CHECK(clock.HasReached(alarm));
}

void TestRelativeDeadlineUsesOnlyMonotonicTime() {
    maomi::ReliableClock clock;
    clock.Observe(Sample(true, {2026, 8, 25, 12, 0, 0}, 1000));
    const auto deadline = clock.DeadlineAfter(5000);

    clock.Observe(Sample(true, {2026, 8, 24, 12, 0, 0}, 5999));
    CHECK(!clock.HasReached(deadline));
    clock.Observe(Sample(false, {1970, 1, 1, 0, 0, 0}, 6000));
    CHECK(clock.HasReached(deadline));
}

void TestCompanionDaysNeverDecrease() {
    maomi::ReliableClock clock;
    clock.Observe(Sample(false, {2026, 8, 25, 12, 0, 0}, 1000));
    CHECK(clock.CompanionDays(20260824, 7) == 7);

    clock.Observe(Sample(true, {2026, 8, 25, 12, 0, 0}, 2000));
    CHECK(clock.CompanionDays(20260824, 0) == 2);
    CHECK(clock.CompanionDays(20260824, 7) == 7);

    clock.Observe(Sample(true, {2026, 8, 23, 12, 0, 0}, 3000));
    CHECK(clock.CompanionDays(20260824, 7) == 7);
    CHECK(clock.CompanionDays(20260229, 7) == 7);
}

maomi::PersistentState PopulatedState() {
    maomi::PersistentState state;
    state.first_day = 20260820;
    state.bond_points = 42;
    state.max_companion_days = 6;
    state.manual_quiet = true;
    state.reminder_data_size = 4;
    state.reminder_data[0] = 0x10;
    state.reminder_data[1] = 0x20;
    state.reminder_data[2] = 0x30;
    state.reminder_data[3] = 0x40;
    return state;
}

void SeedCurrentSchema(FakeStorageBackend& backend) {
    backend.Seed(maomi::kSchemaVersionKey, maomi::kStorageSchemaVersion);
}

void TestStorageNamespaceAndKeyLimits() {
    CHECK(std::strcmp(maomi::kStorageNamespace, "maomi") == 0);
    const char* keys[] = {maomi::kSchemaVersionKey, maomi::kFirstDayKey,
                          maomi::kBondPointsKey,    maomi::kMaxCompanionDaysKey,
                          maomi::kManualQuietKey,   maomi::kRemindersKey};
    for (const auto* key : keys) {
        CHECK(std::strlen(key) > 0);
        CHECK(std::strlen(key) <= 15);
    }
}

void TestPersistentFieldsSurviveRestart() {
    FakeStorageBackend backend;
    maomi::StateStorage first(backend);
    const auto initial = first.Load(1000);
    CHECK(initial.health == maomi::LoadHealth::kMigrated);
    CHECK(initial.write_allowed);

    const auto expected = PopulatedState();
    CHECK(first.Update(expected, maomi::WriteImportance::kImportant, 1000) ==
          maomi::SaveResult::kCommitted);
    CHECK(backend.commit_count() == 1);

    maomi::StateStorage restarted(backend);
    const auto restored = restarted.Load(0);
    CHECK(restored.health == maomi::LoadHealth::kHealthy);
    CHECK(restored.state == expected);
}

void TestCorruptFieldsFallBackWithoutPreventingStartup() {
    FakeStorageBackend backend;
    SeedCurrentSchema(backend);
    backend.Seed(maomi::kFirstDayKey, int32_t{20260229});
    backend.Seed(maomi::kBondPointsKey, int32_t{-1});
    backend.Seed(maomi::kMaxCompanionDaysKey, int32_t{-10});
    backend.Seed(maomi::kManualQuietKey, uint8_t{2});
    backend.Seed(maomi::kRemindersKey, std::vector<uint8_t>(maomi::kMaxReminderDataSize + 1, 0x55));

    maomi::StateStorage storage(backend);
    const auto loaded = storage.Load(0);
    CHECK(loaded.health == maomi::LoadHealth::kRecovered);
    CHECK(loaded.write_allowed);
    CHECK(loaded.state == maomi::PersistentState{});
    CHECK(storage.NeedsWrite());
    CHECK(storage.FlushIfDue(maomi::kNormalCommitDelayMs) == maomi::SaveResult::kCommitted);
}

void TestLegacySchemaMigratesButFutureSchemaIsNotDowngraded() {
    FakeStorageBackend legacy_backend;
    legacy_backend.Seed(maomi::kSchemaVersionKey, int32_t{0});
    legacy_backend.Seed(maomi::kBondPointsKey, int32_t{18});
    maomi::StateStorage legacy(legacy_backend);
    const auto migrated = legacy.Load(0);
    CHECK(migrated.health == maomi::LoadHealth::kMigrated);
    CHECK(migrated.state.bond_points == 18);
    CHECK(legacy.FlushIfDue(maomi::kNormalCommitDelayMs) == maomi::SaveResult::kCommitted);

    maomi::StateStorage after_migration(legacy_backend);
    CHECK(after_migration.Load(0).health == maomi::LoadHealth::kHealthy);

    FakeStorageBackend future_backend;
    future_backend.Seed(maomi::kSchemaVersionKey, int32_t{99});
    future_backend.Seed(maomi::kBondPointsKey, int32_t{77});
    maomi::StateStorage future(future_backend);
    const auto future_loaded = future.Load(0);
    CHECK(future_loaded.health == maomi::LoadHealth::kFutureSchema);
    CHECK(!future_loaded.write_allowed);
    CHECK(future_loaded.state.bond_points == 77);
    auto changed = future_loaded.state;
    changed.bond_points = 78;
    CHECK(future.Update(changed, maomi::WriteImportance::kImportant, 0) ==
          maomi::SaveResult::kWriteBlocked);
    CHECK(future_backend.commit_attempts() == 0);
}

void TestNormalWritesAreCoalescedForFiveMinutes() {
    FakeStorageBackend backend;
    SeedCurrentSchema(backend);
    maomi::StateStorage storage(backend);
    auto state = storage.Load(0).state;

    for (int i = 1; i <= 1000; ++i) {
        state.bond_points = i;
        CHECK(storage.Update(state, maomi::WriteImportance::kNormal, static_cast<uint64_t>(i)) ==
              maomi::SaveResult::kDeferred);
    }
    CHECK(backend.commit_attempts() == 0);
    CHECK(storage.FlushIfDue(1 + maomi::kNormalCommitDelayMs - 1) == maomi::SaveResult::kDeferred);
    CHECK(storage.FlushIfDue(1 + maomi::kNormalCommitDelayMs) == maomi::SaveResult::kCommitted);
    CHECK(backend.commit_count() == 1);
    CHECK(backend.write_calls() == 1);
}

void TestImportantWritesAreImmediateButRateLimited() {
    FakeStorageBackend backend;
    SeedCurrentSchema(backend);
    maomi::StateStorage storage(backend);
    auto state = storage.Load(0).state;

    state.manual_quiet = true;
    CHECK(storage.Update(state, maomi::WriteImportance::kImportant, 1000) ==
          maomi::SaveResult::kCommitted);
    state.bond_points = 1;
    CHECK(storage.Update(state, maomi::WriteImportance::kImportant, 1001) ==
          maomi::SaveResult::kRateLimited);
    CHECK(backend.commit_count() == 1);
    CHECK(storage.FlushIfDue(1000 + maomi::kImportantCommitIntervalMs - 1) ==
          maomi::SaveResult::kRateLimited);
    CHECK(storage.FlushIfDue(1000 + maomi::kImportantCommitIntervalMs) ==
          maomi::SaveResult::kCommitted);
    CHECK(backend.commit_count() == 2);
}

void TestFailedCommitRemainsPendingAndInvalidUpdatesAreRejected() {
    FakeStorageBackend backend;
    SeedCurrentSchema(backend);
    bool saw_write_failure = false;
    bool saw_null_log_context = false;
    maomi::StateStorage storage(backend, [&](maomi::StorageLogEvent event, const char* context) {
        if (event == maomi::StorageLogEvent::kWriteFailure) {
            saw_write_failure = true;
            saw_null_log_context = context == nullptr;
        }
    });
    auto state = storage.Load(0).state;

    state.bond_points = 1;
    backend.set_fail_commit(true);
    CHECK(storage.Update(state, maomi::WriteImportance::kImportant, 1000) ==
          maomi::SaveResult::kFailed);
    CHECK(saw_write_failure);
    CHECK(!saw_null_log_context);
    CHECK(storage.NeedsWrite());
    backend.set_fail_commit(false);
    CHECK(storage.FlushIfDue(1000) == maomi::SaveResult::kRateLimited);
    CHECK(storage.FlushIfDue(1000 + maomi::kImportantCommitIntervalMs) ==
          maomi::SaveResult::kCommitted);

    auto invalid = state;
    invalid.bond_points = maomi::kMaximumBondPoints + 1;
    CHECK(storage.Update(invalid, maomi::WriteImportance::kNormal, 2000) ==
          maomi::SaveResult::kInvalidState);
}

}  // namespace

int main() {
    TestTrustedTimeRequiresServerSignalAndValidCivilTime();
    TestCrossMidnightQuietWindow();
    TestAbsoluteAlarmDoesNotFireEarlyAfterRollback();
    TestRelativeDeadlineUsesOnlyMonotonicTime();
    TestCompanionDaysNeverDecrease();
    TestStorageNamespaceAndKeyLimits();
    TestPersistentFieldsSurviveRestart();
    TestCorruptFieldsFallBackWithoutPreventingStartup();
    TestLegacySchemaMigratesButFutureSchemaIsNotDowngraded();
    TestNormalWritesAreCoalescedForFiveMinutes();
    TestImportantWritesAreImmediateButRateLimited();
    TestFailedCommitRemainsPendingAndInvalidUpdatesAreRejected();
    std::cout << "maomi clock/storage tests passed" << std::endl;
    return 0;
}
