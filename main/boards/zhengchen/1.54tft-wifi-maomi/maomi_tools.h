#pragma once

#include "maomi_bond.h"
#include "maomi_pet_core.h"
#include "maomi_reminders.h"

#include <cstdint>
#include <functional>
#include <string_view>

class McpServer;

namespace maomi {

inline constexpr char kPetInteractToolName[] = "self.pet.interact";
inline constexpr char kPetStartGameToolName[] = "self.pet.start_game";
inline constexpr char kPetStatusToolName[] = "self.pet.get_status";
inline constexpr char kPetQuietToolName[] = "self.pet.set_quiet";
inline constexpr char kCountdownToolName[] = "self.timer.start_countdown";
inline constexpr char kAlarmToolName[] = "self.alarm.set";
inline constexpr char kIntervalReminderToolName[] = "self.reminder.start_interval";
inline constexpr char kPomodoroToolName[] = "self.pomodoro.start";
inline constexpr char kReminderListToolName[] = "self.reminder.list";
inline constexpr char kReminderCancelToolName[] = "self.reminder.cancel";

enum class PetAction : uint8_t {
    kPet,
    kFeed,
    kPlay,
};

enum class VoiceGame : uint8_t {
    kCatGuess,
    kMiniAdventure,
    kStoryChain,
    kCatDetective,
    kMemorySuitcase,
    kQuickQuiz,
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
    std::function<InteractionToolResult(VoiceGame)> start_game;
    std::function<PetToolSnapshot()> get_status;
    std::function<QuietToolResult(bool)> set_quiet;
};

struct ReminderToolDependencies {
    std::function<ReminderResult(uint32_t, std::string_view)> start_countdown;
    std::function<ReminderResult(const DateTime&, std::string_view)> set_alarm;
    std::function<ReminderResult(ReminderKind, uint32_t, std::string_view)> start_interval;
    std::function<ReminderResult(uint32_t, uint32_t, uint32_t)> start_pomodoro;
    std::function<ReminderResult(uint16_t)> cancel;
    std::function<ReminderList()> list;
};

// McpServer validates required field types and schedules callbacks onto the application main task.
// These callbacks add the action whitelist and reject unavailable or inconsistent board results.
void RegisterPetTools(McpServer& server, PetToolDependencies dependencies);

// Registers local reminder operations. McpServer runs callbacks on the application main task;
// every dependency must report the engine's real result instead of optimistic success.
void RegisterReminderTools(McpServer& server, ReminderToolDependencies dependencies);

}  // namespace maomi
