#include "maomi_tools.h"

#include "mcp_server.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace maomi {
namespace {

PetAction ParseAction(const std::string& action) {
    if (action == "pet") {
        return PetAction::kPet;
    }
    if (action == "feed") {
        return PetAction::kFeed;
    }
    if (action == "play") {
        return PetAction::kPlay;
    }
    throw std::runtime_error("Unsupported pet action; allowed values are pet, feed, and play");
}

const char* ActionText(PetAction action) {
    switch (action) {
        case PetAction::kPet:
            return "pet";
        case PetAction::kFeed:
            return "feed";
        case PetAction::kPlay:
            return "play";
    }
    return nullptr;
}

const char* OperationText(ToolOperationState state) {
    switch (state) {
        case ToolOperationState::kCompleted:
            return "completed";
        case ToolOperationState::kQueued:
            return "queued";
        case ToolOperationState::kRejected:
            return "rejected";
        case ToolOperationState::kUnavailable:
            return "unavailable";
    }
    return nullptr;
}

const char* BondLevelTextForTool(BondLevel level) {
    switch (level) {
        case BondLevel::kAcquainted:
            return "acquainted";
        case BondLevel::kFamiliar:
            return "familiar";
        case BondLevel::kClose:
            return "close";
        case BondLevel::kInSync:
            return "in_sync";
    }
    return nullptr;
}

const char* MoodText(PetState mood) {
    switch (mood) {
        case PetState::kIdle:
            return "idle";
        case PetState::kCurious:
            return "curious";
        case PetState::kSleepy:
            return "sleepy";
        case PetState::kSleeping:
            return "sleeping";
        case PetState::kHappy:
            return "happy";
        case PetState::kEating:
            return "eating";
        case PetState::kPlaying:
            return "playing";
        case PetState::kCharging:
            return "charging";
        case PetState::kFull:
            return "full";
        case PetState::kLowBattery:
            return "low_battery";
        case PetState::kReminding:
            return "reminding";
    }
    return nullptr;
}

const char* BoolText(bool value) { return value ? "true" : "false"; }

void ValidateInteractionResult(const InteractionToolResult& result, PetAction requested_action) {
    if (result.action != requested_action || ActionText(result.action) == nullptr ||
        OperationText(result.state) == nullptr || result.points_added > 3 ||
        result.bond_points < 0 || result.bond_points > kMaximumBondPoints) {
        throw std::runtime_error("Pet interaction returned an invalid device snapshot");
    }
    if (result.state == ToolOperationState::kRejected) {
        throw std::runtime_error("Pet interaction was rejected because the device queue is busy");
    }
    if (result.state == ToolOperationState::kUnavailable) {
        throw std::runtime_error("Pet interaction is unavailable on this device");
    }
}

void ValidateSnapshot(const PetToolSnapshot& snapshot) {
    if (snapshot.bond_points < 0 || snapshot.bond_points > kMaximumBondPoints ||
        BondLevelTextForTool(snapshot.bond_level) == nullptr ||
        MoodText(snapshot.mood) == nullptr || snapshot.battery_level < -1 ||
        snapshot.battery_level > 100) {
        throw std::runtime_error("Pet status returned an invalid device snapshot");
    }
}

std::string InteractionJson(const InteractionToolResult& result) {
    std::string json = "{\"ok\":true,\"status\":\"";
    json += OperationText(result.state);
    json += "\",\"action\":\"";
    json += ActionText(result.action);
    json += "\",\"points_added\":" + std::to_string(result.points_added);
    json += ",\"bond_points\":" + std::to_string(result.bond_points);
    json += ",\"sound_queued\":";
    json += BoolText(result.sound_queued);
    json += ",\"persistence_pending\":";
    json += BoolText(result.persistence_pending);
    json += "}";
    return json;
}

std::string StatusJson(const PetToolSnapshot& snapshot) {
    ValidateSnapshot(snapshot);
    std::string json = "{\"ok\":true,\"name\":\"小猫咪\",\"bond_points\":";
    json += std::to_string(snapshot.bond_points);
    json += ",\"bond_level\":\"";
    json += BondLevelTextForTool(snapshot.bond_level);
    json += "\",\"companion_days\":" + std::to_string(snapshot.companion_days);
    json += ",\"mood\":\"";
    json += MoodText(snapshot.mood);
    json += "\",\"battery_level\":";
    if (snapshot.battery_level < 0) {
        json += "null";
    } else {
        json += std::to_string(snapshot.battery_level);
    }
    json += ",\"charging\":";
    json += BoolText(snapshot.charging);
    json += ",\"manual_quiet\":";
    json += BoolText(snapshot.manual_quiet);
    json += ",\"active_reminders\":" + std::to_string(snapshot.active_reminders);
    json += "}";
    return json;
}

std::string QuietJson(const QuietToolResult& result) {
    if (OperationText(result.state) == nullptr) {
        throw std::runtime_error("Quiet mode returned an invalid device result");
    }
    if (result.state == ToolOperationState::kRejected) {
        throw std::runtime_error("Quiet mode change was rejected");
    }
    if (result.state == ToolOperationState::kUnavailable) {
        throw std::runtime_error("Quiet mode is unavailable on this device");
    }

    std::string json = "{\"ok\":true,\"status\":\"";
    json += OperationText(result.state);
    json += "\",\"manual_quiet\":";
    json += BoolText(result.enabled);
    json += ",\"persistence_pending\":";
    json += BoolText(result.persistence_pending);
    json += "}";
    return json;
}

const char* ReminderKindText(ReminderKind kind) {
    switch (kind) {
        case ReminderKind::kCountdown:
            return "countdown";
        case ReminderKind::kAlarm:
            return "alarm";
        case ReminderKind::kWater:
            return "water";
        case ReminderKind::kSedentary:
            return "sedentary";
        case ReminderKind::kPomodoro:
            return "pomodoro";
    }
    return nullptr;
}

const char* ReminderPhaseText(ReminderPhase phase) {
    switch (phase) {
        case ReminderPhase::kNone:
            return "none";
        case ReminderPhase::kWork:
            return "work";
        case ReminderPhase::kBreak:
            return "break";
    }
    return nullptr;
}

bool IsLeapYear(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

int DaysInMonth(int year, int month) {
    constexpr std::array<int, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > static_cast<int>(kDays.size())) {
        return 0;
    }
    return month == 2 && IsLeapYear(year) ? 29 : kDays[month - 1];
}

int ParseDigits(std::string_view value, size_t offset, size_t count) {
    int result = 0;
    for (size_t index = 0; index < count; ++index) {
        const char character = value[offset + index];
        if (character < '0' || character > '9') {
            throw std::runtime_error("Date must use YYYY-MM-DD format");
        }
        result = result * 10 + (character - '0');
    }
    return result;
}

DateTime ParseDate(std::string_view value, int hour, int minute) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        throw std::runtime_error("Date must use YYYY-MM-DD format");
    }
    const int year = ParseDigits(value, 0, 4);
    const int month = ParseDigits(value, 5, 2);
    const int day = ParseDigits(value, 8, 2);
    if (year < ReliableClock::kMinimumTrustedYear || month < 1 || month > 12 || day < 1 ||
        day > DaysInMonth(year, month) || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        throw std::runtime_error("Date or time is invalid");
    }
    return {.year = year, .month = month, .day = day, .hour = hour, .minute = minute};
}

bool IsValidUtf8(std::string_view value) {
    size_t offset = 0;
    while (offset < value.size()) {
        const auto first = static_cast<uint8_t>(value[offset]);
        if (first < 0x80) {
            ++offset;
            continue;
        }

        size_t continuation_count = 0;
        uint32_t code_point = 0;
        uint32_t minimum = 0;
        if ((first & 0xE0) == 0xC0) {
            continuation_count = 1;
            code_point = first & 0x1F;
            minimum = 0x80;
        } else if ((first & 0xF0) == 0xE0) {
            continuation_count = 2;
            code_point = first & 0x0F;
            minimum = 0x800;
        } else if ((first & 0xF8) == 0xF0) {
            continuation_count = 3;
            code_point = first & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuation_count > value.size() - offset - 1) {
            return false;
        }
        for (size_t index = 1; index <= continuation_count; ++index) {
            const auto continuation = static_cast<uint8_t>(value[offset + index]);
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        if (code_point < minimum || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }
        offset += continuation_count + 1;
    }
    return true;
}

void ValidateLabelArgument(std::string_view label) {
    if (label.size() > kMaxReminderLabelBytes || !IsValidUtf8(label) ||
        std::any_of(label.begin(), label.end(), [](char character) {
            const auto byte = static_cast<uint8_t>(character);
            return byte < 0x20 || byte == 0x7F;
        })) {
        throw std::runtime_error(
            "Reminder label must be valid UTF-8 without control characters and at most 32 bytes");
    }
}

ReminderKind ParseIntervalKind(const std::string& kind) {
    if (kind == "water") {
        return ReminderKind::kWater;
    }
    if (kind == "sedentary") {
        return ReminderKind::kSedentary;
    }
    throw std::runtime_error("Interval reminder kind must be water or sedentary");
}

bool PersistencePending(SaveResult result) {
    return result == SaveResult::kDeferred || result == SaveResult::kRateLimited ||
           result == SaveResult::kFailed;
}

void ValidateSuccessfulResult(const ReminderResult& result, ReminderStatus expected_status,
                              ReminderKind expected_kind) {
    if (result.status == ReminderStatus::kInvalidArgument) {
        throw std::runtime_error("Reminder arguments were rejected by the device");
    }
    if (result.status == ReminderStatus::kCapacityReached) {
        throw std::runtime_error("Reminder capacity has been reached");
    }
    if (result.status == ReminderStatus::kPomodoroActive) {
        throw std::runtime_error("A pomodoro is already active");
    }
    if (result.status == ReminderStatus::kNotFound) {
        throw std::runtime_error("Reminder ID was not found");
    }
    if (result.status == ReminderStatus::kPersistenceUnavailable) {
        throw std::runtime_error("Reminder storage is unavailable");
    }
    if (result.status != expected_status || result.id == 0 || result.kind != expected_kind ||
        ReminderKindText(result.kind) == nullptr) {
        throw std::runtime_error("Reminder operation returned an invalid device result");
    }
}

std::string_view SnapshotLabel(const ReminderSnapshot& snapshot) {
    const auto end = std::find(snapshot.label.begin(), snapshot.label.end(), '\0');
    if (end == snapshot.label.end()) {
        throw std::runtime_error("Reminder list returned an invalid device snapshot");
    }
    const std::string_view label(snapshot.label.data(),
                                 static_cast<size_t>(end - snapshot.label.begin()));
    ValidateLabelArgument(label);
    return label;
}

void ValidateSnapshot(const ReminderSnapshot& snapshot) {
    const bool interval =
        snapshot.kind == ReminderKind::kWater || snapshot.kind == ReminderKind::kSedentary;
    const bool pomodoro = snapshot.kind == ReminderKind::kPomodoro;
    if (snapshot.id == 0 || ReminderKindText(snapshot.kind) == nullptr ||
        ReminderPhaseText(snapshot.phase) == nullptr ||
        (snapshot.persistent != (snapshot.kind == ReminderKind::kAlarm || interval)) ||
        (pomodoro && snapshot.phase == ReminderPhase::kNone) ||
        (!pomodoro && snapshot.phase != ReminderPhase::kNone) ||
        (snapshot.kind == ReminderKind::kAlarm && snapshot.next_wall_time_seconds <= 0) ||
        (interval && (snapshot.interval_seconds < 10 * 60 || snapshot.interval_seconds > 720 * 60 ||
                      snapshot.interval_seconds % 60 != 0)) ||
        (pomodoro && (snapshot.total_cycles < 1 || snapshot.total_cycles > 12 ||
                      snapshot.completed_cycles > snapshot.total_cycles))) {
        throw std::runtime_error("Reminder list returned an invalid device snapshot");
    }
    static_cast<void>(SnapshotLabel(snapshot));
}

void ValidateList(const ReminderList& list) {
    if (list.count > list.items.size()) {
        throw std::runtime_error("Reminder list returned an invalid device snapshot");
    }
    for (size_t index = 0; index < list.count; ++index) {
        ValidateSnapshot(list.items[index]);
        for (size_t earlier = 0; earlier < index; ++earlier) {
            if (list.items[earlier].id == list.items[index].id) {
                throw std::runtime_error("Reminder list returned duplicate IDs");
            }
        }
    }
}

std::string JsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '"' || character == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

std::string SnapshotJson(const ReminderSnapshot& snapshot) {
    ValidateSnapshot(snapshot);
    std::string json = "{\"id\":" + std::to_string(snapshot.id);
    json += ",\"type\":\"";
    json += ReminderKindText(snapshot.kind);
    json += "\",\"persistent\":";
    json += BoolText(snapshot.persistent);
    json += ",\"label\":\"" + JsonEscape(SnapshotLabel(snapshot)) + "\"";
    json += ",\"remaining_seconds\":" +
            std::to_string(snapshot.remaining_ms / 1000 + (snapshot.remaining_ms % 1000 != 0));
    json += ",\"next_wall_time_seconds\":";
    if (snapshot.next_wall_time_seconds > 0) {
        json += std::to_string(snapshot.next_wall_time_seconds);
    } else {
        json += "null";
    }
    json += ",\"interval_minutes\":" + std::to_string(snapshot.interval_seconds / 60);
    json += ",\"phase\":\"";
    json += ReminderPhaseText(snapshot.phase);
    json += "\",\"completed_cycles\":" + std::to_string(snapshot.completed_cycles);
    json += ",\"total_cycles\":" + std::to_string(snapshot.total_cycles);
    json += "}";
    return json;
}

std::string CreatedReminderJson(const ReminderResult& result, ReminderKind expected_kind,
                                const std::function<ReminderList()>& list) {
    ValidateSuccessfulResult(result, ReminderStatus::kAccepted, expected_kind);
    if (!list) {
        throw std::runtime_error("Reminder list is unavailable on this device");
    }
    const auto current = list();
    ValidateList(current);
    const auto found = std::find_if(
        current.items.begin(), current.items.begin() + current.count,
        [&result](const ReminderSnapshot& snapshot) { return snapshot.id == result.id; });
    if (found == current.items.begin() + current.count || found->kind != result.kind ||
        found->persistent != result.persistent) {
        throw std::runtime_error("Reminder creation returned an inconsistent device snapshot");
    }
    std::string json = "{\"ok\":true,\"status\":\"accepted\",\"reminder\":";
    json += SnapshotJson(*found);
    json += ",\"persistence_pending\":";
    json += BoolText(PersistencePending(result.save_result));
    json += "}";
    return json;
}

std::string ReminderListJson(const ReminderList& list) {
    ValidateList(list);
    std::string json = "{\"ok\":true,\"count\":" + std::to_string(list.count) + ",\"reminders\":[";
    for (size_t index = 0; index < list.count; ++index) {
        if (index != 0) {
            json += ',';
        }
        json += SnapshotJson(list.items[index]);
    }
    json += "]}";
    return json;
}

std::string CancelledReminderJson(const ReminderResult& result) {
    ValidateSuccessfulResult(result, ReminderStatus::kCancelled, result.kind);
    std::string json = "{\"ok\":true,\"status\":\"cancelled\",\"id\":" + std::to_string(result.id) +
                       ",\"type\":\"";
    json += ReminderKindText(result.kind);
    json += "\",\"persistent\":";
    json += BoolText(result.persistent);
    json += ",\"persistence_pending\":";
    json += BoolText(PersistencePending(result.save_result));
    json += "}";
    return json;
}

}  // namespace

void RegisterPetTools(McpServer& server, PetToolDependencies dependencies) {
    auto interact = std::move(dependencies.interact);
    server.AddTool(
        kPetInteractToolName,
        "当主人明确要摸小猫咪、喂零食或陪玩时必须调用。action 只能是 pet、feed、play；"
        "只有工具成功返回后才能说互动已经执行，queued 表示设备已排队而不是已经展示完成。",
        PropertyList({Property("action", kPropertyTypeString)}),
        [interact = std::move(interact)](const PropertyList& properties) -> ReturnValue {
            if (!interact) {
                throw std::runtime_error("Pet interaction is unavailable on this device");
            }
            const PetAction action = ParseAction(properties["action"].value<std::string>());
            const auto result = interact(action);
            ValidateInteractionResult(result, action);
            return InteractionJson(result);
        });

    auto get_status = std::move(dependencies.get_status);
    server.AddTool(
        kPetStatusToolName,
        "当主人询问小猫咪的名字、心情、亲密度、陪伴天数、电池、充电、安静模式或提醒数量时"
        "调用；必须依据返回的实时状态回答。",
        PropertyList(), [get_status = std::move(get_status)](const PropertyList&) -> ReturnValue {
            if (!get_status) {
                throw std::runtime_error("Pet status is unavailable on this device");
            }
            return StatusJson(get_status());
        });

    auto set_quiet = std::move(dependencies.set_quiet);
    server.AddTool(
        kPetQuietToolName,
        "当主人要求小猫咪安静或恢复活泼时必须调用。enabled=true 开启手动安静，false 关闭；"
        "只有工具成功返回后才能声称状态已改变。",
        PropertyList({Property("enabled", kPropertyTypeBoolean)}),
        [set_quiet = std::move(set_quiet)](const PropertyList& properties) -> ReturnValue {
            if (!set_quiet) {
                throw std::runtime_error("Quiet mode is unavailable on this device");
            }
            return QuietJson(set_quiet(properties["enabled"].value<bool>()));
        });
}

void RegisterReminderTools(McpServer& server, ReminderToolDependencies dependencies) {
    auto start_countdown = std::move(dependencies.start_countdown);
    auto countdown_list = dependencies.list;
    server.AddTool(
        kCountdownToolName,
        "Create a local countdown from 1 to 86400 seconds. Use only after the user asks for a "
        "countdown or timer.",
        PropertyList({Property("duration_seconds", kPropertyTypeInteger, 1, 86400),
                      Property("label", kPropertyTypeString, std::string())}),
        [start_countdown = std::move(start_countdown),
         list = std::move(countdown_list)](const PropertyList& properties) -> ReturnValue {
            if (!start_countdown || !list) {
                throw std::runtime_error("Countdown reminders are unavailable on this device");
            }
            const auto label = properties["label"].value<std::string>();
            ValidateLabelArgument(label);
            const int duration_seconds = properties["duration_seconds"].value<int>();
            if (duration_seconds < 1 || duration_seconds > 86400) {
                throw std::runtime_error("Countdown duration must be between 1 and 86400 seconds");
            }
            return CreatedReminderJson(
                start_countdown(static_cast<uint32_t>(duration_seconds), label),
                ReminderKind::kCountdown, list);
        });

    auto set_alarm = std::move(dependencies.set_alarm);
    auto alarm_list = dependencies.list;
    server.AddTool(
        kAlarmToolName,
        "Create one local alarm at a future local date and time. Date must use YYYY-MM-DD.",
        PropertyList({Property("date", kPropertyTypeString),
                      Property("hour", kPropertyTypeInteger, 0, 23),
                      Property("minute", kPropertyTypeInteger, 0, 59),
                      Property("label", kPropertyTypeString, std::string())}),
        [set_alarm = std::move(set_alarm),
         list = std::move(alarm_list)](const PropertyList& properties) -> ReturnValue {
            if (!set_alarm || !list) {
                throw std::runtime_error("Alarms are unavailable on this device");
            }
            const auto label = properties["label"].value<std::string>();
            ValidateLabelArgument(label);
            const auto target =
                ParseDate(properties["date"].value<std::string>(), properties["hour"].value<int>(),
                          properties["minute"].value<int>());
            return CreatedReminderJson(set_alarm(target, label), ReminderKind::kAlarm, list);
        });

    auto start_interval = std::move(dependencies.start_interval);
    auto interval_list = dependencies.list;
    server.AddTool(
        kIntervalReminderToolName,
        "Create a persistent water or sedentary reminder every 10 to 720 minutes.",
        PropertyList({Property("kind", kPropertyTypeString),
                      Property("interval_minutes", kPropertyTypeInteger, 10, 720),
                      Property("label", kPropertyTypeString, std::string())}),
        [start_interval = std::move(start_interval),
         list = std::move(interval_list)](const PropertyList& properties) -> ReturnValue {
            if (!start_interval || !list) {
                throw std::runtime_error("Interval reminders are unavailable on this device");
            }
            const auto label = properties["label"].value<std::string>();
            ValidateLabelArgument(label);
            const auto kind = ParseIntervalKind(properties["kind"].value<std::string>());
            const int interval_minutes = properties["interval_minutes"].value<int>();
            if (interval_minutes < 10 || interval_minutes > 720) {
                throw std::runtime_error("Reminder interval must be between 10 and 720 minutes");
            }
            return CreatedReminderJson(
                start_interval(kind, static_cast<uint32_t>(interval_minutes), label), kind, list);
        });

    auto start_pomodoro = std::move(dependencies.start_pomodoro);
    auto pomodoro_list = dependencies.list;
    server.AddTool(
        kPomodoroToolName, "Start one local pomodoro with bounded work, break, and cycle counts.",
        PropertyList({Property("work_minutes", kPropertyTypeInteger, 1, 120),
                      Property("break_minutes", kPropertyTypeInteger, 1, 60),
                      Property("cycles", kPropertyTypeInteger, 1, 12)}),
        [start_pomodoro = std::move(start_pomodoro),
         list = std::move(pomodoro_list)](const PropertyList& properties) -> ReturnValue {
            if (!start_pomodoro || !list) {
                throw std::runtime_error("Pomodoro is unavailable on this device");
            }
            const int work_minutes = properties["work_minutes"].value<int>();
            const int break_minutes = properties["break_minutes"].value<int>();
            const int cycles = properties["cycles"].value<int>();
            if (work_minutes < 1 || work_minutes > 120 || break_minutes < 1 || break_minutes > 60 ||
                cycles < 1 || cycles > 12) {
                throw std::runtime_error("Pomodoro parameters are outside their allowed ranges");
            }
            return CreatedReminderJson(
                start_pomodoro(static_cast<uint32_t>(work_minutes),
                               static_cast<uint32_t>(break_minutes), static_cast<uint32_t>(cycles)),
                ReminderKind::kPomodoro, list);
        });

    auto list = std::move(dependencies.list);
    server.AddTool(kReminderListToolName,
                   "List the device's active reminders, remaining time, next local trigger, and "
                   "pomodoro phase.",
                   PropertyList(), [list = std::move(list)](const PropertyList&) -> ReturnValue {
                       if (!list) {
                           throw std::runtime_error("Reminder list is unavailable on this device");
                       }
                       return ReminderListJson(list());
                   });

    auto cancel = std::move(dependencies.cancel);
    server.AddTool(
        kReminderCancelToolName,
        "Cancel one local reminder by the exact ID returned by create or list.",
        PropertyList({Property("id", kPropertyTypeInteger, 1,
                               static_cast<int>(std::numeric_limits<uint16_t>::max()))}),
        [cancel = std::move(cancel)](const PropertyList& properties) -> ReturnValue {
            if (!cancel) {
                throw std::runtime_error("Reminder cancellation is unavailable on this device");
            }
            const int id = properties["id"].value<int>();
            if (id < 1 || id > std::numeric_limits<uint16_t>::max()) {
                throw std::runtime_error("Reminder ID must be between 1 and 65535");
            }
            return CancelledReminderJson(cancel(static_cast<uint16_t>(id)));
        });
}

}  // namespace maomi
