#include "../1.54tft-wifi/power_manager.h"
#include "../1.54tft-wifi/zhengchen_lcd_display.h"
#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "led/single_led.h"
#include "maomi_autonomy.h"
#include "maomi_bond.h"
#include "maomi_clock.h"
#include "maomi_interaction_audio_policy.h"
#include "maomi_pet_core.h"
#include "maomi_reminders.h"
#include "maomi_storage.h"
#include "maomi_tools.h"
#include "maomi_ui.h"
#include "maomi_variant.h"
#include "maomi_wake.h"
#include "mcp_server.h"
#include "power_save_timer.h"
#include "system_reset.h"
#include "wifi_board.h"

#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include <atomic>
#include <ctime>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <utility>

#define TAG "ZHENGCHEN_1_54TFT_WIFI_MAOMI"

namespace {

constexpr uint8_t kMaomiBatteryWarmupSamples = 4;
constexpr float kHighTemperatureThresholdCelsius = 75.0f;
constexpr uint32_t kMaomiAutonomyRandomSeed = 0x4D414F4Du;
constexpr uint64_t kMaomiInteractionVisibleDurationMs = 4'000;
constexpr uint64_t kMaomiInteractionSoundMaxWaitMs = 15'000;
constexpr uint64_t kMaomiReminderVisibleDurationMs = 4'000;
constexpr char kMaomiReminderSoundName[] = "maomi_prompt.ogg";

class FirmwareUiAssetCatalog final : public maomi::UiAssetCatalog {
public:
    bool HasAsset(const char* filename) const override {
        if (filename == nullptr || filename[0] == '\0') {
            return false;
        }
        void* data = nullptr;
        size_t size = 0;
        return Assets::GetInstance().GetAssetData(filename, data, size) && data != nullptr &&
               size != 0;
    }
};

}  // namespace

class ZHENGCHEN_1_54TFT_WIFI_MAOMI : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    ZHENGCHEN_LcdDisplay* display_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;
    PowerManager* power_manager_ = nullptr;
    MaomiVariant maomi_variant_;
    maomi::PetCore maomi_pet_core_;
    maomi::NvsStorageBackend maomi_storage_backend_;
    maomi::StateStorage maomi_storage_;
    maomi::ReliableClock maomi_clock_;
    std::unique_ptr<maomi::BondTracker> maomi_bond_;
    std::unique_ptr<maomi::ReminderEngine> maomi_reminders_;
    maomi::AutonomyController maomi_autonomy_{kMaomiAutonomyRandomSeed};
    maomi::UiMapper maomi_ui_mapper_;
    maomi::PowerUiPolicy maomi_power_ui_policy_;
    FirmwareUiAssetCatalog maomi_ui_assets_;
    maomi::WakeSequence maomi_wake_;
    maomi::WakePollGate maomi_wake_poll_gate_;
    uint32_t maomi_next_playback_id_ = 0;
    uint32_t maomi_local_sound_playback_id_ = 0;
    esp_timer_handle_t maomi_poll_timer_ = nullptr;
    std::atomic<bool> maomi_policy_poll_pending_{false};
    DeviceState last_maomi_official_state_ = kDeviceStateUnknown;
    int last_maomi_battery_level_ = -1;
    uint8_t maomi_battery_warmup_samples_ = 0;
    bool maomi_external_power_connected_ = false;
    bool maomi_high_temperature_ = false;
    uint8_t maomi_poll_divider_ = 0;
    bool maomi_runtime_ready_ = false;
    bool last_maomi_time_valid_ = false;
    uint64_t maomi_last_clock_observe_second_ = UINT64_MAX;
    bool maomi_autonomy_sound_playing_ = false;
    bool maomi_local_sound_suspended_voice_processing_ = false;
    std::optional<maomi::PetAction> maomi_active_interaction_;
    uint64_t maomi_interaction_remaining_ms_ = 0;
    uint64_t maomi_interaction_last_update_ms_ = 0;
    uint64_t maomi_reminder_remaining_ms_ = 0;
    uint64_t maomi_reminder_last_update_ms_ = 0;
    std::optional<maomi::PetAction> maomi_pending_interaction_sound_;
    uint64_t maomi_pending_interaction_sound_since_ms_ = 0;
    std::string_view last_maomi_display_emotion_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    static uint64_t MonotonicMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

    void StopMaomiVoiceUpload() {
        auto& audio_service = Application::GetInstance().GetAudioService();
        audio_service.EnableVoiceProcessing(false);
        audio_service.DiscardVoiceUploadBacklog();
    }

    void SuspendMaomiVoiceUploadForLocalSound() {
        auto& app = Application::GetInstance();
        auto& audio_service = app.GetAudioService();
        if (maomi_local_sound_suspended_voice_processing_ ||
            app.GetDeviceState() != kDeviceStateListening ||
            !audio_service.IsAudioProcessorRunning()) {
            return;
        }
        StopMaomiVoiceUpload();
        maomi_local_sound_suspended_voice_processing_ = true;
    }

    void RestoreMaomiVoiceUploadAfterLocalSound() {
        if (!maomi_local_sound_suspended_voice_processing_) {
            return;
        }
        maomi_local_sound_suspended_voice_processing_ = false;
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateListening) {
            app.GetAudioService().EnableVoiceProcessing(true);
        }
    }

    uint32_t NextMaomiPlaybackId() {
        ++maomi_next_playback_id_;
        if (maomi_next_playback_id_ == 0) {
            ++maomi_next_playback_id_;
        }
        return maomi_next_playback_id_;
    }

    bool HasMaomiSound(const char* filename) const {
        void* data = nullptr;
        size_t size = 0;
        return filename != nullptr && Assets::GetInstance().GetAssetData(filename, data, size) &&
               data != nullptr && size != 0;
    }

    bool TryPlayMaomiSound(const char* filename, bool autonomy_sound) {
        if (maomi_local_sound_playback_id_ != 0 || !HasMaomiSound(filename)) {
            return false;
        }
        auto& audio_service = Application::GetInstance().GetAudioService();
        if (!audio_service.IsPlaybackIdle()) {
            return false;
        }

        void* data = nullptr;
        size_t size = 0;
        if (!Assets::GetInstance().GetAssetData(filename, data, size) || data == nullptr ||
            size == 0) {
            return false;
        }
        SuspendMaomiVoiceUploadForLocalSound();
        const uint32_t playback_id = NextMaomiPlaybackId();
        if (!audio_service.TryPlaySound(std::string_view(static_cast<const char*>(data), size),
                                        false, playback_id)) {
            RestoreMaomiVoiceUploadAfterLocalSound();
            return false;
        }
        maomi_local_sound_playback_id_ = playback_id;
        maomi_autonomy_sound_playing_ = autonomy_sound;
        return true;
    }

    void CancelTrackedMaomiSound() {
        if (maomi_local_sound_playback_id_ == 0) {
            return;
        }
        Application::GetInstance().GetAudioService().ResetDecoder();
        maomi_local_sound_playback_id_ = 0;
        maomi_autonomy_sound_playing_ = false;
        RestoreMaomiVoiceUploadAfterLocalSound();
    }

    static const char* InteractionSoundName(maomi::PetAction action) {
        switch (action) {
            case maomi::PetAction::kPet:
            case maomi::PetAction::kFeed:
            case maomi::PetAction::kPlay:
                return "maomi_meow.ogg";
        }
        return nullptr;
    }

    void TryPlayPendingInteractionSound(const maomi::Snapshot& snapshot) {
        if (!maomi_pending_interaction_sound_.has_value() ||
            !maomi::AllowsMaomiLocalPresentation(snapshot.official_state) ||
            snapshot.priority != maomi::PetPriority::kInteraction) {
            return;
        }
        const auto action = *maomi_pending_interaction_sound_;
        const bool state_matches =
            (action == maomi::PetAction::kPet && snapshot.state == maomi::PetState::kBeingPetted) ||
            (action == maomi::PetAction::kFeed && snapshot.state == maomi::PetState::kEating) ||
            (action == maomi::PetAction::kPlay && snapshot.state == maomi::PetState::kPlaying);
        if (!state_matches) {
            return;
        }
        if (TryPlayMaomiSound(InteractionSoundName(action), false)) {
            maomi_pending_interaction_sound_.reset();
            maomi_pending_interaction_sound_since_ms_ = 0;
            maomi_interaction_remaining_ms_ = kMaomiInteractionVisibleDurationMs;
            maomi_interaction_last_update_ms_ = MonotonicMs();
        }
    }

    maomi::WakePlaybackStart StartMaomiLocalResponse() {
        auto& audio_service = Application::GetInstance().GetAudioService();
        if (maomi_local_sound_playback_id_ == 0) {
            audio_service.ResetDecoder();
        } else {
            CancelTrackedMaomiSound();
        }
        const uint32_t playback_id = NextMaomiPlaybackId();

        void* data = nullptr;
        size_t size = 0;
        if (Assets::GetInstance().GetAssetData("maomi_wake.ogg", data, size) && data != nullptr &&
            audio_service.TryPlaySound(std::string_view(static_cast<const char*>(data), size),
                                       false, playback_id)) {
            return {maomi::WakePlaybackResult::kLocalStarted, playback_id};
        }

        if (Lang::Sounds::OGG_POPUP.empty()) {
            return {maomi::WakePlaybackResult::kFailed, 0};
        }
        if (!audio_service.PlaySound(Lang::Sounds::OGG_POPUP)) {
            return {maomi::WakePlaybackResult::kFailed, 0};
        }
        return {maomi::WakePlaybackResult::kFallbackCompleted, 0};
    }

    void RestoreMaomiWakeDetection() {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateIdle) {
            app.GetAudioService().EnableWakeWordDetection(true);
        }
    }

    void LogMaomiWake(maomi::WakeLogEvent event, const maomi::WakeSnapshot& snapshot) {
        const char* event_name = "unknown";
        switch (event) {
            case maomi::WakeLogEvent::kLocalStarted:
                event_name = "local_started";
                break;
            case maomi::WakeLogEvent::kFallbackCompleted:
                event_name = "fallback_completed";
                break;
            case maomi::WakeLogEvent::kDuplicateSuppressed:
                event_name = "duplicate_suppressed";
                break;
            case maomi::WakeLogEvent::kPlaybackFailed:
                event_name = "playback_failed";
                break;
            case maomi::WakeLogEvent::kPlaybackTimedOut:
                event_name = "playback_timed_out";
                break;
            case maomi::WakeLogEvent::kOfficialInvoked:
                event_name = "official_invoked";
                break;
            case maomi::WakeLogEvent::kOfficialCompleted:
                event_name = "official_completed";
                break;
            case maomi::WakeLogEvent::kRecovered:
                event_name = "recovered";
                break;
            case maomi::WakeLogEvent::kAbandonedForOfficialState:
                event_name = "official_state_preempted";
                break;
        }
        ESP_LOGI(TAG,
                 "event=maomi_wake_%s sequence_id=%lu phase=%u started=%lu fallback=%lu "
                 "duplicates=%lu playback_failures=%lu official_invokes=%lu recoveries=%lu",
                 event_name, static_cast<unsigned long>(snapshot.sequence_id),
                 static_cast<unsigned>(snapshot.phase),
                 static_cast<unsigned long>(snapshot.started_count),
                 static_cast<unsigned long>(snapshot.fallback_count),
                 static_cast<unsigned long>(snapshot.duplicate_count),
                 static_cast<unsigned long>(snapshot.playback_failure_count),
                 static_cast<unsigned long>(snapshot.official_invoke_count),
                 static_cast<unsigned long>(snapshot.recovery_count));
    }

    void PollMaomiWake() {
        if (!maomi_wake_.IsBusy() || !maomi_wake_poll_gate_.TryAcquire()) {
            return;
        }
        Application::GetInstance().RequestBoardPoll();
    }

    bool InitializeMaomiWake() {
        auto& app = Application::GetInstance();
        try {
            app.SetWakeWordInterceptor([this](const std::string& wake_word) {
                auto& app = Application::GetInstance();
                const auto state = app.GetDeviceState();
                if (state == kDeviceStateIdle) {
                    power_save_timer_->WakeUp();
                }
                const auto result = maomi_wake_.HandleWakeWord(wake_word, state, MonotonicMs());
                if (result == maomi::WakeHandleResult::kStarted) {
                    maomi_pet_core_.Submit(maomi::Event::UserWake());
                    UpdateMaomiAutonomyOnMainTask(maomi::ActivitySource::kWakeWord);
                }
                return result != maomi::WakeHandleResult::kPassThrough;
            });
            app.SetPlaybackFinishedObserver([this](uint32_t playback_id) {
                auto& app = Application::GetInstance();
                if (playback_id == maomi_local_sound_playback_id_) {
                    maomi_local_sound_playback_id_ = 0;
                    maomi_autonomy_sound_playing_ = false;
                    RestoreMaomiVoiceUploadAfterLocalSound();
                }
                maomi_wake_.HandlePlaybackFinished(MonotonicMs(), app.GetDeviceState(),
                                                   playback_id);
            });
            app.SetBoardPollObserver([this]() {
                auto& app = Application::GetInstance();
                maomi_wake_.Poll(MonotonicMs(), app.GetDeviceState(),
                                 app.GetAudioService().IsPlaybackIdle());
                maomi_wake_poll_gate_.Release();
            });
            app.GetAudioService().SetDiscardVoiceUploadOnWake(true);
            return true;
        } catch (const std::bad_alloc&) {
            app.SetWakeWordInterceptor({});
            app.SetPlaybackFinishedObserver({});
            app.SetBoardPollObserver({});
            return false;
        }
    }

    void RenderMaomiUi(const maomi::Snapshot& snapshot) {
        if (!maomi_variant_.IsEnabled() || !display_->IsSetupUICalled()) {
            return;
        }

        const maomi::PowerUiSample power_sample = {
            .battery_level = snapshot.battery_level,
            .battery_level_valid = snapshot.battery_level >= 0,
            .external_power_connected = maomi_external_power_connected_,
        };
        const auto power = maomi_power_ui_policy_.Update(power_sample);
        const auto plan =
            maomi_ui_mapper_.Resolve(snapshot, power, maomi_high_temperature_, maomi_ui_assets_);
        if (plan.surface != maomi::UiSurface::kPet) {
            last_maomi_display_emotion_ = {};
            return;
        }
        TryPlayPendingInteractionSound(snapshot);
        if (last_maomi_display_emotion_ == plan.display_emotion) {
            return;
        }
        display_->SetEmotion(plan.display_emotion);
        last_maomi_display_emotion_ = plan.display_emotion;
    }

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_9);
        power_manager_->OnTemperatureChanged([this](float chip_temp) {
            Application::GetInstance().Schedule([this, chip_temp]() {
                maomi_high_temperature_ = chip_temp >= kHighTemperatureThresholdCelsius;
                display_->UpdateHighTempWarning(chip_temp);
                RenderMaomiUi(maomi_pet_core_.GetSnapshot());
            });
        });

        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            Application::GetInstance().Schedule([this, is_charging]() {
                maomi_external_power_connected_ = is_charging;
                if (is_charging) {
                    power_save_timer_->SetEnabled(false);
                    ESP_LOGI("PowerManager", "Charging started");
                } else {
                    power_save_timer_->SetEnabled(true);
                    ESP_LOGI("PowerManager", "Charging stopped");
                }
                if (maomi_variant_.IsEnabled()) {
                    maomi_pet_core_.Submit(maomi::Event::ChargingChanged(is_charging));
                }
            });
        });
    }

    static bool SaveIsPending(maomi::SaveResult result) {
        return result == maomi::SaveResult::kDeferred ||
               result == maomi::SaveResult::kRateLimited || result == maomi::SaveResult::kFailed ||
               result == maomi::SaveResult::kWriteBlocked ||
               result == maomi::SaveResult::kInvalidState;
    }

    void ObserveMaomiClockOnMainTask(uint64_t monotonic_ms) {
        const uint64_t current_second = monotonic_ms / 1000;
        if (current_second == maomi_last_clock_observe_second_) {
            return;
        }
        maomi_last_clock_observe_second_ = current_second;

        const std::time_t now = std::time(nullptr);
        std::tm local_time = {};
        const bool has_local_time = localtime_r(&now, &local_time) != nullptr;
        maomi::TimeSample sample;
        sample.server_time_set = has_local_time && local_time.tm_year + 1900 >=
                                                       maomi::ReliableClock::kMinimumTrustedYear;
        sample.local_time = {
            .year = local_time.tm_year + 1900,
            .month = local_time.tm_mon + 1,
            .day = local_time.tm_mday,
            .hour = local_time.tm_hour,
            .minute = local_time.tm_min,
            .second = local_time.tm_sec,
        };
        sample.monotonic_ms = monotonic_ms;
        maomi_clock_.Observe(sample);

        const bool time_valid = maomi_clock_.GetSnapshot().valid;
        if (time_valid != last_maomi_time_valid_) {
            last_maomi_time_valid_ = time_valid;
            maomi_pet_core_.Submit(maomi::Event::TimeValidityChanged(time_valid));
        }

        if (maomi_bond_ && maomi_bond_->ObserveTime(maomi_clock_)) {
            const auto merged = maomi_bond_->MergePersistentState(maomi_storage_.GetState());
            maomi_storage_.Update(merged, maomi::WriteImportance::kNormal, monotonic_ms);
        }
        maomi_storage_.FlushIfDue(monotonic_ms);
    }

    maomi::ClockSnapshot ReminderClockAt(uint64_t monotonic_ms) const {
        auto snapshot = maomi_clock_.GetSnapshot();
        snapshot.monotonic_ms = monotonic_ms;
        return snapshot;
    }

    static maomi::PetState PetStateForAction(maomi::PetAction action) {
        switch (action) {
            case maomi::PetAction::kPet:
                return maomi::PetState::kBeingPetted;
            case maomi::PetAction::kFeed:
                return maomi::PetState::kEating;
            case maomi::PetAction::kPlay:
                return maomi::PetState::kPlaying;
        }
        return maomi::PetState::kIdle;
    }

    static maomi::BondAction BondActionForPetAction(maomi::PetAction action) {
        switch (action) {
            case maomi::PetAction::kPet:
                return maomi::BondAction::kPet;
            case maomi::PetAction::kFeed:
                return maomi::BondAction::kFeed;
            case maomi::PetAction::kPlay:
                return maomi::BondAction::kPlay;
        }
        return maomi::BondAction::kCount;
    }

    static maomi::PetState PetStateForAutonomyAction(maomi::AutonomyAction action) {
        switch (action) {
            case maomi::AutonomyAction::kBlink:
                return maomi::PetState::kBlinking;
            case maomi::AutonomyAction::kLookAround:
                return maomi::PetState::kCurious;
            case maomi::AutonomyAction::kBecomeSleepy:
                return maomi::PetState::kSleepy;
            case maomi::AutonomyAction::kSleepBreath:
                return maomi::PetState::kSleeping;
            case maomi::AutonomyAction::kNone:
                return maomi::PetState::kIdle;
        }
        return maomi::PetState::kIdle;
    }

    void ApplyMaomiAutonomyDecision(const maomi::AutonomySnapshot& before,
                                    const maomi::AutonomyDecision& decision,
                                    const maomi::AutonomySnapshot& after) {
        const bool action_ended = decision.stopped_action != maomi::AutonomyAction::kNone ||
                                  (before.active_action != maomi::AutonomyAction::kNone &&
                                   after.active_action == maomi::AutonomyAction::kNone);
        if (action_ended) {
            maomi_pet_core_.ReleaseExpression(maomi::PetPriority::kAutonomous);
        }
        if (decision.started_action != maomi::AutonomyAction::kNone) {
            maomi_pet_core_.RequestExpression(PetStateForAutonomyAction(decision.started_action),
                                              maomi::PetPriority::kAutonomous, false);
        }

        if (decision.restore_display) {
            GetBacklight()->RestoreBrightness();
        }
        if (decision.enter_low_brightness) {
            GetBacklight()->SetBrightness(1);
        }

        if (decision.stopped_sound != maomi::AutonomySound::kNone &&
            maomi_autonomy_sound_playing_) {
            Application::GetInstance().GetAudioService().ResetDecoder();
            maomi_local_sound_playback_id_ = 0;
            maomi_autonomy_sound_playing_ = false;
        }
        if (decision.started_sound == maomi::AutonomySound::kPlayLocalMeow) {
            TryPlayMaomiSound("maomi_meow.ogg", true);
        }
    }

    void UpdateMaomiReminderLifetime(uint64_t monotonic_ms, const maomi::Snapshot& pet_snapshot) {
        if (maomi_reminder_remaining_ms_ == 0) {
            return;
        }
        if (monotonic_ms < maomi_reminder_last_update_ms_) {
            maomi_reminder_last_update_ms_ = monotonic_ms;
            return;
        }
        const uint64_t elapsed_ms = monotonic_ms - maomi_reminder_last_update_ms_;
        maomi_reminder_last_update_ms_ = monotonic_ms;
        if (maomi_high_temperature_ || pet_snapshot.paused_by_official_state ||
            pet_snapshot.priority != maomi::PetPriority::kReminder) {
            return;
        }
        if (elapsed_ms < maomi_reminder_remaining_ms_) {
            maomi_reminder_remaining_ms_ -= elapsed_ms;
            return;
        }

        maomi_reminder_remaining_ms_ = 0;
        maomi_pet_core_.ReleaseExpression(maomi::PetPriority::kReminder);
    }

    void HandleMaomiRemindersOnMainTask(uint64_t monotonic_ms,
                                        const maomi::Snapshot& pet_snapshot) {
        if (!maomi_reminders_) {
            return;
        }
        auto& app = Application::GetInstance();
        auto& audio_service = app.GetAudioService();
        const bool device_busy = app.GetDeviceState() != kDeviceStateIdle ||
                                 !audio_service.IsPlaybackIdle() ||
                                 maomi_local_sound_playback_id_ != 0;
        const bool low_battery =
            pet_snapshot.battery_level >= 0 && pet_snapshot.battery_level <= 20;
        const auto event = maomi_reminders_->Update({
            .clock = ReminderClockAt(monotonic_ms),
            .device_busy = device_busy,
            .low_battery = low_battery,
        });
        if (event.state == maomi::ReminderEventState::kNone) {
            return;
        }

        const auto presentation = maomi::DecideReminderPresentation(event, maomi_high_temperature_);
        if (!presentation.show_animation) {
            return;
        }
        const auto submit = maomi_pet_core_.Submit(maomi::Event::ReminderDue());
        if (submit == maomi::SubmitResult::kRejected) {
            ESP_LOGW(TAG, "Maomi reminder presentation queue rejected id=%u",
                     static_cast<unsigned>(event.id));
            return;
        }
        maomi_reminder_remaining_ms_ = kMaomiReminderVisibleDurationMs;
        maomi_reminder_last_update_ms_ = monotonic_ms;
        if (presentation.play_sound && !TryPlayMaomiSound(kMaomiReminderSoundName, false)) {
            ESP_LOGW(TAG, "Maomi reminder sound unavailable id=%u",
                     static_cast<unsigned>(event.id));
        }
    }

    void UpdateMaomiAutonomyOnMainTask(maomi::ActivitySource activity) {
        if (!maomi_runtime_ready_) {
            return;
        }
        const uint64_t monotonic_ms = MonotonicMs();
        ObserveMaomiClockOnMainTask(monotonic_ms);
        const auto pet_snapshot = maomi_pet_core_.GetSnapshot();
        TryPlayPendingInteractionSound(pet_snapshot);
        UpdateMaomiInteractionLifetime(monotonic_ms, pet_snapshot);
        UpdateMaomiReminderLifetime(monotonic_ms, pet_snapshot);
        HandleMaomiRemindersOnMainTask(monotonic_ms, pet_snapshot);
        const auto before = maomi_autonomy_.GetSnapshot();
        const maomi::AutonomyInputs inputs = {
            .official_idle = Application::GetInstance().GetDeviceState() == kDeviceStateIdle,
            .higher_priority_active = pet_snapshot.priority >= maomi::PetPriority::kReminder &&
                                      pet_snapshot.priority < maomi::PetPriority::kAutonomous,
            .activity = activity,
            .clock = maomi_clock_.GetSnapshot(),
            .charging = pet_snapshot.charging,
            .battery_level = pet_snapshot.battery_level,
            .manual_quiet = maomi_storage_.GetState().manual_quiet,
        };
        const auto decision = maomi_autonomy_.Update(monotonic_ms, inputs);
        const auto after = maomi_autonomy_.GetSnapshot();
        ApplyMaomiAutonomyDecision(before, decision, after);
    }

    void RequestMaomiPolicyPoll() {
        bool expected = false;
        if (!maomi_policy_poll_pending_.compare_exchange_strong(expected, true)) {
            return;
        }
        Application::GetInstance().Schedule([this]() {
            maomi_policy_poll_pending_.store(false);
            UpdateMaomiAutonomyOnMainTask(maomi::ActivitySource::kNone);
        });
    }

    void UpdateMaomiInteractionLifetime(uint64_t monotonic_ms,
                                        const maomi::Snapshot& pet_snapshot) {
        if (!maomi_active_interaction_.has_value()) {
            return;
        }
        const auto sound_wait = maomi::DecideInteractionSoundWait(
            maomi_pending_interaction_sound_.has_value(), maomi_pending_interaction_sound_since_ms_,
            monotonic_ms, kMaomiInteractionSoundMaxWaitMs);
        if (sound_wait == maomi::InteractionSoundWaitDecision::kKeepWaiting) {
            maomi_interaction_last_update_ms_ = monotonic_ms;
            return;
        }
        if (sound_wait == maomi::InteractionSoundWaitDecision::kTimedOut) {
            ESP_LOGW(TAG, "Dropping pending interaction sound after bounded wait");
            maomi_pending_interaction_sound_.reset();
            maomi_pending_interaction_sound_since_ms_ = 0;
            maomi_interaction_last_update_ms_ = monotonic_ms;
            return;
        }
        if (monotonic_ms < maomi_interaction_last_update_ms_) {
            maomi_interaction_last_update_ms_ = monotonic_ms;
            return;
        }
        const uint64_t elapsed_ms = monotonic_ms - maomi_interaction_last_update_ms_;
        maomi_interaction_last_update_ms_ = monotonic_ms;
        if (pet_snapshot.paused_by_official_state ||
            pet_snapshot.priority != maomi::PetPriority::kInteraction) {
            return;
        }
        if (elapsed_ms < maomi_interaction_remaining_ms_) {
            maomi_interaction_remaining_ms_ -= elapsed_ms;
            return;
        }

        maomi_interaction_remaining_ms_ = 0;
        maomi_active_interaction_.reset();
        maomi_pending_interaction_sound_.reset();
        maomi_pending_interaction_sound_since_ms_ = 0;
        maomi_pet_core_.ReleaseExpression(maomi::PetPriority::kInteraction);
    }

    maomi::InteractionToolResult HandleMaomiInteraction(maomi::PetAction action) {
        maomi::InteractionToolResult result;
        result.action = action;
        if (!maomi_bond_) {
            return result;
        }

        const auto submit =
            maomi_pet_core_.Submit(maomi::Event::Interaction(PetStateForAction(action)));
        if (submit == maomi::SubmitResult::kRejected) {
            result.state = maomi::ToolOperationState::kRejected;
            result.bond_points = maomi_bond_->GetSnapshot(maomi_clock_).total_points;
            return result;
        }

        const auto bond_update = maomi_bond_->Record(BondActionForPetAction(action), maomi_clock_);
        maomi::SaveResult save_result = maomi::SaveResult::kNoChanges;
        if (bond_update.persistent_state_changed) {
            const auto merged = maomi_bond_->MergePersistentState(maomi_storage_.GetState());
            save_result =
                maomi_storage_.Update(merged, maomi::WriteImportance::kImportant, MonotonicMs());
        }
        result.state = maomi::ToolOperationState::kQueued;
        result.points_added = bond_update.points_added;
        result.bond_points = maomi_bond_->GetSnapshot(maomi_clock_).total_points;
        result.persistence_pending =
            bond_update.persistent_state_changed && SaveIsPending(save_result);

        maomi_active_interaction_ = action;
        maomi_interaction_remaining_ms_ = kMaomiInteractionVisibleDurationMs;
        maomi_interaction_last_update_ms_ = MonotonicMs();

        const char* sound = InteractionSoundName(action);
        result.sound_queued = maomi::QueueLatestInteractionSound(
            HasMaomiSound(sound), action, maomi_interaction_last_update_ms_,
            maomi_pending_interaction_sound_, maomi_pending_interaction_sound_since_ms_);
        UpdateMaomiAutonomyOnMainTask(maomi::ActivitySource::kPetInteraction);
        return result;
    }

    maomi::PetToolSnapshot GetMaomiToolSnapshot() const {
        if (!maomi_bond_) {
            return {};
        }
        const auto pet = maomi_pet_core_.GetSnapshot();
        const auto bond = maomi_bond_->GetSnapshot(maomi_clock_);
        return {
            .bond_points = bond.total_points,
            .bond_level = bond.level,
            .companion_days = bond.companion_days,
            .mood = pet.state,
            .battery_level = pet.battery_level,
            .charging = pet.charging,
            .manual_quiet = maomi_storage_.GetState().manual_quiet,
            .active_reminders = maomi_reminders_ == nullptr
                                    ? uint8_t{0}
                                    : static_cast<uint8_t>(maomi_reminders_->ActiveCount()),
        };
    }

    maomi::QuietToolResult SetMaomiQuiet(bool enabled) {
        maomi::QuietToolResult result;
        auto state = maomi_storage_.GetState();
        state.manual_quiet = enabled;
        const auto save =
            maomi_storage_.Update(state, maomi::WriteImportance::kImportant, MonotonicMs());
        if (save == maomi::SaveResult::kWriteBlocked || save == maomi::SaveResult::kInvalidState ||
            (save == maomi::SaveResult::kFailed &&
             maomi_storage_.GetState().manual_quiet != enabled)) {
            result.enabled = maomi_storage_.GetState().manual_quiet;
            return result;
        }
        result.state = maomi::ToolOperationState::kCompleted;
        result.enabled = maomi_storage_.GetState().manual_quiet;
        result.persistence_pending = SaveIsPending(save);
        UpdateMaomiAutonomyOnMainTask(maomi::ActivitySource::kNone);
        return result;
    }

    bool InitializeMaomiState() {
        const auto load = maomi_storage_.Load(MonotonicMs());
        try {
            maomi_bond_ = std::make_unique<maomi::BondTracker>(load.state);
        } catch (const std::bad_alloc&) {
            ESP_LOGE(TAG, "Failed to allocate Maomi relationship state");
            return false;
        }
        ObserveMaomiClockOnMainTask(MonotonicMs());
        return true;
    }

    bool InitializeMaomiReminders() {
        try {
            auto reminders = std::make_unique<maomi::ReminderEngine>(maomi_storage_);
            const auto restored = reminders->Restore(maomi_clock_.GetSnapshot());
            if (restored.status == maomi::RestoreStatus::kUnavailable) {
                ESP_LOGE(TAG, "Maomi reminders unavailable because saved state is read-only");
                return false;
            }
            if (restored.status == maomi::RestoreStatus::kRecovered) {
                ESP_LOGW(TAG, "Recovered invalid Maomi reminder data with safe defaults");
            }
            ESP_LOGI(TAG, "Maomi reminders restored: %u",
                     static_cast<unsigned>(restored.restored_count));
            maomi_reminders_ = std::move(reminders);
            return true;
        } catch (const std::bad_alloc&) {
            ESP_LOGE(TAG, "Failed to allocate Maomi reminder state");
            return false;
        }
    }

    bool InitializeMaomiTools() {
        try {
            auto& server = McpServer::GetInstance();
            maomi::RegisterPetTools(
                server,
                {
                    .interact =
                        [this](maomi::PetAction action) { return HandleMaomiInteraction(action); },
                    .get_status = [this]() { return GetMaomiToolSnapshot(); },
                    .set_quiet = [this](bool enabled) { return SetMaomiQuiet(enabled); },
                });

            maomi::ReminderToolDependencies reminder_dependencies;
            if (maomi_reminders_) {
                reminder_dependencies.start_countdown = [this](uint32_t duration_seconds,
                                                               std::string_view label) {
                    const uint64_t monotonic_ms = MonotonicMs();
                    ObserveMaomiClockOnMainTask(monotonic_ms);
                    return maomi_reminders_->StartCountdown(duration_seconds, label,
                                                            ReminderClockAt(monotonic_ms));
                };
                reminder_dependencies.set_alarm = [this](const maomi::DateTime& target,
                                                         std::string_view label) {
                    const uint64_t monotonic_ms = MonotonicMs();
                    ObserveMaomiClockOnMainTask(monotonic_ms);
                    return maomi_reminders_->SetAlarm(target, label, ReminderClockAt(monotonic_ms));
                };
                reminder_dependencies.start_interval = [this](maomi::ReminderKind kind,
                                                              uint32_t interval_minutes,
                                                              std::string_view label) {
                    const uint64_t monotonic_ms = MonotonicMs();
                    ObserveMaomiClockOnMainTask(monotonic_ms);
                    return maomi_reminders_->StartInterval(kind, interval_minutes, label,
                                                           ReminderClockAt(monotonic_ms));
                };
                reminder_dependencies.start_pomodoro =
                    [this](uint32_t work_minutes, uint32_t break_minutes, uint32_t cycles) {
                        const uint64_t monotonic_ms = MonotonicMs();
                        ObserveMaomiClockOnMainTask(monotonic_ms);
                        return maomi_reminders_->StartPomodoro(work_minutes, break_minutes, cycles,
                                                               ReminderClockAt(monotonic_ms));
                    };
                reminder_dependencies.cancel = [this](uint16_t id) {
                    const uint64_t monotonic_ms = MonotonicMs();
                    ObserveMaomiClockOnMainTask(monotonic_ms);
                    return maomi_reminders_->Cancel(id, ReminderClockAt(monotonic_ms));
                };
                reminder_dependencies.list = [this]() {
                    const uint64_t monotonic_ms = MonotonicMs();
                    ObserveMaomiClockOnMainTask(monotonic_ms);
                    return maomi_reminders_->List(ReminderClockAt(monotonic_ms));
                };
            }
            maomi::RegisterReminderTools(server, std::move(reminder_dependencies));
            return true;
        } catch (const std::exception& error) {
            ESP_LOGE(TAG, "Failed to register Maomi MCP tools: %s", error.what());
            return false;
        }
    }

    void PollMaomiPetCore() {
        PollMaomiWake();
        RequestMaomiPolicyPoll();

        const auto official_state = Application::GetInstance().GetDeviceState();
        if (official_state != last_maomi_official_state_) {
            last_maomi_official_state_ = official_state;
            Application::GetInstance().Schedule([this, official_state]() {
                if (!maomi::AllowsMaomiLocalPresentation(official_state)) {
                    CancelTrackedMaomiSound();
                }
                maomi_pet_core_.Submit(maomi::Event::OfficialStateChanged(official_state));
            });
        }

        if (++maomi_poll_divider_ < 10) {
            return;
        }
        maomi_poll_divider_ = 0;

        const auto uptime_seconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
        maomi_pet_core_.Submit(maomi::Event::Tick(uptime_seconds));
        if (maomi_battery_warmup_samples_ < kMaomiBatteryWarmupSamples) {
            ++maomi_battery_warmup_samples_;
            if (maomi_battery_warmup_samples_ < kMaomiBatteryWarmupSamples) {
                return;
            }
        }
        const int battery_level = static_cast<int>(power_manager_->GetBatteryLevel());
        if (battery_level != last_maomi_battery_level_) {
            last_maomi_battery_level_ = battery_level;
            maomi_pet_core_.Submit(maomi::Event::BatteryChanged(battery_level));
        }
    }

    bool InitializeMaomiPetCore() {
        last_maomi_official_state_ = Application::GetInstance().GetDeviceState();

        const esp_timer_create_args_t timer_args = {
            .callback =
                [](void* arg) {
                    static_cast<ZHENGCHEN_1_54TFT_WIFI_MAOMI*>(arg)->PollMaomiPetCore();
                },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "maomi_poll",
            .skip_unhandled_events = true,
        };
        const esp_err_t create_result = esp_timer_create(&timer_args, &maomi_poll_timer_);
        if (create_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create Maomi poll timer: %s", esp_err_to_name(create_result));
            return false;
        }
        const int ui_observer_id = maomi_pet_core_.AddObserver(
            [this](const maomi::Snapshot& snapshot) { RenderMaomiUi(snapshot); });
        if (ui_observer_id < 0) {
            ESP_LOGE(TAG, "Failed to register Maomi UI observer");
            esp_timer_delete(maomi_poll_timer_);
            maomi_poll_timer_ = nullptr;
            return false;
        }
        const esp_err_t start_result = esp_timer_start_periodic(maomi_poll_timer_, 100000);
        if (start_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Maomi poll timer: %s", esp_err_to_name(start_result));
            maomi_pet_core_.RemoveObserver(ui_observer_id);
            esp_timer_delete(maomi_poll_timer_);
            maomi_poll_timer_ = nullptr;
            return false;
        }
        maomi_external_power_connected_ = !power_manager_->IsDischarging();
        maomi_pet_core_.Submit(maomi::Event::ChargingChanged(maomi_external_power_connected_));
        return true;
    }

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_2);
        rtc_gpio_set_direction(GPIO_NUM_2, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_2, 1);

        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->SetEnabled(true);
    }

    void ScheduleMaomiActivity(maomi::ActivitySource activity) {
        Application::GetInstance().Schedule(
            [this, activity]() { UpdateMaomiAutonomyOnMainTask(activity); });
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            ScheduleMaomiActivity(maomi::ActivitySource::kButton);
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            if (maomi_wake_.IsBusy()) {
                return;
            }
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            ScheduleMaomiActivity(maomi::ActivitySource::kButton);
            auto& app = Application::GetInstance();
            app.SetDeviceState(kDeviceStateWifiConfiguring);
            EnterWifiConfigMode();
        });

        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            ScheduleMaomiActivity(maomi::ActivitySource::kButton);
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume / 10));
        });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            ScheduleMaomiActivity(maomi::ActivitySource::kButton);
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            ScheduleMaomiActivity(maomi::ActivitySource::kButton);
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume / 10));
        });

        volume_down_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            ScheduleMaomiActivity(maomi::ActivitySource::kButton);
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeSt7789Display() {
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));

        display_ = new ZHENGCHEN_LcdDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                            DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_->SetupHighTempWarningPopup();
    }

    void InitializeMaomiVariant() {
        if (!maomi_variant_.Initialize()) {
            ESP_LOGW(TAG, "Maomi features disabled; continuing with base firmware");
            return;
        }
        if (!InitializeMaomiState()) {
            ESP_LOGW(TAG, "Maomi saved state unavailable; continuing with base firmware");
            return;
        }
        if (!InitializeMaomiReminders()) {
            ESP_LOGW(TAG, "Maomi reminders disabled; local pet features remain enabled");
        }
        if (!InitializeMaomiPetCore()) {
            ESP_LOGW(TAG, "Maomi pet core disabled; continuing with base firmware");
            return;
        }
        maomi_runtime_ready_ = true;
        if (!InitializeMaomiTools()) {
            ESP_LOGW(TAG, "Maomi MCP tools unavailable; local pet features remain enabled");
        }
        if (!InitializeMaomiWake()) {
            ESP_LOGW(TAG, "Maomi wake setup failed; continuing with base firmware");
        }
    }

public:
    ZHENGCHEN_1_54TFT_WIFI_MAOMI()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
          maomi_pet_core_(
              [](std::function<void()>&& callback) {
                  Application::GetInstance().Schedule(std::move(callback));
              },
              [](maomi::LogEvent event, const maomi::Snapshot& snapshot) {
                  if (event == maomi::LogEvent::kQueuePressure) {
                      ESP_LOGW(TAG,
                               "event=maomi_queue_pressure queue_depth=%u submitted=%lu "
                               "processed=%lu coalesced=%lu rejected=%lu evicted=%lu",
                               static_cast<unsigned>(snapshot.queue_depth),
                               static_cast<unsigned long>(snapshot.submitted),
                               static_cast<unsigned long>(snapshot.processed),
                               static_cast<unsigned long>(snapshot.coalesced),
                               static_cast<unsigned long>(snapshot.rejected),
                               static_cast<unsigned long>(snapshot.evicted));
                  }
              },
              Application::GetInstance().GetDeviceState()),
          maomi_storage_(maomi_storage_backend_,
                         [](maomi::StorageLogEvent event, const char* context) {
                             ESP_LOGW(TAG, "event=maomi_storage status=%u context=%s",
                                      static_cast<unsigned>(event),
                                      context == nullptr ? "" : context);
                         }),
          maomi_wake_({
              .stop_voice_upload = [this]() { StopMaomiVoiceUpload(); },
              .start_local_response = [this]() { return StartMaomiLocalResponse(); },
              .cancel_playback =
                  []() { Application::GetInstance().GetAudioService().ResetDecoder(); },
              .invoke_official =
                  [](const std::string& wake_word) {
                      return Application::GetInstance().TryWakeWordInvokeFromMainTask(wake_word);
                  },
              .abort_official =
                  []() {
                      auto& app = Application::GetInstance();
                      if (app.GetDeviceState() == kDeviceStateConnecting) {
                          app.SetDeviceState(kDeviceStateIdle);
                      }
                  },
              .restore_wake_detection = [this]() { RestoreMaomiWakeDetection(); },
              .logger = [this](
                            maomi::WakeLogEvent event,
                            const maomi::WakeSnapshot& snapshot) { LogMaomiWake(event, snapshot); },
          }) {
        InitializeSpi();
        InitializeSt7789Display();
        InitializePowerSaveTimer();
        InitializePowerManager();
        InitializeButtons();
        InitializeMaomiVariant();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                                               AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
                                               AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = std::max<uint32_t>(power_manager_->GetBatteryLevel(), 20);
        return true;
    }

    virtual bool GetTemperature(float& esp32temp) override {
        esp32temp = power_manager_->GetTemperature();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(ZHENGCHEN_1_54TFT_WIFI_MAOMI);
