#pragma once

#include "maomi_pet_core.h"

#include <cstddef>
#include <cstdint>

namespace maomi {

constexpr size_t kPetUiStateCount = 11;
constexpr uint8_t kMaxUiAnimationFrames = 4;
constexpr uint8_t kUiFrameCacheLimit = 2;
constexpr uint16_t kMinimumUiFrameIntervalMs = 100;

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

// Pure mapping policy: no LVGL calls, allocations, or image decoding. Call Resolve from the
// application main task (PetCore observers already run there), then pass display_emotion to the
// existing display only when surface is kPet.
class UiMapper {
public:
    UiRenderPlan Resolve(const Snapshot& snapshot, bool high_temperature,
                         const UiAssetCatalog& assets) const;

private:
    static SystemOverlay OverlayFor(DeviceState state);
};

}  // namespace maomi
