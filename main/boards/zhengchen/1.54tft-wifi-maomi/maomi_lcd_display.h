#pragma once

#include "../1.54tft-wifi/zhengchen_lcd_display.h"

class MaomiLcdDisplay : public ZHENGCHEN_LcdDisplay {
private:
    lv_obj_t* countdown_popup_ = nullptr;
    lv_obj_t* countdown_label_ = nullptr;

    void SetupCountdownPopup() {
        if (countdown_popup_ != nullptr) {
            return;
        }
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        countdown_popup_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(countdown_popup_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(countdown_popup_, lvgl_theme->spacing(4), 0);
        lv_obj_set_style_pad_ver(countdown_popup_, lvgl_theme->spacing(2), 0);
        lv_obj_set_style_radius(countdown_popup_, lvgl_theme->spacing(4), 0);
        lv_obj_set_style_border_width(countdown_popup_, 0, 0);
        lv_obj_set_style_bg_color(countdown_popup_, lvgl_theme->background_color(), 0);
        lv_obj_set_style_bg_opa(countdown_popup_, LV_OPA_80, 0);
        lv_obj_align(countdown_popup_, LV_ALIGN_TOP_MID, 0, 28);
        lv_obj_set_scrollbar_mode(countdown_popup_, LV_SCROLLBAR_MODE_OFF);

        countdown_label_ = lv_label_create(countdown_popup_);
        lv_obj_set_style_text_color(countdown_label_, lvgl_theme->text_color(), 0);
        lv_label_set_text(countdown_label_, "");
        lv_obj_center(countdown_label_);
        lv_obj_add_flag(countdown_popup_, LV_OBJ_FLAG_HIDDEN);
    }

public:
    using ZHENGCHEN_LcdDisplay::ZHENGCHEN_LcdDisplay;

    void SetupUI() override {
        ZHENGCHEN_LcdDisplay::SetupUI();
        DisplayLockGuard lock(this);
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
        lv_label_set_text_fmt(countdown_label_, "倒计时：%ld秒", static_cast<long>(seconds));
        lv_obj_remove_flag(countdown_popup_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(countdown_popup_);
    }
};
