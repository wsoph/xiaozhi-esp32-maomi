#include "maomi_tools.h"

#include "mcp_server.h"

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

}  // namespace maomi
