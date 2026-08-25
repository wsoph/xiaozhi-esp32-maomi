#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#ifdef ESP_PLATFORM
#include <nvs.h>
#endif

namespace maomi {

inline constexpr char kStorageNamespace[] = "maomi";
inline constexpr char kSchemaVersionKey[] = "schema_ver";
inline constexpr char kFirstDayKey[] = "first_day";
inline constexpr char kBondPointsKey[] = "bond";
inline constexpr char kMaxCompanionDaysKey[] = "max_days";
inline constexpr char kManualQuietKey[] = "quiet";
inline constexpr char kRemindersKey[] = "reminders";

constexpr int32_t kStorageSchemaVersion = 1;
constexpr int32_t kMaximumBondPoints = 9999;
constexpr uint32_t kMaximumCompanionDays = 3000000;
constexpr size_t kMaxReminderDataSize = 1024;
constexpr uint64_t kNormalCommitDelayMs = 5 * 60 * 1000;
constexpr uint64_t kImportantCommitIntervalMs = 5 * 1000;

enum class BackendReadResult : uint8_t {
    kOk,
    kNotFound,
    kInvalidData,
    kIoError,
};

class StorageBackend {
public:
    virtual ~StorageBackend() = default;
    virtual BackendReadResult ReadInt32(const char* key, int32_t* value) = 0;
    virtual BackendReadResult ReadUInt8(const char* key, uint8_t* value) = 0;
    virtual BackendReadResult ReadBlob(const char* key, uint8_t* value, size_t capacity,
                                       size_t* size) = 0;
    virtual bool WriteInt32(const char* key, int32_t value) = 0;
    virtual bool WriteUInt8(const char* key, uint8_t value) = 0;
    virtual bool WriteBlob(const char* key, const uint8_t* value, size_t size) = 0;
    virtual bool Commit() = 0;
};

struct PersistentState {
    int32_t first_day = 0;
    int32_t bond_points = 0;
    uint32_t max_companion_days = 0;
    bool manual_quiet = false;
    std::array<uint8_t, kMaxReminderDataSize> reminder_data{};
    size_t reminder_data_size = 0;

    bool operator==(const PersistentState& other) const {
        if (reminder_data_size > reminder_data.size() ||
            other.reminder_data_size > other.reminder_data.size()) {
            return false;
        }
        return first_day == other.first_day && bond_points == other.bond_points &&
               max_companion_days == other.max_companion_days &&
               manual_quiet == other.manual_quiet &&
               reminder_data_size == other.reminder_data_size &&
               std::equal(reminder_data.begin(), reminder_data.begin() + reminder_data_size,
                          other.reminder_data.begin());
    }
};

enum class LoadHealth : uint8_t {
    kHealthy,
    kMigrated,
    kRecovered,
    kFutureSchema,
};

enum class WriteImportance : uint8_t {
    kNormal,
    kImportant,
};

enum class SaveResult : uint8_t {
    kNoChanges,
    kDeferred,
    kRateLimited,
    kCommitted,
    kFailed,
    kInvalidState,
    kWriteBlocked,
};

enum class StorageLogEvent : uint8_t {
    kInvalidFieldRecovered,
    kReadFailureRecovered,
    kFutureSchemaReadOnly,
    kWriteFailure,
};

struct LoadResult {
    PersistentState state;
    LoadHealth health = LoadHealth::kHealthy;
    bool write_allowed = true;
};

// Main-task-owned persistence policy. Update only changes memory until its bounded commit policy
// allows a write; callbacks from other tasks must schedule storage work onto the main task.
class StateStorage {
public:
    using Logger = std::function<void(StorageLogEvent event, const char* context)>;

    explicit StateStorage(StorageBackend& backend, Logger logger = {});
    StateStorage(const StateStorage&) = delete;
    StateStorage& operator=(const StateStorage&) = delete;

    LoadResult Load(uint64_t monotonic_ms);
    SaveResult Update(const PersistentState& state, WriteImportance importance,
                      uint64_t monotonic_ms);
    SaveResult FlushIfDue(uint64_t monotonic_ms);
    const PersistentState& GetState() const;
    bool NeedsWrite() const;

private:
    static constexpr uint32_t kDirtySchema = 1U << 0;
    static constexpr uint32_t kDirtyFirstDay = 1U << 1;
    static constexpr uint32_t kDirtyBondPoints = 1U << 2;
    static constexpr uint32_t kDirtyMaxCompanionDays = 1U << 3;
    static constexpr uint32_t kDirtyManualQuiet = 1U << 4;
    static constexpr uint32_t kDirtyReminders = 1U << 5;

    StorageBackend& backend_;
    Logger logger_;
    PersistentState state_;
    uint32_t dirty_mask_ = 0;
    uint64_t dirty_since_ms_ = 0;
    uint64_t last_write_attempt_ms_ = 0;
    bool loaded_ = false;
    bool write_allowed_ = true;
    bool important_pending_ = false;
    bool has_write_attempt_ = false;

    static bool IsValidState(const PersistentState& state);
    static bool IsValidFirstDay(int32_t first_day);
    static bool HasElapsed(uint64_t now_ms, uint64_t then_ms, uint64_t interval_ms);
    void MarkRecovered(LoadHealth* health, uint32_t dirty_bit, StorageLogEvent event,
                       const char* key);
    bool PersistDirty();
};

#ifdef ESP_PLATFORM
class NvsStorageBackend final : public StorageBackend {
public:
    NvsStorageBackend();
    ~NvsStorageBackend() override;
    NvsStorageBackend(const NvsStorageBackend&) = delete;
    NvsStorageBackend& operator=(const NvsStorageBackend&) = delete;

    BackendReadResult ReadInt32(const char* key, int32_t* value) override;
    BackendReadResult ReadUInt8(const char* key, uint8_t* value) override;
    BackendReadResult ReadBlob(const char* key, uint8_t* value, size_t capacity,
                               size_t* size) override;
    bool WriteInt32(const char* key, int32_t value) override;
    bool WriteUInt8(const char* key, uint8_t value) override;
    bool WriteBlob(const char* key, const uint8_t* value, size_t size) override;
    bool Commit() override;

private:
    nvs_handle_t handle_ = 0;
};
#endif

}  // namespace maomi
