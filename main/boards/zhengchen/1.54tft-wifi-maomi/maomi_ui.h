#pragma once

#include "maomi_pet_core.h"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace maomi {

constexpr size_t kPetUiStateCount = 11;
constexpr uint8_t kMaxUiAnimationFrames = 4;
constexpr uint8_t kUiFrameCacheLimit = 2;
constexpr uint8_t kMaximumUiAnimationFps = 10;
constexpr uint16_t kMinimumUiFrameIntervalMs = 100;
constexpr int kLowBatteryEnterPercent = 20;
constexpr int kLowBatteryExitPercent = 25;

enum class UiSurface : uint8_t {
    kPet,
    kOfficial,
};

enum class SystemOverlay : uint8_t {
    kNone,
    kUnknown,
    kStarting,
    kWifiConfiguring,
    kConnecting,
    kListening,
    kSpeaking,
    kNotifying,
    kUpgrading,
    kActivating,
    kAudioTesting,
    kFatalError,
    kHighTemperature,
};

class UiAssetCatalog {
public:
    virtual ~UiAssetCatalog() = default;
    virtual bool HasAsset(const char* filename) const = 0;
};

// Fixed-size output consumed by the board's main-task display adapter. It owns no image data;
// asset decoding remains in the existing display layer, which caches at most its current and
// next frame.
struct UiRenderPlan {
    UiSurface surface = UiSurface::kOfficial;
    SystemOverlay overlay = SystemOverlay::kUnknown;
    PetState pet_state = PetState::kIdle;
    const char* asset_file = "";
    const char* display_emotion = "neutral";
    const char* fallback_emotion = "neutral";
    bool using_custom_asset = false;
    bool animated = false;
    uint8_t frame_count = 1;
    uint8_t frame_cache_limit = 1;
    uint16_t minimum_frame_interval_ms = kMinimumUiFrameIntervalMs;
};

enum class PowerUiMode : uint8_t {
    kNormal,
    kLowBattery,
    kCharging,
    kFull,
};

struct PowerUiSample {
    int battery_level = -1;
    bool battery_level_valid = false;
    bool external_power_connected = false;
};

struct PowerUiDecision {
    PowerUiMode mode = PowerUiMode::kNormal;
    PetState pet_state = PetState::kIdle;
    bool override_pet_state = false;
    bool allow_autonomous_audio = true;
};

// Stateful presentation policy only. Hardware sampling and protection remain owned by the
// board's original PowerManager.
class PowerUiPolicy {
public:
    PowerUiDecision Update(const PowerUiSample& sample);

private:
    bool low_battery_latched_ = false;
};

// Pure mapping policy: no LVGL calls, allocations, or image decoding. Call Resolve from the
// application main task (PetCore observers already run there), then pass display_emotion to the
// existing display only when surface is kPet.
class UiMapper {
public:
    UiRenderPlan Resolve(const Snapshot& snapshot, bool high_temperature,
                         const UiAssetCatalog& assets) const;
    UiRenderPlan Resolve(const Snapshot& snapshot, const PowerUiDecision& power,
                         bool high_temperature, const UiAssetCatalog& assets) const;

private:
    static SystemOverlay OverlayFor(DeviceState state);
};

enum class RefreshRequest : uint8_t {
    kAccepted,
    kInactive,
    kRateLimited,
    kCoalesced,
};

struct AnimationGateSnapshot {
    bool active = false;
    uint8_t pending_depth = 0;
    uint8_t maximum_pending_depth = 0;
    uint16_t minimum_interval_ms = kMinimumUiFrameIntervalMs;
    uint64_t requested = 0;
    uint64_t accepted = 0;
    uint64_t rate_limited = 0;
    uint64_t coalesced = 0;
    uint64_t consumed = 0;
};

// Timer callbacks call RequestFromTimer only. kAccepted authorizes one application-main-task
// refresh; that task calls ConsumeOnMainTask after handling it. The single pending bit replaces
// an animation queue, so a slow display cannot cause unbounded memory or callback growth.
class AnimationRefreshGate {
public:
    AnimationRefreshGate() = default;
    AnimationRefreshGate(const AnimationRefreshGate&) = delete;
    AnimationRefreshGate& operator=(const AnimationRefreshGate&) = delete;

    void Configure(const UiRenderPlan& plan);
    RefreshRequest RequestFromTimer(uint64_t monotonic_ms);
    bool ConsumeOnMainTask();
    AnimationGateSnapshot GetSnapshot() const;

private:
    mutable std::mutex mutex_;
    AnimationGateSnapshot snapshot_;
    uint64_t last_accepted_ms_ = 0;
    bool has_last_accepted_ = false;
    PetState animation_state_ = PetState::kIdle;
};

}  // namespace maomi
