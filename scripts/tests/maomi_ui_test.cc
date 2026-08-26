#include "maomi_ui.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
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

maomi::PowerUiSample PowerSample(int battery_level, bool external_power_connected,
                                 bool battery_level_valid = true) {
    maomi::PowerUiSample sample;
    sample.battery_level = battery_level;
    sample.battery_level_valid = battery_level_valid;
    sample.external_power_connected = external_power_connected;
    return sample;
}

void TestPowerUiWaitsForStableBatteryData() {
    maomi::PowerUiPolicy policy;

    const auto unplugged = policy.Update(PowerSample(0, false, false));
    CHECK(unplugged.mode == maomi::PowerUiMode::kNormal);
    CHECK(unplugged.override_pet_state);
    CHECK(unplugged.pet_state == maomi::PetState::kIdle);
    CHECK(unplugged.allow_autonomous_audio);

    const auto plugged = policy.Update(PowerSample(100, true, false));
    CHECK(plugged.mode == maomi::PowerUiMode::kNormal);
    CHECK(plugged.override_pet_state);
    CHECK(plugged.pet_state == maomi::PetState::kIdle);
    CHECK(plugged.allow_autonomous_audio);
}

void TestLowBatteryUsesTwentyToTwentyFivePercentHysteresis() {
    maomi::PowerUiPolicy policy;
    constexpr std::array<int, 6> kLevels = {21, 20, 21, 19, 22, 25};
    constexpr std::array<maomi::PowerUiMode, 6> kModes = {
        maomi::PowerUiMode::kNormal,     maomi::PowerUiMode::kLowBattery,
        maomi::PowerUiMode::kLowBattery, maomi::PowerUiMode::kLowBattery,
        maomi::PowerUiMode::kLowBattery, maomi::PowerUiMode::kNormal,
    };

    for (size_t i = 0; i < kLevels.size(); ++i) {
        const auto decision = policy.Update(PowerSample(kLevels[i], false));
        CHECK(decision.mode == kModes[i]);
        CHECK(decision.allow_autonomous_audio == (kModes[i] == maomi::PowerUiMode::kNormal));
        if (kModes[i] == maomi::PowerUiMode::kLowBattery) {
            CHECK(decision.override_pet_state);
            CHECK(decision.pet_state == maomi::PetState::kLowBattery);
        }
    }
}

void TestExternalPowerDeterministicallyMapsChargingFullAndUnplugged() {
    maomi::PowerUiPolicy policy;

    const auto charging = policy.Update(PowerSample(80, true));
    CHECK(charging.mode == maomi::PowerUiMode::kCharging);
    CHECK(charging.override_pet_state);
    CHECK(charging.pet_state == maomi::PetState::kCharging);
    CHECK(!charging.allow_autonomous_audio);

    const auto unplugged = policy.Update(PowerSample(80, false));
    CHECK(unplugged.mode == maomi::PowerUiMode::kNormal);
    CHECK(!unplugged.override_pet_state);
    CHECK(unplugged.allow_autonomous_audio);

    const auto full = policy.Update(PowerSample(100, true));
    CHECK(full.mode == maomi::PowerUiMode::kFull);
    CHECK(full.override_pet_state);
    CHECK(full.pet_state == maomi::PetState::kFull);
    CHECK(!full.allow_autonomous_audio);

    const auto full_but_unplugged = policy.Update(PowerSample(100, false));
    CHECK(full_but_unplugged.mode == maomi::PowerUiMode::kNormal);
    CHECK(!full_but_unplugged.override_pet_state);
    CHECK(full_but_unplugged.allow_autonomous_audio);
}

void TestChargingOverridesLowBatteryWithoutClearingItsLatch() {
    maomi::PowerUiPolicy policy;

    CHECK(policy.Update(PowerSample(20, false)).mode == maomi::PowerUiMode::kLowBattery);
    CHECK(policy.Update(PowerSample(20, true)).mode == maomi::PowerUiMode::kCharging);
    CHECK(policy.Update(PowerSample(21, false)).mode == maomi::PowerUiMode::kLowBattery);
    CHECK(policy.Update(PowerSample(25, false)).mode == maomi::PowerUiMode::kNormal);
}

void TestOfficialErrorAndHighTemperaturePreemptChargingPresentation() {
    FakeAssetCatalog assets;
    assets.Add("maomi_charge.gif");
    maomi::PowerUiPolicy power_policy;
    maomi::UiMapper mapper;
    const auto charging = power_policy.Update(PowerSample(80, true));

    auto fatal = IdleSnapshot(maomi::PetState::kIdle);
    fatal.official_state = kDeviceStateFatalError;
    fatal.paused_by_official_state = true;
    const auto fatal_plan = mapper.Resolve(fatal, charging, false, assets);
    CHECK(fatal_plan.surface == maomi::UiSurface::kOfficial);
    CHECK(fatal_plan.overlay == maomi::SystemOverlay::kFatalError);

    const auto hot_plan =
        mapper.Resolve(IdleSnapshot(maomi::PetState::kIdle), charging, true, assets);
    CHECK(hot_plan.surface == maomi::UiSurface::kOfficial);
    CHECK(hot_plan.overlay == maomi::SystemOverlay::kHighTemperature);
}

void TestPowerPresentationOverridesPetCorePowerEdgeCases() {
    FakeAssetCatalog assets;
    assets.Add("maomi_charge.gif");
    maomi::PowerUiPolicy power_policy;
    maomi::UiMapper mapper;

    auto stale_low_battery = IdleSnapshot(maomi::PetState::kLowBattery);
    const auto charging = power_policy.Update(PowerSample(20, true));
    const auto charging_plan = mapper.Resolve(stale_low_battery, charging, false, assets);
    CHECK(charging_plan.surface == maomi::UiSurface::kPet);
    CHECK(charging_plan.pet_state == maomi::PetState::kCharging);
    CHECK(std::strcmp(charging_plan.display_emotion, "maomi_charge") == 0);

    const auto unstable = power_policy.Update(PowerSample(0, false, false));
    const auto unstable_plan = mapper.Resolve(stale_low_battery, unstable, false, assets);
    CHECK(unstable_plan.surface == maomi::UiSurface::kPet);
    CHECK(unstable_plan.pet_state == maomi::PetState::kIdle);
    CHECK(std::strcmp(unstable_plan.display_emotion, "neutral") == 0);
}

void TestAnimationRefreshIsSinglePendingAndRateLimited() {
    static_assert(!std::is_copy_constructible_v<maomi::AnimationRefreshGate>);

    FakeAssetCatalog assets;
    assets.Add("maomi_look.gif");
    maomi::UiMapper mapper;
    maomi::AnimationRefreshGate gate;
    const auto curious = mapper.Resolve(IdleSnapshot(maomi::PetState::kCurious), false, assets);

    gate.Configure(curious);
    CHECK(gate.RequestFromTimer(0) == maomi::RefreshRequest::kAccepted);
    for (int i = 0; i < 1000; ++i) {
        CHECK(gate.RequestFromTimer(0) == maomi::RefreshRequest::kCoalesced);
    }
    auto snapshot = gate.GetSnapshot();
    CHECK(snapshot.active);
    CHECK(snapshot.pending_depth == 1);
    CHECK(snapshot.maximum_pending_depth == 1);
    CHECK(snapshot.accepted == 1);
    CHECK(snapshot.coalesced == 1000);

    CHECK(gate.ConsumeOnMainTask());
    CHECK(!gate.ConsumeOnMainTask());
    CHECK(gate.RequestFromTimer(curious.minimum_frame_interval_ms - 1) ==
          maomi::RefreshRequest::kRateLimited);
    CHECK(gate.RequestFromTimer(curious.minimum_frame_interval_ms) ==
          maomi::RefreshRequest::kAccepted);
}

void TestOfficialPreemptionCancelsAnimationRefresh() {
    FakeAssetCatalog assets;
    assets.Add("maomi_sleep.gif");
    maomi::UiMapper mapper;
    maomi::AnimationRefreshGate gate;

    const auto sleeping = mapper.Resolve(IdleSnapshot(maomi::PetState::kSleeping), false, assets);
    gate.Configure(sleeping);
    CHECK(gate.RequestFromTimer(1000) == maomi::RefreshRequest::kAccepted);
    CHECK(gate.GetSnapshot().pending_depth == 1);

    auto listening = IdleSnapshot(maomi::PetState::kSleeping);
    listening.official_state = kDeviceStateListening;
    listening.paused_by_official_state = true;
    gate.Configure(mapper.Resolve(listening, false, assets));
    CHECK(!gate.GetSnapshot().active);
    CHECK(gate.GetSnapshot().pending_depth == 0);
    CHECK(!gate.ConsumeOnMainTask());
    CHECK(gate.RequestFromTimer(2000) == maomi::RefreshRequest::kInactive);
}

void TestSwitchingEqualRateAnimationsDropsStaleRefresh() {
    FakeAssetCatalog assets;
    assets.Add("maomi_look.gif");
    assets.Add("maomi_charge.gif");
    maomi::UiMapper mapper;
    maomi::AnimationRefreshGate gate;

    const auto curious = mapper.Resolve(IdleSnapshot(maomi::PetState::kCurious), false, assets);
    const auto charging = mapper.Resolve(IdleSnapshot(maomi::PetState::kCharging), false, assets);
    CHECK(curious.minimum_frame_interval_ms == charging.minimum_frame_interval_ms);

    gate.Configure(curious);
    CHECK(gate.RequestFromTimer(1000) == maomi::RefreshRequest::kAccepted);
    gate.Configure(charging);
    CHECK(gate.GetSnapshot().pending_depth == 0);
    CHECK(gate.RequestFromTimer(1000) == maomi::RefreshRequest::kAccepted);
}

void TestEightHourAnimationLoadHasFixedRefreshBound() {
    constexpr uint64_t kEightHoursMs = 8ULL * 60 * 60 * 1000;
    constexpr uint64_t kTimerPeriodMs = 10;

    maomi::UiRenderPlan fastest_allowed;
    fastest_allowed.surface = maomi::UiSurface::kPet;
    fastest_allowed.animated = true;
    fastest_allowed.minimum_frame_interval_ms = maomi::kMinimumUiFrameIntervalMs;

    maomi::AnimationRefreshGate gate;
    gate.Configure(fastest_allowed);
    for (uint64_t now_ms = 0; now_ms < kEightHoursMs; now_ms += kTimerPeriodMs) {
        if (gate.RequestFromTimer(now_ms) == maomi::RefreshRequest::kAccepted) {
            CHECK(gate.GetSnapshot().pending_depth == 1);
            CHECK(gate.ConsumeOnMainTask());
        }
    }

    const auto snapshot = gate.GetSnapshot();
    const uint64_t maximum_refreshes = kEightHoursMs * maomi::kMaximumUiAnimationFps / 1000;
    CHECK(snapshot.accepted <= maximum_refreshes);
    CHECK(snapshot.consumed == snapshot.accepted);
    CHECK(snapshot.pending_depth == 0);
    CHECK(snapshot.maximum_pending_depth == 1);
}

}  // namespace

int main() {
    TestEveryPetStateMapsToKnownBoundedResource();
    TestMissingNonCriticalAssetsAlwaysUseOfficialFallbacks();
    TestOfficialAndHighTemperatureSurfacesPreemptWithoutAssetLookup();
    TestInvalidOrInconsistentStateFailsClosed();
    TestPowerUiWaitsForStableBatteryData();
    TestLowBatteryUsesTwentyToTwentyFivePercentHysteresis();
    TestExternalPowerDeterministicallyMapsChargingFullAndUnplugged();
    TestChargingOverridesLowBatteryWithoutClearingItsLatch();
    TestOfficialErrorAndHighTemperaturePreemptChargingPresentation();
    TestPowerPresentationOverridesPetCorePowerEdgeCases();
    TestAnimationRefreshIsSinglePendingAndRateLimited();
    TestOfficialPreemptionCancelsAnimationRefresh();
    TestSwitchingEqualRateAnimationsDropsStaleRefresh();
    TestEightHourAnimationLoadHasFixedRefreshBound();
    std::cout << "maomi ui mapping tests passed" << std::endl;
    return 0;
}
