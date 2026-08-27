#include "maomi_reminders.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
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

class CountingBackend : public maomi::StorageBackend {
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
        ++blob_writes;
        pending_[key] = std::vector<uint8_t>(value, value + size);
        return true;
    }
    bool Commit() override {
        ++commits;
        for (auto& [key, value] : pending_) {
            committed_[key] = std::move(value);
        }
        pending_.clear();
        return true;
    }
    void SeedBlob(std::vector<uint8_t> blob) { committed_[maomi::kRemindersKey] = std::move(blob); }

    int commits = 0;
    int blob_writes = 0;

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

maomi::ClockSnapshot Clock(uint64_t monotonic_ms) {
    return {.valid = true, .local_time = {2028, 3, 1, 9, 0, 0}, .monotonic_ms = monotonic_ms};
}

void TestCapacityAndPomodoroExclusivity() {
    CountingBackend backend;
    maomi::StateStorage storage(backend);
    storage.Load(0);
    maomi::ReminderEngine engine(storage);
    engine.Restore(Clock(0));

    for (size_t index = 0; index < maomi::kMaxPersistentReminders; ++index) {
        CHECK(engine.StartInterval(maomi::ReminderKind::kWater, 10 + index, "water", Clock(0))
                  .status == maomi::ReminderStatus::kAccepted);
    }
    CHECK(engine.SetAlarm({2028, 3, 2, 9, 0, 0}, "full", Clock(0)).status ==
          maomi::ReminderStatus::kCapacityReached);

    for (size_t index = 0; index < maomi::kMaxTemporaryReminders; ++index) {
        CHECK(engine.StartCountdown(60 + index, "timer", Clock(0)).status ==
              maomi::ReminderStatus::kAccepted);
    }
    CHECK(engine.StartCountdown(120, "full", Clock(0)).status ==
          maomi::ReminderStatus::kCapacityReached);

    const auto first_id = engine.List(Clock(0)).items[maomi::kMaxPersistentReminders].id;
    CHECK(engine.Cancel(first_id, Clock(0)).status == maomi::ReminderStatus::kCancelled);
    CHECK(engine.StartPomodoro(25, 5, 2, Clock(0)).status == maomi::ReminderStatus::kAccepted);
    CHECK(engine.StartPomodoro(25, 5, 2, Clock(0)).status ==
          maomi::ReminderStatus::kPomodoroActive);
}

void TestThousandCreateCancelCyclesStayBounded() {
    CountingBackend backend;
    maomi::StateStorage storage(backend);
    storage.Load(0);
    maomi::ReminderEngine engine(storage);
    engine.Restore(Clock(0));

    for (uint64_t index = 0; index < 1000; ++index) {
        const auto created =
            engine.StartInterval(maomi::ReminderKind::kSedentary, 10, "stand", Clock(index));
        CHECK(created.status == maomi::ReminderStatus::kAccepted);
        CHECK(engine.PersistentCount() == 1);
        CHECK(storage.GetState().reminder_data_size <= maomi::kMaxReminderDataSize);
        CHECK(engine.Cancel(created.id, Clock(index)).status == maomi::ReminderStatus::kCancelled);
        CHECK(engine.PersistentCount() == 0);
        CHECK(engine.ActiveCount() == 0);
    }
    CHECK(backend.commits <= 2);
    CHECK(backend.blob_writes <= 2);
    CHECK(storage.GetState().reminder_data_size == 0);
    CHECK(storage.FlushIfDue(1000 + maomi::kImportantCommitIntervalMs) ==
          maomi::SaveResult::kCommitted);

    maomi::StateStorage restarted_storage(backend);
    restarted_storage.Load(0);
    maomi::ReminderEngine restarted(restarted_storage);
    CHECK(restarted.Restore(Clock(0)).status == maomi::RestoreStatus::kEmpty);
    CHECK(restarted.ActiveCount() == 0);
}

void TestIdsAreUniqueAndLabelsAreBounded() {
    CountingBackend backend;
    maomi::StateStorage storage(backend);
    storage.Load(0);
    maomi::ReminderEngine engine(storage);
    engine.Restore(Clock(0));

    uint16_t ids[maomi::kMaxTemporaryReminders] = {};
    for (size_t index = 0; index < maomi::kMaxTemporaryReminders; ++index) {
        const auto result = engine.StartCountdown(60, "ok", Clock(0));
        CHECK(result.status == maomi::ReminderStatus::kAccepted);
        ids[index] = result.id;
        for (size_t previous = 0; previous < index; ++previous) {
            CHECK(ids[previous] != ids[index]);
        }
    }
    CHECK(engine
              .StartInterval(maomi::ReminderKind::kWater, 10,
                             std::string(maomi::kMaxReminderLabelBytes + 1, 'x'), Clock(0))
              .status == maomi::ReminderStatus::kInvalidArgument);
    CHECK(engine.Cancel(0, Clock(0)).status == maomi::ReminderStatus::kNotFound);
    CHECK(engine.Cancel(65535, Clock(0)).status == maomi::ReminderStatus::kNotFound);
}

void TestCorruptPersistentBlobRecoversSafely() {
    CountingBackend backend;
    backend.SeedBlob({0x4d, 0x52, 0x4d, 0x31, 0xff, 0xff, 0xff});
    maomi::StateStorage storage(backend);
    storage.Load(0);
    maomi::ReminderEngine engine(storage);
    const auto restored = engine.Restore(Clock(0));
    CHECK(restored.status == maomi::RestoreStatus::kRecovered);
    CHECK(engine.ActiveCount() == 0);
    CHECK(storage.GetState().reminder_data_size == 0);
}

}  // namespace

int main() {
    static_assert(maomi::kMaxPersistentReminders + maomi::kMaxTemporaryReminders <= 16);
    TestCapacityAndPomodoroExclusivity();
    TestThousandCreateCancelCyclesStayBounded();
    TestIdsAreUniqueAndLabelsAreBounded();
    TestCorruptPersistentBlobRecoversSafely();
    std::cout << "maomi reminder stress tests passed" << std::endl;
    return 0;
}
