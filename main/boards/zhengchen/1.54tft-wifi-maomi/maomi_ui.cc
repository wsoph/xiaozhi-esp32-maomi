#include "maomi_ui.h"

#include <array>
#include <limits>

namespace maomi {
namespace {

struct StateResource {
    PetState state;
    const char* asset_file;
    const char* custom_emotion;
    const char* fallback_emotion;
    bool animated;
    uint8_t frame_count;
    uint16_t minimum_frame_interval_ms;
};

constexpr std::array<StateResource, kPetUiStateCount> kStateResources = {{
    {PetState::kIdle, "neutral.png", "neutral", "neutral", false, 1, 1000},
    {PetState::kCurious, "maomi_look.gif", "maomi_look", "thinking", true, 4, 420},
    {PetState::kBlinking, "maomi_blink.gif", "maomi_blink", "winking", true, 3, 140},
    {PetState::kSleepy, "sleepy.png", "sleepy", "sleepy", false, 1, 1000},
    {PetState::kSleeping, "maomi_sleep.gif", "maomi_sleep", "sleepy", true, 4, 700},
    {PetState::kHappy, "happy.png", "happy", "happy", false, 1, 1000},
    {PetState::kBeingPetted, "maomi_pet.gif", "maomi_pet", "loving", true, 4, 300},
    {PetState::kEating, "maomi_eat.gif", "maomi_eat", "delicious", true, 4, 260},
    {PetState::kPlaying, "maomi_play.gif", "maomi_play", "funny", true, 4, 300},
    {PetState::kCharging, "maomi_charge.gif", "maomi_charge", "relaxed", true, 4, 420},
    {PetState::kFull, "relaxed.png", "relaxed", "relaxed", false, 1, 1000},
    {PetState::kLowBattery, "maomi_low_battery.gif", "maomi_low_battery", "sad", true, 4, 360},
    {PetState::kReminding, "maomi_reminder.gif", "maomi_reminder", "surprised", true, 4, 220},
}};

const StateResource& ResourceFor(PetState state) {
    const auto index = static_cast<size_t>(state);
    if (index >= kStateResources.size() || kStateResources[index].state != state) {
        return kStateResources[0];
    }
    return kStateResources[index];
}

UiRenderPlan OfficialPlan(SystemOverlay overlay) {
    UiRenderPlan plan;
    plan.surface = UiSurface::kOfficial;
    plan.overlay = overlay;
    return plan;
}

void SaturatingIncrement(uint64_t* value) {
    if (*value < std::numeric_limits<uint64_t>::max()) {
        ++*value;
    }
}

}  // namespace

PowerUiDecision PowerUiPolicy::Update(const PowerUiSample& sample) {
    if (!sample.battery_level_valid || sample.battery_level < 0 || sample.battery_level > 100) {
        return {
            .mode = PowerUiMode::kNormal,
            .pet_state = PetState::kIdle,
            .override_pet_state = true,
            .allow_autonomous_audio = !sample.external_power_connected,
        };
    }

    if (sample.battery_level <= kLowBatteryEnterPercent) {
        low_battery_latched_ = true;
    } else if (sample.battery_level >= kLowBatteryExitPercent) {
        low_battery_latched_ = false;
    }

    if (sample.external_power_connected) {
        const bool full = sample.battery_level == 100;
        return {
            .mode = full ? PowerUiMode::kFull : PowerUiMode::kCharging,
            .pet_state = full ? PetState::kFull : PetState::kCharging,
            .override_pet_state = true,
            .allow_autonomous_audio = false,
        };
    }
    if (low_battery_latched_) {
        return {
            .mode = PowerUiMode::kLowBattery,
            .pet_state = PetState::kLowBattery,
            .override_pet_state = true,
            .allow_autonomous_audio = false,
        };
    }
    return {};
}

UiRenderPlan UiMapper::Resolve(const Snapshot& snapshot, bool high_temperature,
                               const UiAssetCatalog& assets) const {
    return Resolve(snapshot, PowerUiDecision{}, high_temperature, assets);
}

UiRenderPlan UiMapper::Resolve(const Snapshot& snapshot, const PowerUiDecision& power,
                               bool high_temperature, const UiAssetCatalog& assets) const {
    if (high_temperature) {
        return OfficialPlan(SystemOverlay::kHighTemperature);
    }

    const auto overlay = OverlayFor(snapshot.official_state);
    if (snapshot.paused_by_official_state || overlay != SystemOverlay::kNone) {
        return OfficialPlan(overlay == SystemOverlay::kNone ? SystemOverlay::kUnknown : overlay);
    }

    const auto& resource = ResourceFor(power.override_pet_state ? power.pet_state : snapshot.state);
    const bool available = assets.HasAsset(resource.asset_file);

    UiRenderPlan plan;
    plan.surface = UiSurface::kPet;
    plan.overlay = SystemOverlay::kNone;
    plan.pet_state = resource.state;
    plan.asset_file = resource.asset_file;
    plan.display_emotion = available ? resource.custom_emotion : resource.fallback_emotion;
    plan.fallback_emotion = resource.fallback_emotion;
    plan.using_custom_asset = available;
    plan.animated = available && resource.animated;
    plan.frame_count = plan.animated ? resource.frame_count : 1;
    plan.frame_cache_limit =
        plan.frame_count > kUiFrameCacheLimit ? kUiFrameCacheLimit : plan.frame_count;
    plan.minimum_frame_interval_ms =
        plan.animated ? resource.minimum_frame_interval_ms : kMinimumUiFrameIntervalMs;
    return plan;
}

SystemOverlay UiMapper::OverlayFor(DeviceState state) {
    switch (state) {
        case kDeviceStateIdle:
            return SystemOverlay::kNone;
        case kDeviceStateStarting:
            return SystemOverlay::kStarting;
        case kDeviceStateWifiConfiguring:
            return SystemOverlay::kWifiConfiguring;
        case kDeviceStateConnecting:
            return SystemOverlay::kConnecting;
        case kDeviceStateListening:
            return SystemOverlay::kListening;
        case kDeviceStateSpeaking:
            return SystemOverlay::kSpeaking;
        case kDeviceStateNotifying:
            return SystemOverlay::kNotifying;
        case kDeviceStateUpgrading:
            return SystemOverlay::kUpgrading;
        case kDeviceStateActivating:
            return SystemOverlay::kActivating;
        case kDeviceStateAudioTesting:
            return SystemOverlay::kAudioTesting;
        case kDeviceStateFatalError:
            return SystemOverlay::kFatalError;
        case kDeviceStateUnknown:
        default:
            return SystemOverlay::kUnknown;
    }
}

void AnimationRefreshGate::Configure(const UiRenderPlan& plan) {
    const bool active = plan.surface == UiSurface::kPet && plan.animated;
    const uint16_t interval = plan.minimum_frame_interval_ms < kMinimumUiFrameIntervalMs
                                  ? kMinimumUiFrameIntervalMs
                                  : plan.minimum_frame_interval_ms;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!active) {
        snapshot_.active = false;
        snapshot_.pending_depth = 0;
        has_last_accepted_ = false;
        animation_state_ = PetState::kIdle;
        return;
    }
    if (!snapshot_.active || snapshot_.minimum_interval_ms != interval ||
        animation_state_ != plan.pet_state) {
        snapshot_.pending_depth = 0;
        has_last_accepted_ = false;
    }
    snapshot_.active = true;
    snapshot_.minimum_interval_ms = interval;
    animation_state_ = plan.pet_state;
}

RefreshRequest AnimationRefreshGate::RequestFromTimer(uint64_t monotonic_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    SaturatingIncrement(&snapshot_.requested);
    if (!snapshot_.active) {
        return RefreshRequest::kInactive;
    }
    if (snapshot_.pending_depth != 0) {
        SaturatingIncrement(&snapshot_.coalesced);
        return RefreshRequest::kCoalesced;
    }
    if (has_last_accepted_ && (monotonic_ms < last_accepted_ms_ ||
                               monotonic_ms - last_accepted_ms_ < snapshot_.minimum_interval_ms)) {
        SaturatingIncrement(&snapshot_.rate_limited);
        return RefreshRequest::kRateLimited;
    }

    has_last_accepted_ = true;
    last_accepted_ms_ = monotonic_ms;
    snapshot_.pending_depth = 1;
    snapshot_.maximum_pending_depth = 1;
    SaturatingIncrement(&snapshot_.accepted);
    return RefreshRequest::kAccepted;
}

bool AnimationRefreshGate::ConsumeOnMainTask() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.pending_depth == 0) {
        return false;
    }
    snapshot_.pending_depth = 0;
    SaturatingIncrement(&snapshot_.consumed);
    return true;
}

AnimationGateSnapshot AnimationRefreshGate::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

}  // namespace maomi
