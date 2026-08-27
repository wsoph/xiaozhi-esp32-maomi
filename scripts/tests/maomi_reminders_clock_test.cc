#include "maomi_reminders.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
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
        pending_[key] = std::vector<uint8_t>(value, value + size);
        return true;
    }
    bool Commit() override {
        if (fail_commit) {
            return false;
        }
        for (auto& [key, value] : pending_) {
            committed_[key] = std::move(value);
        }
        pending_.clear();
        return true;
    }
    void SeedInt32(const char* key, int32_t value) { committed_[key] = value; }
    void SeedBlob(std::vector<uint8_t> blob) { committed_[maomi::kRemindersKey] = std::move(blob); }
    std::vector<uint8_t> ReminderBlob() const {
        const auto item = committed_.find(maomi::kRemindersKey);
        if (item == committed_.end()) {
            return {};
        }
        const auto* blob = std::get_if<std::vector<uint8_t>>(&item->second);
        return blob == nullptr ? std::vector<uint8_t>{} : *blob;
    }

    bool fail_commit = false;

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
        pending_[key] = value;
        return true;
    }

    std::unordered_map<std::string, Value> committed_;
    std::unordered_map<std::string, Value> pending_;
};

maomi::ClockSnapshot Clock(uint64_t monotonic_ms, maomi::DateTime local_time = {},
                           bool valid = false) {
    return {.valid = valid, .local_time = local_time, .monotonic_ms = monotonic_ms};
}

maomi::ReminderTick Tick(uint64_t monotonic_ms, maomi::DateTime local_time = {}, bool valid = false,
                         bool busy = false, bool low_battery = false) {
    return {.clock = Clock(monotonic_ms, local_time, valid),
            .device_busy = busy,
            .low_battery = low_battery};
}

uint32_t UpdateChecksum(uint32_t checksum, const uint8_t* data, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        checksum ^= data[index];
        checksum *= 16777619U;
    }
    return checksum;
}

void PatchReminderChecksum(std::vector<uint8_t>* blob) {
    CHECK(blob != nullptr);
    CHECK(blob->size() >= 12);
    uint32_t checksum = UpdateChecksum(2166136261U, blob->data(), 8);
    checksum = UpdateChecksum(checksum, blob->data() + 12, blob->size() - 12);
    for (size_t index = 0; index < 4; ++index) {
        (*blob)[8 + index] = static_cast<uint8_t>(checksum >> (index * 8));
    }
}

void PatchUInt32(std::vector<uint8_t>* blob, size_t offset, uint32_t value) {
    CHECK(blob != nullptr);
    CHECK(offset + 4 <= blob->size());
    for (size_t index = 0; index < 4; ++index) {
        (*blob)[offset + index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

struct Fixture {
    FakeStorageBackend backend;
    maomi::StateStorage storage{backend};
    maomi::ReminderEngine engine{storage};

    Fixture() {
        storage.Load(0);
        CHECK(engine.Restore(Clock(0)).status != maomi::RestoreStatus::kUnavailable);
    }
};

void TestCountdownBoundsAndMonotonicClock() {
    Fixture fixture;
    const auto one_second = fixture.engine.StartCountdown(1, "tea", Clock(1000));
    CHECK(one_second.status == maomi::ReminderStatus::kAccepted);
    CHECK(!one_second.persistent);
    CHECK(fixture.engine.Update(Tick(1999)).state == maomi::ReminderEventState::kNone);
    const auto due = fixture.engine.Update(Tick(2000));
    CHECK(due.state == maomi::ReminderEventState::kTriggered);
    CHECK(due.id == one_second.id);
    CHECK(due.kind == maomi::ReminderKind::kCountdown);
    CHECK(due.audible);
    CHECK(fixture.engine.Update(Tick(2001)).state == maomi::ReminderEventState::kNone);

    const auto day = fixture.engine.StartCountdown(86400, "day", Clock(3000));
    CHECK(day.status == maomi::ReminderStatus::kAccepted);
    CHECK(fixture.engine.Update(Tick(3000 + 86400ULL * 1000 - 1)).state ==
          maomi::ReminderEventState::kNone);
    CHECK(fixture.engine.Update(Tick(3000 + 86400ULL * 1000)).id == day.id);
    CHECK(fixture.engine.StartCountdown(0, "", Clock(3000)).status ==
          maomi::ReminderStatus::kInvalidArgument);
    CHECK(fixture.engine.StartCountdown(86401, "", Clock(3000)).status ==
          maomi::ReminderStatus::kInvalidArgument);
}

void TestAlarmDateValidationAndTrustedTime() {
    Fixture fixture;
    const auto trusted_now = Clock(0, {2028, 2, 28, 8, 0, 0}, true);
    CHECK(fixture.engine.SetAlarm({2026, 2, 29, 8, 0, 0}, "bad", trusted_now).status ==
          maomi::ReminderStatus::kInvalidArgument);
    CHECK(fixture.engine.SetAlarm({2028, 2, 29, 8, 0, 0}, "untrusted", Clock(0)).status ==
          maomi::ReminderStatus::kInvalidArgument);
    CHECK(fixture.engine.ActiveCount() == 0);
    const auto leap = fixture.engine.SetAlarm({2028, 2, 29, 8, 0, 0}, "leap", trusted_now);
    CHECK(leap.status == maomi::ReminderStatus::kAccepted);
    CHECK(leap.persistent);

    CHECK(fixture.engine.Update(Tick(1000, {2030, 1, 1, 0, 0, 0}, false)).state ==
          maomi::ReminderEventState::kNone);
    CHECK(fixture.engine.Update(Tick(2000, {2028, 2, 29, 7, 59, 59}, true)).state ==
          maomi::ReminderEventState::kNone);
    const auto due = fixture.engine.Update(Tick(5000, {2028, 2, 29, 8, 0, 0}, true));
    CHECK(due.state == maomi::ReminderEventState::kTriggered);
    CHECK(due.id == leap.id);
    CHECK(due.audible);
}

void TestPomodoroAlternatesAndStopsAfterCycles() {
    Fixture fixture;
    const auto pomodoro = fixture.engine.StartPomodoro(1, 1, 2, Clock(0));
    CHECK(pomodoro.status == maomi::ReminderStatus::kAccepted);
    const auto work_one = fixture.engine.Update(Tick(60 * 1000));
    CHECK(work_one.id == pomodoro.id);
    CHECK(work_one.phase == maomi::ReminderPhase::kWork);
    CHECK(fixture.engine.Find(pomodoro.id)->phase == maomi::ReminderPhase::kBreak);
    const auto break_one = fixture.engine.Update(Tick(120 * 1000));
    CHECK(break_one.phase == maomi::ReminderPhase::kBreak);
    CHECK(fixture.engine.Find(pomodoro.id)->phase == maomi::ReminderPhase::kWork);
    const auto work_two = fixture.engine.Update(Tick(180 * 1000));
    CHECK(work_two.phase == maomi::ReminderPhase::kWork);
    CHECK(fixture.engine.Find(pomodoro.id) == nullptr);
}

void TestRestartRestoresOnlyPersistentPlans() {
    FakeStorageBackend backend;
    maomi::StateStorage first_storage(backend);
    first_storage.Load(0);
    maomi::ReminderEngine first(first_storage);
    first.Restore(Clock(0));
    CHECK(first.StartCountdown(60, "temporary", Clock(0)).status ==
          maomi::ReminderStatus::kAccepted);
    const auto alarm =
        first.SetAlarm({2028, 3, 1, 9, 0, 0}, "alarm", Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    CHECK(alarm.status == maomi::ReminderStatus::kAccepted);
    const auto water = first.StartInterval(maomi::ReminderKind::kWater, 30, "water",
                                           Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    CHECK(water.status == maomi::ReminderStatus::kAccepted);
    CHECK(first_storage.FlushIfDue(maomi::kImportantCommitIntervalMs) ==
          maomi::SaveResult::kCommitted);
    CHECK(first.StartPomodoro(25, 5, 2, Clock(0)).status == maomi::ReminderStatus::kAccepted);

    maomi::StateStorage restarted_storage(backend);
    restarted_storage.Load(0);
    maomi::ReminderEngine restarted(restarted_storage);
    CHECK(restarted.Restore(Clock(0, {2028, 3, 1, 8, 1, 0}, true)).status ==
          maomi::RestoreStatus::kRestored);
    const auto list = restarted.List(Clock(0, {2028, 3, 1, 8, 1, 0}, true));
    CHECK(list.count == 2);
    CHECK(restarted.Find(alarm.id) != nullptr);
    CHECK(restarted.Find(water.id) != nullptr);
}

void TestRestartCatchUpAndMissedAlarmRules() {
    FakeStorageBackend backend;
    maomi::StateStorage first_storage(backend);
    first_storage.Load(0);
    maomi::ReminderEngine first(first_storage);
    first.Restore(Clock(0));
    const auto alarm =
        first.SetAlarm({2028, 3, 1, 9, 0, 0}, "near", Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    CHECK(alarm.status == maomi::ReminderStatus::kAccepted);

    maomi::StateStorage near_storage(backend);
    near_storage.Load(0);
    maomi::ReminderEngine near(near_storage);
    near.Restore(Clock(1000, {2028, 3, 1, 9, 4, 59}, true));
    const auto caught_up = near.Update(Tick(1000, {2028, 3, 1, 9, 4, 59}, true));
    CHECK(caught_up.state == maomi::ReminderEventState::kTriggered);
    CHECK(caught_up.id == alarm.id);
    CHECK(near.Update(Tick(1001, {2028, 3, 1, 9, 5, 0}, true)).state ==
          maomi::ReminderEventState::kNone);

    FakeStorageBackend late_backend;
    maomi::StateStorage late_first_storage(late_backend);
    late_first_storage.Load(0);
    maomi::ReminderEngine late_first(late_first_storage);
    late_first.Restore(Clock(0));
    const auto late_alarm =
        late_first.SetAlarm({2028, 3, 1, 9, 0, 0}, "late", Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    maomi::StateStorage late_storage(late_backend);
    late_storage.Load(0);
    maomi::ReminderEngine late(late_storage);
    late.Restore(Clock(1000, {2028, 3, 1, 9, 10, 0}, true));
    const auto boundary = late.Update(Tick(1000, {2028, 3, 1, 9, 10, 0}, true));
    CHECK(boundary.state == maomi::ReminderEventState::kTriggered);
    CHECK(boundary.id == late_alarm.id);
    CHECK(late.Find(late_alarm.id) == nullptr);

    FakeStorageBackend expired_backend;
    maomi::StateStorage expired_first_storage(expired_backend);
    expired_first_storage.Load(0);
    maomi::ReminderEngine expired_first(expired_first_storage);
    expired_first.Restore(Clock(0));
    const auto expired_alarm = expired_first.SetAlarm({2028, 3, 1, 9, 0, 0}, "expired",
                                                      Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    maomi::StateStorage expired_storage(expired_backend);
    expired_storage.Load(0);
    maomi::ReminderEngine expired(expired_storage);
    expired.Restore(Clock(1000, {2028, 3, 1, 9, 10, 1}, true));
    const auto missed = expired.Update(Tick(1000, {2028, 3, 1, 9, 10, 1}, true));
    CHECK(missed.state == maomi::ReminderEventState::kMissed);
    CHECK(missed.id == expired_alarm.id);
    CHECK(!missed.audible);
    CHECK(expired.Find(expired_alarm.id) == nullptr);
}

void TestQuietHoursBusyExpiryAndLowBattery() {
    Fixture fixture;
    const auto alarm = fixture.engine.SetAlarm({2028, 3, 1, 23, 0, 0}, "night",
                                               Clock(0, {2028, 3, 1, 22, 50, 0}, true));
    const auto water = fixture.engine.StartInterval(maomi::ReminderKind::kWater, 10, "water",
                                                    Clock(0, {2028, 3, 1, 22, 50, 0}, true));
    CHECK(alarm.status == maomi::ReminderStatus::kAccepted);
    CHECK(water.status == maomi::ReminderStatus::kAccepted);
    const auto night_alarm = fixture.engine.Update(Tick(5000, {2028, 3, 1, 23, 0, 0}, true));
    CHECK(night_alarm.kind == maomi::ReminderKind::kAlarm);
    CHECK(night_alarm.audible);
    const auto night_water = fixture.engine.Update(Tick(10000, {2028, 3, 1, 23, 0, 0}, true));
    CHECK(night_water.kind == maomi::ReminderKind::kWater);
    CHECK(!night_water.audible);

    const auto countdown = fixture.engine.StartCountdown(1, "busy", Clock(2000));
    CHECK(fixture.engine.Update(Tick(3000, {}, false, true)).state ==
          maomi::ReminderEventState::kNone);
    const auto delayed = fixture.engine.Update(Tick(3000 + 4 * 60 * 1000));
    CHECK(delayed.state == maomi::ReminderEventState::kTriggered);
    CHECK(delayed.id == countdown.id);
    CHECK(fixture.engine.Update(Tick(3001 + 4 * 60 * 1000)).state ==
          maomi::ReminderEventState::kNone);

    const auto expired = fixture.engine.StartCountdown(1, "expired", Clock(500000));
    CHECK(fixture.engine.Update(Tick(501000, {}, false, true)).state ==
          maomi::ReminderEventState::kNone);
    const auto missed = fixture.engine.Update(Tick(501000 + 5 * 60 * 1000));
    CHECK(missed.state == maomi::ReminderEventState::kMissed);
    CHECK(missed.id == expired.id);
    CHECK(!missed.audible);

    const auto low = fixture.engine.StartCountdown(1, "low", Clock(900000));
    const auto low_event = fixture.engine.Update(Tick(901000, {}, false, false, true));
    CHECK(low_event.id == low.id);
    CHECK(!low_event.audible);
}

void TestAlarmWaitsForDurableRemovalBeforeTriggering() {
    FakeStorageBackend backend;
    maomi::StateStorage storage(backend);
    storage.Load(0);
    maomi::ReminderEngine engine(storage);
    engine.Restore(Clock(0));
    const auto alarm =
        engine.SetAlarm({2028, 3, 1, 8, 0, 1}, "once", Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    CHECK(alarm.status == maomi::ReminderStatus::kAccepted);
    CHECK(engine.Update(Tick(1000, {2028, 3, 1, 8, 0, 1}, true)).state ==
          maomi::ReminderEventState::kNone);
    const auto fired =
        engine.Update(Tick(maomi::kImportantCommitIntervalMs, {2028, 3, 1, 8, 0, 5}, true));
    CHECK(fired.state == maomi::ReminderEventState::kTriggered);
    CHECK(fired.id == alarm.id);

    maomi::StateStorage restarted_storage(backend);
    restarted_storage.Load(0);
    maomi::ReminderEngine restarted(restarted_storage);
    CHECK(restarted.Restore(Clock(0, {2028, 3, 1, 8, 0, 6}, true)).status ==
          maomi::RestoreStatus::kEmpty);
    CHECK(restarted.Update(Tick(1, {2028, 3, 1, 8, 0, 6}, true)).state ==
          maomi::ReminderEventState::kNone);
}

void TestFailedAlarmRemovalDoesNotEmitOrRepeat() {
    FakeStorageBackend backend;
    maomi::StateStorage storage(backend);
    storage.Load(0);
    maomi::ReminderEngine engine(storage);
    engine.Restore(Clock(0));
    const auto alarm =
        engine.SetAlarm({2028, 3, 1, 8, 0, 10}, "retry", Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    backend.fail_commit = true;
    CHECK(engine.Update(Tick(10000, {2028, 3, 1, 8, 0, 10}, true)).state ==
          maomi::ReminderEventState::kNone);
    backend.fail_commit = false;
    const auto fired = engine.Update(
        Tick(10000 + maomi::kImportantCommitIntervalMs, {2028, 3, 1, 8, 0, 15}, true));
    CHECK(fired.state == maomi::ReminderEventState::kTriggered);
    CHECK(fired.id == alarm.id);

    maomi::StateStorage restarted_storage(backend);
    restarted_storage.Load(0);
    maomi::ReminderEngine restarted(restarted_storage);
    CHECK(restarted.Restore(Clock(0, {2028, 3, 1, 8, 0, 16}, true)).status ==
          maomi::RestoreStatus::kEmpty);
}

void TestAlarmPersistenceRetryDoesNotBecomeBusyExpiry() {
    FakeStorageBackend backend;
    maomi::StateStorage storage(backend);
    storage.Load(0);
    maomi::ReminderEngine engine(storage);
    engine.Restore(Clock(0));
    const auto alarm =
        engine.SetAlarm({2028, 3, 1, 8, 0, 10}, "retry", Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    CHECK(alarm.status == maomi::ReminderStatus::kAccepted);

    backend.fail_commit = true;
    CHECK(engine.Update(Tick(10000, {2028, 3, 1, 8, 0, 10}, true)).state ==
          maomi::ReminderEventState::kNone);
    backend.fail_commit = false;
    const auto fired = engine.Update(Tick(310000, {2028, 3, 1, 8, 5, 10}, true));
    CHECK(fired.state == maomi::ReminderEventState::kTriggered);
    CHECK(fired.id == alarm.id);
    CHECK(fired.audible);
}

void TestRestoredAlarmCannotDeferPastRestartWindow() {
    FakeStorageBackend backend;
    maomi::StateStorage first_storage(backend);
    first_storage.Load(0);
    maomi::ReminderEngine first(first_storage);
    first.Restore(Clock(0));
    const auto alarm =
        first.SetAlarm({2028, 3, 1, 9, 0, 0}, "late", Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    CHECK(alarm.status == maomi::ReminderStatus::kAccepted);

    maomi::StateStorage restarted_storage(backend);
    restarted_storage.Load(0);
    maomi::ReminderEngine restarted(restarted_storage);
    CHECK(restarted.Restore(Clock(1000, {2028, 3, 1, 9, 9, 59}, true)).status ==
          maomi::RestoreStatus::kRestored);
    CHECK(restarted.Update(Tick(1000, {2028, 3, 1, 9, 9, 59}, true, true)).state ==
          maomi::ReminderEventState::kNone);
    CHECK(restarted.Update(Tick(2000, {2028, 3, 1, 9, 10, 0}, true, true)).state ==
          maomi::ReminderEventState::kNone);
    const auto missed = restarted.Update(Tick(3000, {2028, 3, 1, 9, 10, 1}, true, true));
    CHECK(missed.state == maomi::ReminderEventState::kMissed);
    CHECK(missed.id == alarm.id);
    CHECK(!missed.audible);
}

void TestPendingIntervalDoesNotRepeatAfterRestart() {
    FakeStorageBackend backend;
    maomi::StateStorage first_storage(backend);
    first_storage.Load(0);
    maomi::ReminderEngine first(first_storage);
    first.Restore(Clock(0));
    const auto water = first.StartInterval(maomi::ReminderKind::kWater, 10, "water",
                                           Clock(0, {2028, 3, 1, 8, 0, 0}, true));
    CHECK(water.status == maomi::ReminderStatus::kAccepted);
    const auto first_event = first.Update(Tick(1000, {2028, 3, 1, 8, 10, 0}, true));
    CHECK(first_event.state == maomi::ReminderEventState::kTriggered);
    CHECK(first_event.id == water.id);
    CHECK(first_event.persistence_pending);

    maomi::StateStorage restarted_storage(backend);
    restarted_storage.Load(0);
    maomi::ReminderEngine restarted(restarted_storage);
    CHECK(restarted.Restore(Clock(0, {2028, 3, 1, 8, 10, 1}, true)).status ==
          maomi::RestoreStatus::kRestored);
    CHECK(restarted.Update(Tick(1, {2028, 3, 1, 8, 10, 1}, true)).state ==
          maomi::ReminderEventState::kNone);
    const auto* restored = restarted.Find(water.id);
    CHECK(restored != nullptr);
    CHECK(restored->next_wall_time_seconds >
          maomi::ReminderEngine::CivilSeconds({2028, 3, 1, 8, 10, 1}));
}

void TestIntervalsUseActualTriggerTimeForQuietHours() {
    Fixture direct;
    const auto water = direct.engine.StartInterval(maomi::ReminderKind::kWater, 10, "water",
                                                   Clock(0, {2028, 3, 1, 21, 49, 0}, true));
    const auto direct_event = direct.engine.Update(Tick(5000, {2028, 3, 1, 22, 0, 0}, true));
    CHECK(direct_event.id == water.id);
    CHECK(!direct_event.audible);

    Fixture delayed;
    const auto sedentary = delayed.engine.StartInterval(
        maomi::ReminderKind::kSedentary, 10, "stand", Clock(0, {2028, 3, 1, 21, 49, 0}, true));
    CHECK(delayed.engine.Update(Tick(5000, {2028, 3, 1, 21, 59, 0}, true, true)).state ==
          maomi::ReminderEventState::kNone);
    const auto delayed_event = delayed.engine.Update(Tick(65000, {2028, 3, 1, 22, 0, 0}, true));
    CHECK(delayed_event.id == sedentary.id);
    CHECK(!delayed_event.audible);
}

void TestReadOnlyStorageIsUnavailable() {
    FakeStorageBackend empty_backend;
    empty_backend.SeedInt32(maomi::kSchemaVersionKey, maomi::kStorageSchemaVersion + 1);
    maomi::StateStorage empty_storage(empty_backend);
    empty_storage.Load(0);
    maomi::ReminderEngine empty_engine(empty_storage);
    CHECK(empty_engine.Restore(Clock(0)).status == maomi::RestoreStatus::kUnavailable);

    FakeStorageBackend backend;
    maomi::StateStorage writable_storage(backend);
    writable_storage.Load(0);
    maomi::ReminderEngine writable(writable_storage);
    writable.Restore(Clock(0));
    CHECK(writable.SetAlarm({2028, 3, 1, 9, 0, 0}, "saved", Clock(0, {2028, 3, 1, 8, 0, 0}, true))
              .status == maomi::ReminderStatus::kAccepted);
    backend.SeedInt32(maomi::kSchemaVersionKey, maomi::kStorageSchemaVersion + 1);
    maomi::StateStorage read_only_storage(backend);
    read_only_storage.Load(0);
    maomi::ReminderEngine read_only(read_only_storage);
    CHECK(read_only.Restore(Clock(0, {2028, 3, 1, 8, 1, 0}, true)).status ==
          maomi::RestoreStatus::kUnavailable);
}

void TestHeaderCorruptionRecoversSafely() {
    FakeStorageBackend backend;
    maomi::StateStorage first_storage(backend);
    first_storage.Load(0);
    maomi::ReminderEngine first(first_storage);
    first.Restore(Clock(0));
    CHECK(first.SetAlarm({2028, 3, 1, 9, 0, 0}, "saved", Clock(0, {2028, 3, 1, 8, 0, 0}, true))
              .status == maomi::ReminderStatus::kAccepted);
    auto blob = backend.ReminderBlob();
    CHECK(blob.size() > 7);
    blob[6] ^= 0x20;
    backend.SeedBlob(std::move(blob));

    maomi::StateStorage recovered_storage(backend);
    recovered_storage.Load(0);
    maomi::ReminderEngine recovered(recovered_storage);
    CHECK(recovered.Restore(Clock(0, {2028, 3, 1, 8, 1, 0}, true)).status ==
          maomi::RestoreStatus::kRecovered);
    CHECK(recovered.ActiveCount() == 0);
}

void TestExtremeWallTimeRecoversSafely() {
    FakeStorageBackend backend;
    maomi::StateStorage first_storage(backend);
    first_storage.Load(0);
    maomi::ReminderEngine first(first_storage);
    first.Restore(Clock(0));
    CHECK(first.SetAlarm({2028, 3, 1, 9, 0, 0}, "saved", Clock(0, {2028, 3, 1, 8, 0, 0}, true))
              .status == maomi::ReminderStatus::kAccepted);
    auto blob = backend.ReminderBlob();
    CHECK(blob.size() >= 24);
    const uint64_t extreme = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    for (size_t index = 0; index < 8; ++index) {
        blob[16 + index] = static_cast<uint8_t>(extreme >> (index * 8));
    }
    PatchReminderChecksum(&blob);
    backend.SeedBlob(std::move(blob));

    maomi::StateStorage recovered_storage(backend);
    recovered_storage.Load(0);
    maomi::ReminderEngine recovered(recovered_storage);
    CHECK(recovered.Restore(Clock(0, {2028, 3, 1, 8, 1, 0}, true)).status ==
          maomi::RestoreStatus::kRecovered);
    CHECK(recovered.ActiveCount() == 0);
}

void TestNonCanonicalPersistentFieldsRecoverSafely() {
    FakeStorageBackend source_backend;
    maomi::StateStorage source_storage(source_backend);
    source_storage.Load(0);
    maomi::ReminderEngine source(source_storage);
    source.Restore(Clock(0));
    CHECK(source
              .StartInterval(maomi::ReminderKind::kWater, 10, "water",
                             Clock(0, {2028, 3, 1, 8, 0, 0}, true))
              .status == maomi::ReminderStatus::kAccepted);
    const auto valid_blob = source_backend.ReminderBlob();
    CHECK(valid_blob.size() > 32);

    auto fractional_minute = valid_blob;
    PatchUInt32(&fractional_minute, 24, 601);
    PatchReminderChecksum(&fractional_minute);
    FakeStorageBackend fractional_backend;
    fractional_backend.SeedBlob(std::move(fractional_minute));
    maomi::StateStorage fractional_storage(fractional_backend);
    fractional_storage.Load(0);
    maomi::ReminderEngine fractional(fractional_storage);
    CHECK(fractional.Restore(Clock(0, {2028, 3, 1, 8, 1, 0}, true)).status ==
          maomi::RestoreStatus::kRecovered);

    auto embedded_nul = valid_blob;
    embedded_nul[28] = 0;
    PatchReminderChecksum(&embedded_nul);
    FakeStorageBackend nul_backend;
    nul_backend.SeedBlob(std::move(embedded_nul));
    maomi::StateStorage nul_storage(nul_backend);
    nul_storage.Load(0);
    maomi::ReminderEngine nul(nul_storage);
    CHECK(nul.Restore(Clock(0, {2028, 3, 1, 8, 1, 0}, true)).status ==
          maomi::RestoreStatus::kRecovered);
}

void TestNightIntervalDoesNotAccumulateAtEight() {
    Fixture fixture;
    const auto sedentary = fixture.engine.StartInterval(
        maomi::ReminderKind::kSedentary, 10, "stand", Clock(0, {2028, 3, 1, 7, 40, 0}, true));
    CHECK(sedentary.status == maomi::ReminderStatus::kAccepted);
    CHECK(fixture.engine.Update(Tick(1000, {2028, 3, 1, 8, 0, 1}, true)).state ==
          maomi::ReminderEventState::kNone);
    const auto* item = fixture.engine.Find(sedentary.id);
    CHECK(item != nullptr);
    CHECK(item->next_wall_time_seconds >
          maomi::ReminderEngine::CivilSeconds({2028, 3, 1, 8, 0, 1}));
}

}  // namespace

int main() {
    TestCountdownBoundsAndMonotonicClock();
    TestAlarmDateValidationAndTrustedTime();
    TestPomodoroAlternatesAndStopsAfterCycles();
    TestRestartRestoresOnlyPersistentPlans();
    TestRestartCatchUpAndMissedAlarmRules();
    TestQuietHoursBusyExpiryAndLowBattery();
    TestNightIntervalDoesNotAccumulateAtEight();
    TestAlarmWaitsForDurableRemovalBeforeTriggering();
    TestFailedAlarmRemovalDoesNotEmitOrRepeat();
    TestAlarmPersistenceRetryDoesNotBecomeBusyExpiry();
    TestRestoredAlarmCannotDeferPastRestartWindow();
    TestPendingIntervalDoesNotRepeatAfterRestart();
    TestIntervalsUseActualTriggerTimeForQuietHours();
    TestReadOnlyStorageIsUnavailable();
    TestHeaderCorruptionRecoversSafely();
    TestExtremeWallTimeRecoversSafely();
    TestNonCanonicalPersistentFieldsRecoverSafely();
    std::cout << "maomi reminder clock tests passed" << std::endl;
    return 0;
}
