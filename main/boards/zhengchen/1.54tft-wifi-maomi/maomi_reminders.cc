#include "maomi_reminders.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace maomi {
namespace {

constexpr uint32_t kBlobMagic = 0x314d524d;  // "MRM1" in little-endian byte order.
constexpr uint8_t kBlobVersion = 1;
constexpr size_t kBlobHeaderSize = 12;

class BlobWriter {
public:
    explicit BlobWriter(PersistentState* state) : state_(state) {}

    bool UInt8(uint8_t value) { return Bytes(&value, sizeof(value)); }
    bool UInt16(uint16_t value) {
        const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
        return Bytes(bytes, sizeof(bytes));
    }
    bool UInt32(uint32_t value) {
        uint8_t bytes[4] = {};
        for (size_t index = 0; index < sizeof(bytes); ++index) {
            bytes[index] = static_cast<uint8_t>(value >> (index * 8));
        }
        return Bytes(bytes, sizeof(bytes));
    }
    bool Int64(int64_t value) {
        uint8_t bytes[8] = {};
        const uint64_t encoded = static_cast<uint64_t>(value);
        for (size_t index = 0; index < sizeof(bytes); ++index) {
            bytes[index] = static_cast<uint8_t>(encoded >> (index * 8));
        }
        return Bytes(bytes, sizeof(bytes));
    }
    bool Bytes(const void* value, size_t size) {
        if (value == nullptr || state_ == nullptr || offset_ > state_->reminder_data.size() ||
            size > state_->reminder_data.size() - offset_) {
            return false;
        }
        std::memcpy(state_->reminder_data.data() + offset_, value, size);
        offset_ += size;
        return true;
    }
    bool PatchUInt32(size_t offset, uint32_t value) {
        if (state_ == nullptr || offset > offset_ || sizeof(value) > offset_ - offset) {
            return false;
        }
        for (size_t index = 0; index < sizeof(value); ++index) {
            state_->reminder_data[offset + index] = static_cast<uint8_t>(value >> (index * 8));
        }
        return true;
    }
    size_t size() const { return offset_; }

private:
    PersistentState* state_ = nullptr;
    size_t offset_ = 0;
};

class BlobReader {
public:
    explicit BlobReader(const PersistentState& state) : state_(state) {}

    bool UInt8(uint8_t* value) { return Bytes(value, sizeof(*value)); }
    bool UInt16(uint16_t* value) {
        uint8_t bytes[2] = {};
        if (!Bytes(bytes, sizeof(bytes))) {
            return false;
        }
        *value = static_cast<uint16_t>(bytes[0]) |
                 static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8);
        return true;
    }
    bool UInt32(uint32_t* value) {
        uint8_t bytes[4] = {};
        if (!Bytes(bytes, sizeof(bytes))) {
            return false;
        }
        *value = 0;
        for (size_t index = 0; index < sizeof(bytes); ++index) {
            *value |= static_cast<uint32_t>(bytes[index]) << (index * 8);
        }
        return true;
    }
    bool Int64(int64_t* value) {
        uint8_t bytes[8] = {};
        if (!Bytes(bytes, sizeof(bytes))) {
            return false;
        }
        uint64_t encoded = 0;
        for (size_t index = 0; index < sizeof(bytes); ++index) {
            encoded |= static_cast<uint64_t>(bytes[index]) << (index * 8);
        }
        *value = static_cast<int64_t>(encoded);
        return true;
    }
    bool Bytes(void* value, size_t size) {
        if (value == nullptr || offset_ > state_.reminder_data_size ||
            size > state_.reminder_data_size - offset_) {
            return false;
        }
        std::memcpy(value, state_.reminder_data.data() + offset_, size);
        offset_ += size;
        return true;
    }
    size_t offset() const { return offset_; }
    bool AtEnd() const { return offset_ == state_.reminder_data_size; }

private:
    const PersistentState& state_;
    size_t offset_ = 0;
};

uint32_t UpdateBlobChecksum(uint32_t checksum, const uint8_t* data, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        checksum ^= data[index];
        checksum *= 16777619U;
    }
    return checksum;
}

uint32_t BlobChecksum(const uint8_t* data, size_t size) {
    if (data == nullptr || size < kBlobHeaderSize) {
        return 0;
    }
    uint32_t checksum = UpdateBlobChecksum(2166136261U, data, 8);
    return UpdateBlobChecksum(checksum, data + kBlobHeaderSize, size - kBlobHeaderSize);
}

}  // namespace

ReminderEngine::ReminderEngine(StateStorage& storage) : storage_(storage) {}

ReminderPresentationDecision DecideReminderPresentation(const ReminderEvent& event,
                                                        bool critical_alert) {
    switch (event.state) {
        case ReminderEventState::kTriggered:
            if (event.id == 0) {
                return {};
            }
            return {
                .show_animation = true,
                .play_sound = event.audible && !critical_alert,
            };
        case ReminderEventState::kMissed:
            return {.show_animation = event.id != 0};
        case ReminderEventState::kNone:
            return {};
    }
    return {};
}

CountdownPresentation SelectCountdownPresentation(const ReminderList& reminders) {
    const ReminderSnapshot* selected = nullptr;
    const size_t count = std::min(reminders.count, reminders.items.size());
    for (size_t index = 0; index < count; ++index) {
        const auto& item = reminders.items[index];
        if (item.kind != ReminderKind::kCountdown ||
            (selected != nullptr &&
             (item.remaining_ms > selected->remaining_ms ||
              (item.remaining_ms == selected->remaining_ms && item.id >= selected->id)))) {
            continue;
        }
        selected = &item;
    }
    if (selected == nullptr) {
        return {};
    }

    const uint64_t seconds = selected->remaining_ms / 1000 +
                             (selected->remaining_ms % 1000 == 0 ? 0 : 1);
    return {
        .visible = true,
        .id = selected->id,
        .remaining_seconds = static_cast<uint32_t>(std::min<uint64_t>(
            seconds, std::numeric_limits<uint32_t>::max())),
    };
}

ReminderResult ReminderEngine::StartCountdown(uint32_t duration_seconds, std::string_view label,
                                              const ClockSnapshot& clock) {
    if (duration_seconds < 1 || duration_seconds > 86400) {
        return {.status = ReminderStatus::kInvalidArgument};
    }
    auto result = Create(ReminderKind::kCountdown, false, label, clock);
    if (result.status != ReminderStatus::kAccepted) {
        return result;
    }
    auto* entry = FindEntry(result.id);
    entry->deadline_ms = AddMilliseconds(clock.monotonic_ms, duration_seconds * 1000ULL);
    return result;
}

ReminderResult ReminderEngine::SetAlarm(const DateTime& target, std::string_view label,
                                        const ClockSnapshot& clock) {
    if (!clock.valid) {
        return {.status = ReminderStatus::kInvalidArgument};
    }
    const int64_t target_seconds = CivilSeconds(target);
    const int64_t now_seconds = CivilSeconds(clock.local_time);
    if (target_seconds < 0 || now_seconds < 0 || target_seconds <= now_seconds) {
        return {.status = ReminderStatus::kInvalidArgument};
    }
    auto result = Create(ReminderKind::kAlarm, true, label, clock);
    if (result.status != ReminderStatus::kAccepted) {
        return result;
    }
    auto* entry = FindEntry(result.id);
    entry->snapshot.next_wall_time_seconds = target_seconds;
    result.save_result = Persist(clock);
    if (IsPersistenceFailure(result.save_result)) {
        *entry = {};
        result.status = ReminderStatus::kPersistenceUnavailable;
        result.id = 0;
    }
    return result;
}

ReminderResult ReminderEngine::StartInterval(ReminderKind kind, uint32_t interval_minutes,
                                             std::string_view label, const ClockSnapshot& clock) {
    if (!IsIntervalKind(kind) || interval_minutes < 10 || interval_minutes > 720) {
        return {.status = ReminderStatus::kInvalidArgument};
    }
    if (clock.valid && CivilSeconds(clock.local_time) < 0) {
        return {.status = ReminderStatus::kInvalidArgument};
    }
    auto result = Create(kind, true, label, clock);
    if (result.status != ReminderStatus::kAccepted) {
        return result;
    }
    auto* entry = FindEntry(result.id);
    entry->snapshot.interval_seconds = interval_minutes * 60;
    if (clock.valid) {
        entry->snapshot.next_wall_time_seconds =
            AddSeconds(CivilSeconds(clock.local_time), entry->snapshot.interval_seconds);
    }
    result.save_result = Persist(clock);
    if (IsPersistenceFailure(result.save_result)) {
        *entry = {};
        result.status = ReminderStatus::kPersistenceUnavailable;
        result.id = 0;
    }
    return result;
}

ReminderResult ReminderEngine::StartPomodoro(uint32_t work_minutes, uint32_t break_minutes,
                                             uint32_t cycles, const ClockSnapshot& clock) {
    if (work_minutes < 1 || work_minutes > 120 || break_minutes < 1 || break_minutes > 60 ||
        cycles < 1 || cycles > 12) {
        return {.status = ReminderStatus::kInvalidArgument};
    }
    if (HasPomodoro()) {
        return {.status = ReminderStatus::kPomodoroActive};
    }
    auto result = Create(ReminderKind::kPomodoro, false, {}, clock);
    if (result.status != ReminderStatus::kAccepted) {
        return result;
    }
    auto* entry = FindEntry(result.id);
    entry->snapshot.phase = ReminderPhase::kWork;
    entry->snapshot.total_cycles = static_cast<uint8_t>(cycles);
    entry->work_seconds = work_minutes * 60;
    entry->break_seconds = break_minutes * 60;
    entry->deadline_ms = AddMilliseconds(clock.monotonic_ms, entry->work_seconds * 1000ULL);
    return result;
}

ReminderResult ReminderEngine::Cancel(uint16_t id, const ClockSnapshot& clock) {
    auto* entry = FindEntry(id);
    if (entry == nullptr) {
        return {.status = ReminderStatus::kNotFound};
    }
    const Entry previous = *entry;
    const ReminderKind kind = entry->snapshot.kind;
    const bool persistent = entry->snapshot.persistent;
    *entry = {};
    SaveResult save_result = SaveResult::kNoChanges;
    if (persistent) {
        save_result = Persist(clock);
        if (IsPersistenceFailure(save_result)) {
            *entry = previous;
            return {.status = ReminderStatus::kPersistenceUnavailable,
                    .id = id,
                    .kind = kind,
                    .persistent = true,
                    .save_result = save_result};
        }
    }
    return {.status = ReminderStatus::kCancelled,
            .id = id,
            .kind = kind,
            .persistent = persistent,
            .save_result = save_result};
}

ReminderResult ReminderEngine::Create(ReminderKind kind, bool persistent, std::string_view label,
                                      const ClockSnapshot&) {
    const bool label_valid = ValidateLabel(label);
    if (!label_valid || (persistent && PersistentCount() >= kMaxPersistentReminders) ||
        (!persistent && ActiveCount() - PersistentCount() >= kMaxTemporaryReminders)) {
        return {.status = label_valid ? ReminderStatus::kCapacityReached
                                      : ReminderStatus::kInvalidArgument};
    }
    auto* entry = FindFreeEntry();
    if (entry == nullptr) {
        return {.status = ReminderStatus::kCapacityReached};
    }
    const uint16_t id = AllocateId();
    if (id == 0) {
        return {.status = ReminderStatus::kCapacityReached};
    }
    *entry = {};
    entry->active = true;
    entry->snapshot.id = id;
    entry->snapshot.kind = kind;
    entry->snapshot.persistent = persistent;
    std::copy(label.begin(), label.end(), entry->snapshot.label.begin());
    entry->snapshot.label[label.size()] = '\0';
    return {.status = ReminderStatus::kAccepted, .id = id, .kind = kind, .persistent = persistent};
}

ReminderList ReminderEngine::List(const ClockSnapshot& clock) const {
    ReminderList result;
    const int64_t now_seconds = clock.valid ? CivilSeconds(clock.local_time) : 0;
    for (const auto& entry : entries_) {
        if (!entry.active) {
            continue;
        }
        auto& item = result.items[result.count++];
        item = entry.snapshot;
        if (!item.persistent) {
            item.remaining_ms =
                entry.deadline_ms > clock.monotonic_ms ? entry.deadline_ms - clock.monotonic_ms : 0;
        } else if (clock.valid && item.next_wall_time_seconds > now_seconds) {
            item.remaining_ms =
                static_cast<uint64_t>(item.next_wall_time_seconds - now_seconds) * 1000;
        }
    }
    return result;
}

const ReminderSnapshot* ReminderEngine::Find(uint16_t id) const {
    const auto* entry = FindEntry(id);
    return entry == nullptr ? nullptr : &entry->snapshot;
}

size_t ReminderEngine::ActiveCount() const {
    return static_cast<size_t>(std::count_if(entries_.begin(), entries_.end(),
                                             [](const Entry& entry) { return entry.active; }));
}

size_t ReminderEngine::PersistentCount() const {
    return static_cast<size_t>(std::count_if(
        entries_.begin(), entries_.end(),
        [](const Entry& entry) { return entry.active && entry.snapshot.persistent; }));
}

uint16_t ReminderEngine::AllocateId() {
    for (uint32_t attempt = 0; attempt < std::numeric_limits<uint16_t>::max(); ++attempt) {
        const uint16_t candidate = next_id_ == 0 ? 1 : next_id_;
        next_id_ = candidate == std::numeric_limits<uint16_t>::max()
                       ? 1
                       : static_cast<uint16_t>(candidate + 1);
        if (FindEntry(candidate) == nullptr) {
            return candidate;
        }
    }
    return 0;
}

ReminderEngine::Entry* ReminderEngine::FindEntry(uint16_t id) {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const Entry& entry) {
        return entry.active && entry.snapshot.id == id;
    });
    return found == entries_.end() ? nullptr : &*found;
}

const ReminderEngine::Entry* ReminderEngine::FindEntry(uint16_t id) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const Entry& entry) {
        return entry.active && entry.snapshot.id == id;
    });
    return found == entries_.end() ? nullptr : &*found;
}

ReminderEngine::Entry* ReminderEngine::FindFreeEntry() {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [](const Entry& entry) { return !entry.active; });
    return found == entries_.end() ? nullptr : &*found;
}

bool ReminderEngine::HasPomodoro() const {
    return std::any_of(entries_.begin(), entries_.end(), [](const Entry& entry) {
        return entry.active && entry.snapshot.kind == ReminderKind::kPomodoro;
    });
}

bool ReminderEngine::ValidateLabel(std::string_view label) const {
    return label.size() <= kMaxReminderLabelBytes &&
           std::find(label.begin(), label.end(), '\0') == label.end();
}

bool ReminderEngine::IsIntervalKind(ReminderKind kind) {
    return kind == ReminderKind::kWater || kind == ReminderKind::kSedentary;
}

bool ReminderEngine::IsPersistenceFailure(SaveResult result) {
    return result == SaveResult::kInvalidState || result == SaveResult::kWriteBlocked;
}

bool ReminderEngine::IsPersistencePending(SaveResult result) {
    return result == SaveResult::kDeferred || result == SaveResult::kRateLimited ||
           result == SaveResult::kFailed;
}

uint64_t ReminderEngine::AddMilliseconds(uint64_t value, uint64_t delta) {
    return delta > std::numeric_limits<uint64_t>::max() - value
               ? std::numeric_limits<uint64_t>::max()
               : value + delta;
}

int64_t ReminderEngine::AddSeconds(int64_t value, int64_t delta) {
    return delta > 0 && value > std::numeric_limits<int64_t>::max() - delta
               ? std::numeric_limits<int64_t>::max()
               : value + delta;
}

int64_t ReminderEngine::CivilSeconds(const DateTime& value) {
    if (!ReliableClock::IsValidDateTime(value, ReliableClock::kMinimumTrustedYear)) {
        return -1;
    }
    int year = value.year - (value.month <= 2 ? 1 : 0);
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned month = static_cast<unsigned>(value.month);
    const unsigned day_of_year =
        (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + value.day - 1;
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const int64_t days = static_cast<int64_t>(era) * 146097 + day_of_era - 719468;
    return days * 86400 + value.hour * 3600 + value.minute * 60 + value.second;
}

DateTime ReminderEngine::DateTimeFromCivilSeconds(int64_t value) {
    if (value < 0) {
        return {};
    }
    const int64_t days = value / 86400;
    const int seconds_of_day = static_cast<int>(value % 86400);
    int64_t shifted = days + 719468;
    const int era = static_cast<int>((shifted >= 0 ? shifted : shifted - 146096) / 146097);
    const unsigned day_of_era = static_cast<unsigned>(shifted - era * 146097);
    const unsigned year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int year = static_cast<int>(year_of_era) + era * 400;
    const unsigned day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned month_prime = (5 * day_of_year + 2) / 153;
    const unsigned day = day_of_year - (153 * month_prime + 2) / 5 + 1;
    const unsigned month = month_prime < 10 ? month_prime + 3 : month_prime - 9;
    year += month <= 2;
    return {.year = year,
            .month = static_cast<int>(month),
            .day = static_cast<int>(day),
            .hour = seconds_of_day / 3600,
            .minute = (seconds_of_day / 60) % 60,
            .second = seconds_of_day % 60};
}

RestoreResult ReminderEngine::Restore(const ClockSnapshot& clock) {
    entries_ = {};
    next_id_ = 1;
    const auto& state = storage_.GetState();
    const auto writable = storage_.Update(state, WriteImportance::kImportant, clock.monotonic_ms);
    if (writable == SaveResult::kWriteBlocked || writable == SaveResult::kInvalidState ||
        writable == SaveResult::kFailed) {
        return {.status = RestoreStatus::kUnavailable, .save_result = writable};
    }
    if (state.reminder_data_size == 0) {
        return {.status = RestoreStatus::kEmpty};
    }
    if (!Deserialize(state)) {
        entries_ = {};
        next_id_ = 1;
        auto recovered = state;
        recovered.reminder_data.fill(0);
        recovered.reminder_data_size = 0;
        const auto save =
            storage_.Update(recovered, WriteImportance::kImportant, clock.monotonic_ms);
        return {.status = IsPersistenceFailure(save) ? RestoreStatus::kUnavailable
                                                     : RestoreStatus::kRecovered,
                .save_result = save};
    }

    bool normalized = false;
    if (clock.valid) {
        const int64_t now_seconds = CivilSeconds(clock.local_time);
        for (auto& entry : entries_) {
            if (!entry.active || !IsIntervalKind(entry.snapshot.kind) ||
                entry.snapshot.next_wall_time_seconds > now_seconds) {
                continue;
            }
            const int64_t interval = entry.snapshot.interval_seconds;
            if (entry.snapshot.next_wall_time_seconds <= 0) {
                entry.snapshot.next_wall_time_seconds = AddSeconds(now_seconds, interval);
            } else {
                const int64_t elapsed = now_seconds - entry.snapshot.next_wall_time_seconds;
                const int64_t steps = elapsed / interval + 1;
                entry.snapshot.next_wall_time_seconds =
                    AddSeconds(entry.snapshot.next_wall_time_seconds, steps * interval);
            }
            normalized = true;
        }
    }

    SaveResult save_result = SaveResult::kNoChanges;
    if (normalized) {
        save_result = Persist(clock);
        if (IsPersistenceFailure(save_result)) {
            return {.status = RestoreStatus::kUnavailable,
                    .save_result = save_result,
                    .restored_count = PersistentCount()};
        }
    }
    return {.status = RestoreStatus::kRestored,
            .save_result = save_result,
            .restored_count = PersistentCount()};
}

SaveResult ReminderEngine::Persist(const ClockSnapshot& clock) {
    auto state = storage_.GetState();
    if (!Serialize(&state)) {
        return SaveResult::kInvalidState;
    }
    auto result = storage_.Update(state, WriteImportance::kImportant, clock.monotonic_ms);
    if (result == SaveResult::kNoChanges && storage_.NeedsWrite()) {
        result = storage_.FlushIfDue(clock.monotonic_ms);
    }
    if (result == SaveResult::kFailed && !(storage_.GetState() == state)) {
        return SaveResult::kInvalidState;
    }
    return result;
}

bool ReminderEngine::Serialize(PersistentState* state) const {
    if (state == nullptr || PersistentCount() > kMaxPersistentReminders) {
        return false;
    }
    state->reminder_data.fill(0);
    state->reminder_data_size = 0;
    const size_t persistent_count = PersistentCount();
    if (persistent_count == 0) {
        return true;
    }

    BlobWriter writer(state);
    if (!writer.UInt32(kBlobMagic) || !writer.UInt8(kBlobVersion) ||
        !writer.UInt8(static_cast<uint8_t>(persistent_count)) || !writer.UInt16(next_id_) ||
        !writer.UInt32(0)) {
        return false;
    }
    for (const auto& entry : entries_) {
        if (!entry.active || !entry.snapshot.persistent) {
            continue;
        }
        const auto label_end =
            std::find(entry.snapshot.label.begin(), entry.snapshot.label.end(), '\0');
        const size_t label_size =
            static_cast<size_t>(std::distance(entry.snapshot.label.begin(), label_end));
        if (label_size > kMaxReminderLabelBytes || !writer.UInt16(entry.snapshot.id) ||
            !writer.UInt8(static_cast<uint8_t>(entry.snapshot.kind)) ||
            !writer.UInt8(static_cast<uint8_t>(label_size)) ||
            !writer.Int64(entry.snapshot.next_wall_time_seconds) ||
            !writer.UInt32(entry.snapshot.interval_seconds) ||
            !writer.Bytes(entry.snapshot.label.data(), label_size)) {
            return false;
        }
    }
    const uint32_t checksum = BlobChecksum(state->reminder_data.data(), writer.size());
    if (!writer.PatchUInt32(8, checksum)) {
        return false;
    }
    state->reminder_data_size = writer.size();
    return true;
}

bool ReminderEngine::Deserialize(const PersistentState& state) {
    if (state.reminder_data_size < kBlobHeaderSize ||
        state.reminder_data_size > state.reminder_data.size()) {
        return false;
    }
    BlobReader reader(state);
    uint32_t magic = 0;
    uint8_t version = 0;
    uint8_t count = 0;
    uint16_t next_id = 0;
    uint32_t checksum = 0;
    if (!reader.UInt32(&magic) || !reader.UInt8(&version) || !reader.UInt8(&count) ||
        !reader.UInt16(&next_id) || !reader.UInt32(&checksum) || magic != kBlobMagic ||
        version != kBlobVersion || count == 0 || count > kMaxPersistentReminders || next_id == 0 ||
        checksum != BlobChecksum(state.reminder_data.data(), state.reminder_data_size)) {
        return false;
    }

    std::array<Entry, kMaxActiveReminders> decoded{};
    for (size_t index = 0; index < count; ++index) {
        uint16_t id = 0;
        uint8_t encoded_kind = 0;
        uint8_t label_size = 0;
        int64_t next_wall_time = 0;
        uint32_t interval_seconds = 0;
        if (!reader.UInt16(&id) || !reader.UInt8(&encoded_kind) || !reader.UInt8(&label_size) ||
            !reader.Int64(&next_wall_time) || !reader.UInt32(&interval_seconds) || id == 0 ||
            label_size > kMaxReminderLabelBytes) {
            return false;
        }
        const auto kind = static_cast<ReminderKind>(encoded_kind);
        const int64_t maximum_wall_time = CivilSeconds({9999, 12, 31, 23, 59, 59});
        const bool wall_time_valid = next_wall_time >= 0 && next_wall_time <= maximum_wall_time;
        const bool alarm_valid =
            kind == ReminderKind::kAlarm && wall_time_valid &&
            CivilSeconds(DateTimeFromCivilSeconds(next_wall_time)) == next_wall_time &&
            interval_seconds == 0;
        const bool interval_valid = IsIntervalKind(kind) && wall_time_valid &&
                                    interval_seconds >= 10 * 60 && interval_seconds <= 720 * 60 &&
                                    interval_seconds % 60 == 0;
        if ((!alarm_valid && !interval_valid) ||
            std::any_of(decoded.begin(), decoded.begin() + index,
                        [id](const Entry& entry) { return entry.snapshot.id == id; })) {
            return false;
        }

        auto& entry = decoded[index];
        entry.active = true;
        entry.snapshot.id = id;
        entry.snapshot.kind = kind;
        entry.snapshot.persistent = true;
        entry.snapshot.next_wall_time_seconds = next_wall_time;
        entry.snapshot.interval_seconds = interval_seconds;
        if (label_size != 0 && !reader.Bytes(entry.snapshot.label.data(), label_size)) {
            return false;
        }
        if (std::find(entry.snapshot.label.begin(), entry.snapshot.label.begin() + label_size,
                      '\0') != entry.snapshot.label.begin() + label_size) {
            return false;
        }
        entry.snapshot.label[label_size] = '\0';
    }
    if (!reader.AtEnd()) {
        return false;
    }
    entries_ = decoded;
    next_id_ = next_id;
    return true;
}

bool ReminderEngine::IsQuietWallTime(int64_t wall_seconds) {
    const DateTime local_time = DateTimeFromCivilSeconds(wall_seconds);
    return ReliableClock::IsWithinDailyWindow(local_time, 22, 0, 8, 0);
}

bool ReminderEngine::AdvanceInterval(Entry* entry, int64_t now_seconds, const ClockSnapshot& clock,
                                     SaveResult* save_result) {
    if (entry == nullptr || !IsIntervalKind(entry->snapshot.kind) ||
        entry->snapshot.interval_seconds == 0 || save_result == nullptr) {
        return false;
    }
    const Entry previous = *entry;
    const int64_t interval = entry->snapshot.interval_seconds;
    if (entry->snapshot.next_wall_time_seconds <= 0) {
        entry->snapshot.next_wall_time_seconds = AddSeconds(now_seconds, interval);
    } else {
        const int64_t elapsed =
            std::max<int64_t>(0, now_seconds - entry->snapshot.next_wall_time_seconds);
        const int64_t steps = elapsed / interval + 1;
        entry->snapshot.next_wall_time_seconds =
            AddSeconds(entry->snapshot.next_wall_time_seconds, steps * interval);
    }
    entry->due_pending = false;
    entry->pending_since_ms = 0;
    *save_result = Persist(clock);
    if (IsPersistenceFailure(*save_result)) {
        *entry = previous;
        return false;
    }
    return true;
}

bool ReminderEngine::RemovePersistent(Entry* entry, const ClockSnapshot& clock,
                                      SaveResult* save_result) {
    if (entry == nullptr || !entry->snapshot.persistent || save_result == nullptr) {
        return false;
    }
    const Entry previous = *entry;
    *entry = {};
    *save_result = Persist(clock);
    if (IsPersistenceFailure(*save_result) || IsPersistencePending(*save_result)) {
        *entry = previous;
        return false;
    }
    return true;
}

ReminderEvent ReminderEngine::FinishDue(Entry* entry, const ReminderTick& tick, bool missed) {
    if (entry == nullptr || !entry->active) {
        return {};
    }
    ReminderEvent event = {
        .state = missed ? ReminderEventState::kMissed : ReminderEventState::kTriggered,
        .id = entry->snapshot.id,
        .kind = entry->snapshot.kind,
        .phase = entry->snapshot.phase,
        .audible = !missed && !tick.low_battery,
    };

    SaveResult save_result = SaveResult::kNoChanges;
    if (IsIntervalKind(entry->snapshot.kind)) {
        const int64_t now_seconds = CivilSeconds(tick.clock.local_time);
        event.audible = event.audible && !IsQuietWallTime(now_seconds);
        if (!AdvanceInterval(entry, now_seconds, tick.clock, &save_result)) {
            return {};
        }
    } else if (entry->snapshot.kind == ReminderKind::kAlarm) {
        if (!RemovePersistent(entry, tick.clock, &save_result)) {
            if (!entry->persistence_retry_pending) {
                entry->persistence_retry_missed = missed;
            }
            entry->persistence_retry_pending = true;
            return {};
        }
    } else if (entry->snapshot.kind == ReminderKind::kPomodoro && !missed) {
        const ReminderPhase completed_phase = entry->snapshot.phase;
        entry->due_pending = false;
        entry->pending_since_ms = 0;
        if (completed_phase == ReminderPhase::kWork) {
            ++entry->snapshot.completed_cycles;
            if (entry->snapshot.completed_cycles >= entry->snapshot.total_cycles) {
                *entry = {};
            } else {
                entry->snapshot.phase = ReminderPhase::kBreak;
                entry->deadline_ms =
                    AddMilliseconds(tick.clock.monotonic_ms, entry->break_seconds * 1000ULL);
            }
        } else {
            entry->snapshot.phase = ReminderPhase::kWork;
            entry->deadline_ms =
                AddMilliseconds(tick.clock.monotonic_ms, entry->work_seconds * 1000ULL);
        }
    } else {
        *entry = {};
    }
    event.persistence_pending = IsPersistencePending(save_result);
    return event;
}

ReminderEvent ReminderEngine::Update(const ReminderTick& tick) {
    const int64_t now_seconds = tick.clock.valid ? CivilSeconds(tick.clock.local_time) : -1;
    for (auto& entry : entries_) {
        if (!entry.active) {
            continue;
        }

        bool due = false;
        if (entry.snapshot.persistent) {
            if (!tick.clock.valid || now_seconds < 0) {
                continue;
            }
            if (IsIntervalKind(entry.snapshot.kind) && entry.snapshot.next_wall_time_seconds <= 0) {
                SaveResult save_result = SaveResult::kNoChanges;
                AdvanceInterval(&entry, now_seconds, tick.clock, &save_result);
                continue;
            }
            due = now_seconds >= entry.snapshot.next_wall_time_seconds;
            if (due && entry.snapshot.kind == ReminderKind::kAlarm &&
                entry.persistence_retry_pending) {
                if (tick.device_busy && !entry.persistence_retry_missed) {
                    continue;
                }
                return FinishDue(&entry, tick, entry.persistence_retry_missed);
            }
            if (due && IsIntervalKind(entry.snapshot.kind) &&
                IsQuietWallTime(entry.snapshot.next_wall_time_seconds) &&
                !IsQuietWallTime(now_seconds) &&
                now_seconds > entry.snapshot.next_wall_time_seconds) {
                SaveResult save_result = SaveResult::kNoChanges;
                AdvanceInterval(&entry, now_seconds, tick.clock, &save_result);
                continue;
            }
            if (due && entry.snapshot.kind == ReminderKind::kAlarm &&
                now_seconds - entry.snapshot.next_wall_time_seconds >
                    kMaximumRestartAlarmLatenessSeconds) {
                return FinishDue(&entry, tick, true);
            }
        } else {
            due = tick.clock.monotonic_ms >= entry.deadline_ms;
        }
        if (!due) {
            continue;
        }

        if (!entry.due_pending) {
            entry.due_pending = true;
            if (!entry.snapshot.persistent) {
                entry.pending_since_ms = entry.deadline_ms;
            } else if (IsIntervalKind(entry.snapshot.kind)) {
                const uint64_t late_ms =
                    static_cast<uint64_t>(
                        std::max<int64_t>(0, now_seconds - entry.snapshot.next_wall_time_seconds)) *
                    1000;
                entry.pending_since_ms =
                    late_ms > tick.clock.monotonic_ms ? 0 : tick.clock.monotonic_ms - late_ms;
            } else {
                // A restored alarm may already be late; its 10-minute restart allowance is
                // separate from the bounded busy deferral that starts when we first observe it.
                entry.pending_since_ms = tick.clock.monotonic_ms;
            }
        }
        const bool expired =
            tick.clock.monotonic_ms >= entry.pending_since_ms &&
            tick.clock.monotonic_ms - entry.pending_since_ms >= kMaximumBusyDeferralMs;
        const bool countdown_can_preempt =
            tick.allow_countdown_busy_preemption &&
            entry.snapshot.kind == ReminderKind::kCountdown;
        if (tick.device_busy && !expired && !countdown_can_preempt) {
            continue;
        }
        return FinishDue(&entry, tick, expired);
    }
    return {};
}

}  // namespace maomi
