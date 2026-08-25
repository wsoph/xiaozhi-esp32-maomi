#include "maomi_ui.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>

namespace {

[[noreturn]] void Fail(const char* expression, int line) {
    std::cerr << "FAILED line " << line << ": " << expression << std::endl;
    std::exit(1);
}

#define CHECK(expression)                \
    do {                                 \
        if (!(expression)) {             \
            Fail(#expression, __LINE__); \
        }                                \
    } while (false)

class FakeAssetCatalog final : public maomi::UiAssetCatalog {
public:
    bool HasAsset(const char* filename) const override {
        ++lookup_count_;
        return filename != nullptr && available_.find(filename) != available_.end();
    }

    void Add(const char* filename) { available_.insert(filename); }
    size_t lookup_count() const { return lookup_count_; }

private:
    std::unordered_set<std::string> available_;
    mutable size_t lookup_count_ = 0;
};

struct ExpectedMapping {
    maomi::PetState state;
    const char* asset_file;
    const char* display_emotion;
    const char* fallback_emotion;
    bool animated;
    uint8_t frame_count;
};

constexpr std::array<ExpectedMapping, maomi::kPetUiStateCount> kExpectedMappings = {{
    {maomi::PetState::kIdle, "neutral.png", "neutral", "neutral", false, 1},
    {maomi::PetState::kCurious, "maomi_look.gif", "maomi_look", "thinking", true, 4},
    {maomi::PetState::kSleepy, "sleepy.png", "sleepy", "sleepy", false, 1},
    {maomi::PetState::kSleeping, "maomi_sleep.gif", "maomi_sleep", "sleepy", true, 4},
    {maomi::PetState::kHappy, "happy.png", "happy", "happy", false, 1},
    {maomi::PetState::kEating, "maomi_eat.gif", "maomi_eat", "delicious", true, 4},
    {maomi::PetState::kPlaying, "maomi_play.gif", "maomi_play", "funny", true, 4},
    {maomi::PetState::kCharging, "maomi_charge.gif", "maomi_charge", "relaxed", true, 4},
    {maomi::PetState::kFull, "relaxed.png", "relaxed", "relaxed", false, 1},
    {maomi::PetState::kLowBattery, "maomi_low_battery.gif", "maomi_low_battery", "sad", true, 4},
    {maomi::PetState::kReminding, "maomi_reminder.gif", "maomi_reminder", "surprised", true, 4},
}};

maomi::Snapshot IdleSnapshot(maomi::PetState state) {
    maomi::Snapshot snapshot;
    snapshot.state = state;
    snapshot.official_state = kDeviceStateIdle;
    snapshot.paused_by_official_state = false;
    return snapshot;
}

void TestEveryPetStateMapsToKnownBoundedResource() {
    FakeAssetCatalog assets;
    for (const auto& expected : kExpectedMappings) {
        assets.Add(expected.asset_file);
    }

    maomi::UiMapper mapper;
    for (const auto& expected : kExpectedMappings) {
        const auto plan = mapper.Resolve(IdleSnapshot(expected.state), false, assets);
        CHECK(plan.surface == maomi::UiSurface::kPet);
        CHECK(plan.overlay == maomi::SystemOverlay::kNone);
        CHECK(plan.pet_state == expected.state);
        CHECK(plan.asset_file != nullptr);
        CHECK(plan.display_emotion != nullptr);
        CHECK(plan.fallback_emotion != nullptr);
        CHECK(std::strcmp(plan.asset_file, expected.asset_file) == 0);
        CHECK(std::strcmp(plan.display_emotion, expected.display_emotion) == 0);
        CHECK(std::strcmp(plan.fallback_emotion, expected.fallback_emotion) == 0);
        CHECK(plan.using_custom_asset);
        CHECK(plan.animated == expected.animated);
        CHECK(plan.frame_count == expected.frame_count);
        CHECK(plan.frame_count >= 1);
        CHECK(plan.frame_count <= maomi::kMaxUiAnimationFrames);
        CHECK(plan.frame_cache_limit <= maomi::kUiFrameCacheLimit);
        CHECK(plan.minimum_frame_interval_ms >= maomi::kMinimumUiFrameIntervalMs);
    }
}

void TestMissingNonCriticalAssetsAlwaysUseOfficialFallbacks() {
    FakeAssetCatalog no_assets;
    maomi::UiMapper mapper;

    for (const auto& expected : kExpectedMappings) {
        const auto plan = mapper.Resolve(IdleSnapshot(expected.state), false, no_assets);
        CHECK(plan.surface == maomi::UiSurface::kPet);
        CHECK(!plan.using_custom_asset);
        CHECK(!plan.animated);
        CHECK(plan.frame_count == 1);
        CHECK(plan.frame_cache_limit == 1);
        CHECK(plan.display_emotion != nullptr);
        CHECK(std::strcmp(plan.display_emotion, expected.fallback_emotion) == 0);
    }
}

struct ExpectedOverlay {
    DeviceState state;
    maomi::SystemOverlay overlay;
};

constexpr std::array<ExpectedOverlay, 10> kExpectedOfficialOverlays = {{
    {kDeviceStateStarting, maomi::SystemOverlay::kStarting},
    {kDeviceStateWifiConfiguring, maomi::SystemOverlay::kWifiConfiguring},
    {kDeviceStateConnecting, maomi::SystemOverlay::kConnecting},
    {kDeviceStateListening, maomi::SystemOverlay::kListening},
    {kDeviceStateSpeaking, maomi::SystemOverlay::kSpeaking},
    {kDeviceStateNotifying, maomi::SystemOverlay::kNotifying},
    {kDeviceStateUpgrading, maomi::SystemOverlay::kUpgrading},
    {kDeviceStateActivating, maomi::SystemOverlay::kActivating},
    {kDeviceStateAudioTesting, maomi::SystemOverlay::kAudioTesting},
    {kDeviceStateFatalError, maomi::SystemOverlay::kFatalError},
}};

void TestOfficialAndHighTemperatureSurfacesPreemptWithoutAssetLookup() {
    FakeAssetCatalog assets;
    maomi::UiMapper mapper;

    for (const auto& expected : kExpectedOfficialOverlays) {
        auto snapshot = IdleSnapshot(maomi::PetState::kHappy);
        snapshot.official_state = expected.state;
        snapshot.paused_by_official_state = true;
        const size_t lookups_before = assets.lookup_count();
        const auto plan = mapper.Resolve(snapshot, false, assets);
        CHECK(plan.surface == maomi::UiSurface::kOfficial);
        CHECK(plan.overlay == expected.overlay);
        CHECK(plan.display_emotion != nullptr);
        CHECK(plan.asset_file != nullptr);
        CHECK(assets.lookup_count() == lookups_before);
    }

    auto idle = IdleSnapshot(maomi::PetState::kHappy);
    const size_t lookups_before = assets.lookup_count();
    const auto hot = mapper.Resolve(idle, true, assets);
    CHECK(hot.surface == maomi::UiSurface::kOfficial);
    CHECK(hot.overlay == maomi::SystemOverlay::kHighTemperature);
    CHECK(assets.lookup_count() == lookups_before);
}

void TestInvalidOrInconsistentStateFailsClosed() {
    FakeAssetCatalog no_assets;
    maomi::UiMapper mapper;

    auto invalid_pet = IdleSnapshot(static_cast<maomi::PetState>(255));
    const auto fallback = mapper.Resolve(invalid_pet, false, no_assets);
    CHECK(fallback.surface == maomi::UiSurface::kPet);
    CHECK(fallback.pet_state == maomi::PetState::kIdle);
    CHECK(std::strcmp(fallback.display_emotion, "neutral") == 0);

    auto inconsistent = IdleSnapshot(maomi::PetState::kHappy);
    inconsistent.paused_by_official_state = true;
    const auto blocked = mapper.Resolve(inconsistent, false, no_assets);
    CHECK(blocked.surface == maomi::UiSurface::kOfficial);
    CHECK(blocked.overlay == maomi::SystemOverlay::kUnknown);

    auto unknown = IdleSnapshot(maomi::PetState::kHappy);
    unknown.official_state = kDeviceStateUnknown;
    const auto unknown_plan = mapper.Resolve(unknown, false, no_assets);
    CHECK(unknown_plan.surface == maomi::UiSurface::kOfficial);
    CHECK(unknown_plan.overlay == maomi::SystemOverlay::kUnknown);
}

}  // namespace

int main() {
    TestEveryPetStateMapsToKnownBoundedResource();
    TestMissingNonCriticalAssetsAlwaysUseOfficialFallbacks();
    TestOfficialAndHighTemperatureSurfacesPreemptWithoutAssetLookup();
    TestInvalidOrInconsistentStateFailsClosed();
    std::cout << "maomi ui mapping tests passed" << std::endl;
    return 0;
}
