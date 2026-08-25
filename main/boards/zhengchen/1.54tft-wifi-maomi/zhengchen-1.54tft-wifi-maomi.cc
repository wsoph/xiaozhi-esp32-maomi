#include "../1.54tft-wifi/power_manager.h"
#include "../1.54tft-wifi/zhengchen_lcd_display.h"
#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "led/single_led.h"
#include "maomi_pet_core.h"
#include "maomi_variant.h"
#include "maomi_wake.h"
#include "power_save_timer.h"
#include "system_reset.h"
#include "wifi_board.h"

#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include <new>
#include <string_view>
#include <utility>

#define TAG "ZHENGCHEN_1_54TFT_WIFI_MAOMI"

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
    maomi::WakeSequence maomi_wake_;
    maomi::WakePollGate maomi_wake_poll_gate_;
    uint32_t maomi_next_playback_id_ = 0;
    esp_timer_handle_t maomi_poll_timer_ = nullptr;
    DeviceState last_maomi_official_state_ = kDeviceStateUnknown;
    int last_maomi_battery_level_ = -1;
    uint8_t maomi_poll_divider_ = 0;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    static uint64_t MonotonicMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

    void StopMaomiVoiceUpload() {
        auto& audio_service = Application::GetInstance().GetAudioService();
        audio_service.EnableVoiceProcessing(false);
        audio_service.DiscardVoiceUploadBacklog();
    }

    uint32_t NextMaomiPlaybackId() {
        ++maomi_next_playback_id_;
        if (maomi_next_playback_id_ == 0) {
            ++maomi_next_playback_id_;
        }
        return maomi_next_playback_id_;
    }

    maomi::WakePlaybackStart StartMaomiLocalResponse() {
        auto& audio_service = Application::GetInstance().GetAudioService();
        audio_service.ResetDecoder();
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
                }
                return result != maomi::WakeHandleResult::kPassThrough;
            });
            app.SetPlaybackFinishedObserver([this](uint32_t playback_id) {
                auto& app = Application::GetInstance();
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

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_9);
        power_manager_->OnTemperatureChanged(
            [this](float chip_temp) { display_->UpdateHighTempWarning(chip_temp); });

        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
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
    }

    void PollMaomiPetCore() {
        PollMaomiWake();

        const auto official_state = Application::GetInstance().GetDeviceState();
        if (official_state != last_maomi_official_state_) {
            last_maomi_official_state_ = official_state;
            maomi_pet_core_.Submit(maomi::Event::OfficialStateChanged(official_state));
        }

        if (++maomi_poll_divider_ < 10) {
            return;
        }
        maomi_poll_divider_ = 0;

        const auto uptime_seconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
        maomi_pet_core_.Submit(maomi::Event::Tick(uptime_seconds));
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
        const esp_err_t start_result = esp_timer_start_periodic(maomi_poll_timer_, 100000);
        if (start_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Maomi poll timer: %s", esp_err_to_name(start_result));
            esp_timer_delete(maomi_poll_timer_);
            maomi_poll_timer_ = nullptr;
            return false;
        }
        maomi_pet_core_.Submit(maomi::Event::ChargingChanged(!power_manager_->IsDischarging()));
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
            auto& app = Application::GetInstance();
            app.SetDeviceState(kDeviceStateWifiConfiguring);
            EnterWifiConfigMode();
        });

        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
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
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
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
        if (!InitializeMaomiPetCore()) {
            ESP_LOGW(TAG, "Maomi pet core disabled; continuing with base firmware");
            return;
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
