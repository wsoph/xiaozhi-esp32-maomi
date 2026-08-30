#pragma once

#include "../1.54tft-wifi/zhengchen_lcd_display.h"

LV_FONT_DECLARE(lv_font_montserrat_48);

class MaomiLcdDisplay : public ZHENGCHEN_LcdDisplay {
private:
    static constexpr uint32_t kCountdownFurColor = 0xFFB85C;
    static constexpr uint32_t kCountdownLightFurColor = 0xFFD39A;
    static constexpr uint32_t kCountdownPaleFurColor = 0xFFF1DC;
    static constexpr uint32_t kCountdownStripeColor = 0xD97832;
    static constexpr uint32_t kCountdownNoseColor = 0xF49AB5;
    static constexpr uint32_t kCountdownInkColor = 0x17151E;

    lv_obj_t* countdown_popup_ = nullptr;
    lv_obj_t* countdown_label_ = nullptr;

    lv_obj_t* CreateCountdownShape(int32_t x, int32_t y, int32_t width, int32_t height,
                                   uint32_t color, int32_t radius) {
        lv_obj_t* shape = lv_obj_create(countdown_popup_);
        lv_obj_set_pos(shape, x, y);
        lv_obj_set_size(shape, width, height);
        lv_obj_set_style_pad_all(shape, 0, 0);
        lv_obj_set_style_radius(shape, radius, 0);
        lv_obj_set_style_border_width(shape, 0, 0);
        lv_obj_set_style_bg_color(shape, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
        lv_obj_set_scrollbar_mode(shape, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
        return shape;
    }

    void SetupCountdownCatFaceFrame() {
        // The enclosure supplies the ears; these edge decorations leave the timer unobstructed.
        CreateCountdownShape(-28, -98, 296, 172, kCountdownPaleFurColor, 86);
        CreateCountdownShape(84, 10, 8, 33, kCountdownStripeColor, 3);
        CreateCountdownShape(105, 10, 8, 33, kCountdownStripeColor, 3);
        CreateCountdownShape(127, 10, 8, 33, kCountdownStripeColor, 3);
        CreateCountdownShape(148, 10, 8, 33, kCountdownStripeColor, 3);

        CreateCountdownShape(-20, 138, 70, 54, kCountdownFurColor, 27);
        CreateCountdownShape(190, 138, 70, 54, kCountdownFurColor, 27);
        CreateCountdownShape(-12, 146, 48, 38, kCountdownLightFurColor, 19);
        CreateCountdownShape(204, 146, 48, 38, kCountdownLightFurColor, 19);
        for (int32_t y : {149, 161, 173}) {
            CreateCountdownShape(0, y, 50, 3, kCountdownStripeColor, 1);
            CreateCountdownShape(190, y, 50, 3, kCountdownStripeColor, 1);
        }

        CreateCountdownShape(116, 191, 8, 6, kCountdownNoseColor, 3);
        CreateCountdownShape(119, 197, 3, 10, kCountdownInkColor, 1);
        CreateCountdownShape(111, 204, 10, 3, kCountdownInkColor, 1);
        CreateCountdownShape(121, 204, 10, 3, kCountdownInkColor, 1);
    }

    void SetupCountdownPopup() {
        if (countdown_popup_ != nullptr) {
            return;
        }
        countdown_popup_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(countdown_popup_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_pad_all(countdown_popup_, 0, 0);
        lv_obj_set_style_radius(countdown_popup_, 0, 0);
        lv_obj_set_style_border_width(countdown_popup_, 0, 0);
        lv_obj_set_style_bg_color(countdown_popup_, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(countdown_popup_, LV_OPA_COVER, 0);
        lv_obj_align(countdown_popup_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_scrollbar_mode(countdown_popup_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(countdown_popup_, LV_OBJ_FLAG_SCROLLABLE);

        SetupCountdownCatFaceFrame();
        countdown_label_ = lv_label_create(countdown_popup_);
        lv_obj_set_style_text_font(countdown_label_, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(countdown_label_, lv_color_hex(kCountdownInkColor), 0);
        lv_obj_set_style_text_align(countdown_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(countdown_label_, "");
        lv_obj_align(countdown_label_, LV_ALIGN_CENTER, 0, -4);
        lv_obj_add_flag(countdown_popup_, LV_OBJ_FLAG_HIDDEN);
    }

public:
    using ZHENGCHEN_LcdDisplay::ZHENGCHEN_LcdDisplay;

    void SetupUI() override {
        ZHENGCHEN_LcdDisplay::SetupUI();
        SetHideSubtitle(true);
        DisplayLockGuard lock(this);
        if (top_bar_ != nullptr) {
            lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        if (status_bar_ != nullptr) {
            lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        if (bottom_bar_ != nullptr) {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        if (low_battery_popup_ != nullptr) {
            lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
        }
        SetupCountdownPopup();
    }

    void SetCountdownSeconds(int32_t seconds) {
        DisplayLockGuard lock(this);
        if (countdown_popup_ == nullptr || countdown_label_ == nullptr) {
            return;
        }
        if (seconds < 0) {
            lv_obj_add_flag(countdown_popup_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        const long hours = static_cast<long>(seconds / 3600);
        const long minutes = static_cast<long>((seconds / 60) % 60);
        const long remaining_seconds = static_cast<long>(seconds % 60);
        lv_label_set_text_fmt(countdown_label_, "%02ld:%02ld:%02ld", hours, minutes,
                              remaining_seconds);
        lv_obj_remove_flag(countdown_popup_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(countdown_popup_);
    }
};
