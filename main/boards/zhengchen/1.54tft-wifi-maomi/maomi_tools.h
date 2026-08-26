#pragma once

#include "maomi_bond.h"
#include "maomi_pet_core.h"

#include <cstdint>
#include <functional>

class McpServer;

namespace maomi {

inline constexpr char kPetInteractToolName[] = "self.pet.interact";
inline constexpr char kPetStatusToolName[] = "self.pet.get_status";
inline constexpr char kPetQuietToolName[] = "self.pet.set_quiet";

enum class PetAction : uint8_t {
    kPet,
    kFeed,
    kPlay,
};

enum class ToolOperationState : uint8_t {
    kCompleted,
    kQueued,
    kRejected,
    kUnavailable,
};

struct InteractionToolResult {
    ToolOperationState state = ToolOperationState::kUnavailable;
    PetAction action = PetAction::kPet;
    uint8_t points_added = 0;
    int32_t bond_points = 0;
    bool sound_queued = false;
    bool persistence_pending = false;
};

struct QuietToolResult {
    ToolOperationState state = ToolOperationState::kUnavailable;
    bool enabled = false;
    bool persistence_pending = false;
};

struct PetToolSnapshot {
    int32_t bond_points = 0;
    BondLevel bond_level = BondLevel::kAcquainted;
    uint32_t companion_days = 0;
    PetState mood = PetState::kIdle;
    int battery_level = -1;
    bool charging = false;
    bool manual_quiet = false;
    uint8_t active_reminders = 0;
};

struct PetToolDependencies {
    std::function<InteractionToolResult(PetAction)> interact;
    std::function<PetToolSnapshot()> get_status;
    std::function<QuietToolResult(bool)> set_quiet;
};

// McpServer validates required field types and schedules callbacks onto the application main task.
// These callbacks add the action whitelist and reject unavailable or inconsistent board results.
void RegisterPetTools(McpServer& server, PetToolDependencies dependencies);

}  // namespace maomi
