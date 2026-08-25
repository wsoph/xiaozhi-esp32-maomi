#include "maomi_storage.h"

#include "maomi_clock.h"

#include <utility>

#ifdef ESP_PLATFORM
#include <esp_err.h>
#endif

namespace maomi {
namespace {

bool ReminderDataEqual(const PersistentState& left, const PersistentState& right) {
    return left.reminder_data_size == right.reminder_data_size &&
           std::equal(left.reminder_data.begin(),
                      left.reminder_data.begin() + left.reminder_data_size,
                      right.reminder_data.begin());
}

}  // namespace

StateStorage::StateStorage(StorageBackend& backend, Logger logger)
    : backend_(backend), logger_(std::move(logger)) {}

LoadResult StateStorage::Load(uint64_t monotonic_ms) {
    state_ = {};
    dirty_mask_ = 0;
    dirty_since_ms_ = monotonic_ms;
    last_write_attempt_ms_ = 0;
    important_pending_ = false;
    has_write_attempt_ = false;
    loaded_ = true;
    write_allowed_ = true;
    LoadHealth health = LoadHealth::kHealthy;

    int32_t schema_version = 0;
    const auto schema_result = backend_.ReadInt32(kSchemaVersionKey, &schema_version);
    if (schema_result == BackendReadResult::kOk) {
        if (schema_version == kStorageSchemaVersion) {
            // Current schema, no migration needed.
        } else if (schema_version == 0) {
            health = LoadHealth::kMigrated;
            dirty_mask_ |= kDirtySchema;
        } else if (schema_version > kStorageSchemaVersion) {
            health = LoadHealth::kFutureSchema;
            write_allowed_ = false;
            if (logger_) {
                logger_(StorageLogEvent::kFutureSchemaReadOnly, kSchemaVersionKey);
            }
        } else {
            health = LoadHealth::kRecovered;
            dirty_mask_ |= kDirtySchema;
            if (logger_) {
                logger_(StorageLogEvent::kInvalidFieldRecovered, kSchemaVersionKey);
            }
        }
    } else if (schema_result == BackendReadResult::kNotFound) {
        health = LoadHealth::kMigrated;
        dirty_mask_ |= kDirtySchema;
    } else {
        health = LoadHealth::kRecovered;
        dirty_mask_ |= kDirtySchema;
        if (logger_) {
            logger_(schema_result == BackendReadResult::kIoError
                        ? StorageLogEvent::kReadFailureRecovered
                        : StorageLogEvent::kInvalidFieldRecovered,
                    kSchemaVersionKey);
        }
    }

    int32_t int_value = 0;
    auto read_result = backend_.ReadInt32(kFirstDayKey, &int_value);
    if (read_result == BackendReadResult::kOk && IsValidFirstDay(int_value)) {
        state_.first_day = int_value;
    } else if (read_result != BackendReadResult::kNotFound) {
        MarkRecovered(&health, kDirtyFirstDay,
                      read_result == BackendReadResult::kIoError
                          ? StorageLogEvent::kReadFailureRecovered
                          : StorageLogEvent::kInvalidFieldRecovered,
                      kFirstDayKey);
    }

    int_value = 0;
    read_result = backend_.ReadInt32(kBondPointsKey, &int_value);
    if (read_result == BackendReadResult::kOk && int_value >= 0 &&
        int_value <= kMaximumBondPoints) {
        state_.bond_points = int_value;
    } else if (read_result != BackendReadResult::kNotFound) {
        MarkRecovered(&health, kDirtyBondPoints,
                      read_result == BackendReadResult::kIoError
                          ? StorageLogEvent::kReadFailureRecovered
                          : StorageLogEvent::kInvalidFieldRecovered,
                      kBondPointsKey);
    }

    int_value = 0;
    read_result = backend_.ReadInt32(kMaxCompanionDaysKey, &int_value);
    if (read_result == BackendReadResult::kOk && int_value >= 0 &&
        static_cast<uint32_t>(int_value) <= kMaximumCompanionDays) {
        state_.max_companion_days = static_cast<uint32_t>(int_value);
    } else if (read_result != BackendReadResult::kNotFound) {
        MarkRecovered(&health, kDirtyMaxCompanionDays,
                      read_result == BackendReadResult::kIoError
                          ? StorageLogEvent::kReadFailureRecovered
                          : StorageLogEvent::kInvalidFieldRecovered,
                      kMaxCompanionDaysKey);
    }

    uint8_t bool_value = 0;
    read_result = backend_.ReadUInt8(kManualQuietKey, &bool_value);
    if (read_result == BackendReadResult::kOk && bool_value <= 1) {
        state_.manual_quiet = bool_value != 0;
    } else if (read_result != BackendReadResult::kNotFound) {
        MarkRecovered(&health, kDirtyManualQuiet,
                      read_result == BackendReadResult::kIoError
                          ? StorageLogEvent::kReadFailureRecovered
                          : StorageLogEvent::kInvalidFieldRecovered,
                      kManualQuietKey);
    }

    size_t reminder_size = 0;
    read_result = backend_.ReadBlob(kRemindersKey, state_.reminder_data.data(),
                                    state_.reminder_data.size(), &reminder_size);
    if (read_result == BackendReadResult::kOk && reminder_size <= kMaxReminderDataSize) {
        state_.reminder_data_size = reminder_size;
    } else if (read_result != BackendReadResult::kNotFound) {
        state_.reminder_data.fill(0);
        state_.reminder_data_size = 0;
        MarkRecovered(&health, kDirtyReminders,
                      read_result == BackendReadResult::kIoError
                          ? StorageLogEvent::kReadFailureRecovered
                          : StorageLogEvent::kInvalidFieldRecovered,
                      kRemindersKey);
    }

    if (!write_allowed_) {
        dirty_mask_ = 0;
    }
    return {state_, health, write_allowed_};
}

SaveResult StateStorage::Update(const PersistentState& state, WriteImportance importance,
                                uint64_t monotonic_ms) {
    if (!loaded_) {
        return SaveResult::kFailed;
    }
    if (!write_allowed_) {
        return SaveResult::kWriteBlocked;
    }
    if (!IsValidState(state)) {
        return SaveResult::kInvalidState;
    }

    uint32_t changed = 0;
    if (state.first_day != state_.first_day) {
        changed |= kDirtyFirstDay;
    }
    if (state.bond_points != state_.bond_points) {
        changed |= kDirtyBondPoints;
    }
    if (state.max_companion_days != state_.max_companion_days) {
        changed |= kDirtyMaxCompanionDays;
    }
    if (state.manual_quiet != state_.manual_quiet) {
        changed |= kDirtyManualQuiet;
    }
    if (!ReminderDataEqual(state, state_)) {
        changed |= kDirtyReminders;
    }
    if (changed == 0) {
        return SaveResult::kNoChanges;
    }

    if (dirty_mask_ == 0) {
        dirty_since_ms_ = monotonic_ms;
    }
    dirty_mask_ |= changed;
    state_ = state;
    if (importance == WriteImportance::kImportant) {
        important_pending_ = true;
    }
    return FlushIfDue(monotonic_ms);
}

SaveResult StateStorage::FlushIfDue(uint64_t monotonic_ms) {
    if (!loaded_) {
        return SaveResult::kFailed;
    }
    if (!write_allowed_) {
        return SaveResult::kWriteBlocked;
    }
    if (dirty_mask_ == 0) {
        return SaveResult::kNoChanges;
    }

    if (!important_pending_ && !HasElapsed(monotonic_ms, dirty_since_ms_, kNormalCommitDelayMs)) {
        return SaveResult::kDeferred;
    }
    if (has_write_attempt_ &&
        !HasElapsed(monotonic_ms, last_write_attempt_ms_, kImportantCommitIntervalMs)) {
        return important_pending_ ? SaveResult::kRateLimited : SaveResult::kDeferred;
    }

    has_write_attempt_ = true;
    last_write_attempt_ms_ = monotonic_ms;
    if (!PersistDirty()) {
        if (logger_) {
            logger_(StorageLogEvent::kWriteFailure, kStorageNamespace);
        }
        return SaveResult::kFailed;
    }
    dirty_mask_ = 0;
    important_pending_ = false;
    return SaveResult::kCommitted;
}

const PersistentState& StateStorage::GetState() const { return state_; }

bool StateStorage::NeedsWrite() const { return dirty_mask_ != 0; }

bool StateStorage::IsValidState(const PersistentState& state) {
    return IsValidFirstDay(state.first_day) && state.bond_points >= 0 &&
           state.bond_points <= kMaximumBondPoints &&
           state.max_companion_days <= kMaximumCompanionDays &&
           state.reminder_data_size <= kMaxReminderDataSize;
}

bool StateStorage::IsValidFirstDay(int32_t first_day) {
    if (first_day == 0) {
        return true;
    }
    DateTime decoded;
    return ReliableClock::DecodeDate(first_day, &decoded) &&
           decoded.year >= ReliableClock::kMinimumTrustedYear;
}

bool StateStorage::HasElapsed(uint64_t now_ms, uint64_t then_ms, uint64_t interval_ms) {
    return now_ms >= then_ms && now_ms - then_ms >= interval_ms;
}

void StateStorage::MarkRecovered(LoadHealth* health, uint32_t dirty_bit, StorageLogEvent event,
                                 const char* key) {
    if (*health != LoadHealth::kFutureSchema) {
        *health = LoadHealth::kRecovered;
    }
    if (write_allowed_) {
        dirty_mask_ |= dirty_bit;
    }
    if (logger_) {
        logger_(event, key);
    }
}

bool StateStorage::PersistDirty() {
    if ((dirty_mask_ & kDirtyFirstDay) != 0 &&
        !backend_.WriteInt32(kFirstDayKey, state_.first_day)) {
        return false;
    }
    if ((dirty_mask_ & kDirtyBondPoints) != 0 &&
        !backend_.WriteInt32(kBondPointsKey, state_.bond_points)) {
        return false;
    }
    if ((dirty_mask_ & kDirtyMaxCompanionDays) != 0 &&
        !backend_.WriteInt32(kMaxCompanionDaysKey,
                             static_cast<int32_t>(state_.max_companion_days))) {
        return false;
    }
    if ((dirty_mask_ & kDirtyManualQuiet) != 0 &&
        !backend_.WriteUInt8(kManualQuietKey, state_.manual_quiet ? 1 : 0)) {
        return false;
    }
    if ((dirty_mask_ & kDirtyReminders) != 0 &&
        !backend_.WriteBlob(kRemindersKey, state_.reminder_data.data(),
                            state_.reminder_data_size)) {
        return false;
    }
    if ((dirty_mask_ & kDirtySchema) != 0 &&
        !backend_.WriteInt32(kSchemaVersionKey, kStorageSchemaVersion)) {
        return false;
    }
    // NVS set operations only stage changes; one commit makes this bounded batch durable.
    // Source:
    // https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-reference/storage/nvs_flash.html#_CPPv410nvs_commit12nvs_handle_t
    return backend_.Commit();
}

#ifdef ESP_PLATFORM
namespace {

BackendReadResult MapNvsReadResult(esp_err_t result) {
    if (result == ESP_OK) {
        return BackendReadResult::kOk;
    }
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return BackendReadResult::kNotFound;
    }
    if (result == ESP_ERR_NVS_TYPE_MISMATCH || result == ESP_ERR_NVS_INVALID_LENGTH) {
        return BackendReadResult::kInvalidData;
    }
    return BackendReadResult::kIoError;
}

}  // namespace

NvsStorageBackend::NvsStorageBackend() {
    if (nvs_open(kStorageNamespace, NVS_READWRITE, &handle_) != ESP_OK) {
        handle_ = 0;
    }
}

NvsStorageBackend::~NvsStorageBackend() {
    if (handle_ != 0) {
        nvs_close(handle_);
    }
}

BackendReadResult NvsStorageBackend::ReadInt32(const char* key, int32_t* value) {
    return handle_ == 0 ? BackendReadResult::kIoError
                        : MapNvsReadResult(nvs_get_i32(handle_, key, value));
}

BackendReadResult NvsStorageBackend::ReadUInt8(const char* key, uint8_t* value) {
    return handle_ == 0 ? BackendReadResult::kIoError
                        : MapNvsReadResult(nvs_get_u8(handle_, key, value));
}

BackendReadResult NvsStorageBackend::ReadBlob(const char* key, uint8_t* value, size_t capacity,
                                              size_t* size) {
    if (handle_ == 0 || size == nullptr) {
        return BackendReadResult::kIoError;
    }
    size_t required = 0;
    auto result = nvs_get_blob(handle_, key, nullptr, &required);
    if (result != ESP_OK) {
        return MapNvsReadResult(result);
    }
    if (required > capacity) {
        return BackendReadResult::kInvalidData;
    }
    if (required == 0) {
        *size = 0;
        return BackendReadResult::kOk;
    }
    size_t available = capacity;
    result = nvs_get_blob(handle_, key, value, &available);
    if (result == ESP_OK) {
        *size = available;
    }
    return MapNvsReadResult(result);
}

bool NvsStorageBackend::WriteInt32(const char* key, int32_t value) {
    return handle_ != 0 && nvs_set_i32(handle_, key, value) == ESP_OK;
}

bool NvsStorageBackend::WriteUInt8(const char* key, uint8_t value) {
    return handle_ != 0 && nvs_set_u8(handle_, key, value) == ESP_OK;
}

bool NvsStorageBackend::WriteBlob(const char* key, const uint8_t* value, size_t size) {
    if (handle_ == 0) {
        return false;
    }
    if (size == 0) {
        const auto result = nvs_erase_key(handle_, key);
        return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
    }
    return value != nullptr && nvs_set_blob(handle_, key, value, size) == ESP_OK;
}

bool NvsStorageBackend::Commit() { return handle_ != 0 && nvs_commit(handle_) == ESP_OK; }
#endif

}  // namespace maomi
