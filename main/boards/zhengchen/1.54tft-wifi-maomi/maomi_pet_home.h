#pragma once

#ifdef CONFIG_MAOMI_LEARNING
#include <lvgl.h>
#include "maomi_pet_presentation.h"

// Owned by MaomiLcdDisplay. All calls are made under the display lock. Text inherits
// the screen font, including after Assets replaces the font during boot/theme changes.
class MaomiPetHome {
    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* detail_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    lv_obj_t* meters_ = nullptr;
    lv_obj_t* cat_ = nullptr;
    lv_obj_t* eyes_[2]{};
    lv_obj_t* scarf_[2]{};
    lv_obj_t* hat_[4]{};
    lv_obj_t* bed_ = nullptr;
    lv_obj_t* cushion_ = nullptr;
    lv_obj_t* toy_ = nullptr;
    lv_obj_t* moon_ = nullptr;
    lv_obj_t* dirt_ = nullptr;
    bool visible_ = false;
    std::string last_;
    uint64_t blink_ = 0;

    static lv_obj_t* Shape(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color,
                           int radius = 0) {
        auto* obj = lv_obj_create(parent);
        lv_obj_remove_style_all(obj);
        lv_obj_set_pos(obj, x, y);
        lv_obj_set_size(obj, w, h);
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(obj, radius, 0);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        return obj;
    }
    static lv_obj_t* Label(lv_obj_t* parent, int y) {
        auto* label = lv_label_create(parent);
        lv_obj_set_width(label, 228);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x493226), 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(label, 6, y);
        lv_label_set_text(label, "");
        return label;
    }
    static void Show(lv_obj_t* obj, bool show) {
        if (show)
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    void Setup() {
        root_ = Shape(lv_screen_active(), 0, 0, 240, 240, 0xFFF1DC);
        Shape(root_, 0, 162, 240, 78, 0xEED9BE);
        Shape(root_, 176, 58, 44, 55, 0xFFFFFF, 12);
        Shape(root_, 181, 63, 34, 45, 0xB9DDE3, 8);
        Shape(root_, 197, 63, 2, 45, 0xFFFFFF);
        Shape(root_, 181, 84, 34, 2, 0xFFFFFF);
        moon_ = Shape(root_, 185, 68, 11, 11, 0xFFE99A, 6);
        bed_ = Shape(root_, 53, 117, 134, 66, 0xA5BFA9, 25);
        cushion_ = Shape(root_, 48, 157, 144, 25, 0xDCA568, 12);
        // Local cat coordinates allow real growth in size without a separate bitmap per age.
        cat_ = Shape(root_, 66, 70, 108, 108, 0, 0);
        lv_obj_set_style_bg_opa(cat_, LV_OPA_TRANSP, 0);
        Shape(cat_, 24, 49, 62, 52, 0xECA24D, 25);
        Shape(cat_, 12, 5, 28, 42, 0xECA24D, 6);
        Shape(cat_, 68, 5, 28, 42, 0xECA24D, 6);
        Shape(cat_, 18, 10, 16, 24, 0xEDB0A1, 5);
        Shape(cat_, 74, 10, 16, 24, 0xEDB0A1, 5);
        Shape(cat_, 12, 20, 84, 64, 0xFFBD69, 30);
        Shape(cat_, 41, 21, 5, 17, 0xCC7932, 2);
        Shape(cat_, 51, 20, 5, 20, 0xCC7932, 2);
        Shape(cat_, 61, 21, 5, 17, 0xCC7932, 2);
        eyes_[0] = Shape(cat_, 31, 47, 7, 10, 0x493226, 4);
        eyes_[1] = Shape(cat_, 70, 47, 7, 10, 0x493226, 4);
        Shape(cat_, 49, 61, 10, 6, 0xCF7C77, 3);
        Shape(cat_, 52, 67, 3, 7, 0x493226, 1);
        Shape(cat_, 22, 90, 24, 14, 0xFFBD69, 7);
        Shape(cat_, 63, 90, 24, 14, 0xFFBD69, 7);
        scarf_[0] = Shape(cat_, 21, 78, 66, 10, 0xD77D70, 4);
        scarf_[1] = Shape(cat_, 70, 85, 12, 19, 0xD77D70, 3);
        hat_[0] = Shape(cat_, 24, 8, 60, 12, 0x6B91B3, 4);
        hat_[1] = Shape(cat_, 32, 0, 44, 14, 0x6B91B3, 5);
        hat_[2] = Shape(cat_, 49, 1, 10, 10, 0xFFE99A, 1);
        hat_[3] = Shape(cat_, 49, 1, 10, 10, 0xFFE99A, 1);
        lv_obj_set_style_transform_rotation(hat_[3], 450, 0);
        toy_ = Shape(root_, 188, 157, 18, 18, 0x6B91B3, 9);
        Shape(toy_, 6, 1, 5, 15, 0xD5E5F0, 2);
        dirt_ = Shape(root_, 26, 162, 15, 11, 0x936C4B, 5);
        title_ = Label(root_, 5);
        detail_ = Label(root_, 30);
        meters_ = Label(root_, 184);
        hint_ = Label(root_, 211);
        Show(root_, false);
    }

public:
    void Update(bool visible, const maomi::PetHomeView& view, uint64_t now_ms) {
        if (!root_)
            Setup();
        visible = visible && view.adopted;
        if (visible != visible_) {
            Show(root_, visible);
            if (visible)
                lv_obj_move_foreground(root_);
            visible_ = visible;
        }
        if (!visible)
            return;
        const auto& life = view.life;
        const auto key = view.name + ":" + std::to_string(view.age_days) + ":" +
                         std::to_string(view.care_days) + ":" + std::to_string(view.coins) + ":" +
                         std::to_string(view.satiety) + ":" + std::to_string(view.poop) + ":" +
                         std::to_string(life.energy) + ":" + std::to_string(life.health) + ":" +
                         std::to_string(life.hydration) + ":" + std::to_string(life.happiness) +
                         ":" + std::to_string(life.outfit) + ":" + std::to_string(life.room) + ":" +
                         std::to_string(life.owned) + ":" + std::to_string(view.sleeping) + ":" +
                         std::to_string(view.birthday);
        if (key != last_) {
            last_ = key;
            lv_label_set_text(title_, view.name.c_str());
            const char* stage = view.care_days >= 21  ? "成年猫"
                                : view.care_days >= 7 ? "少年猫"
                                                      : "幼猫";
            lv_label_set_text_fmt(detail_, "%s  %lu天  %lu币", stage,
                                  static_cast<unsigned long>(view.age_days),
                                  static_cast<unsigned long>(view.coins));
            const int scale = view.care_days >= 21 ? 256 : view.care_days >= 7 ? 232 : 208;
            lv_obj_set_style_transform_scale(cat_, scale, 0);
            lv_obj_set_pos(cat_, 120 - 108 * scale / 512, 178 - 108 * scale / 256);
            Show(bed_, life.room == 2);
            Show(cushion_, life.room != 0);
            Show(toy_, (life.owned & 3) != 0);
            Show(moon_, view.sleeping);
            Show(dirt_, view.poop > 0);
            for (auto* obj : scarf_)
                Show(obj, life.outfit == 1);
            for (auto* obj : hat_)
                Show(obj, life.outfit == 2);
            lv_obj_set_style_bg_color(root_, lv_color_hex(view.sleeping ? 0xD7DFE6 : 0xFFF1DC), 0);
            const int phase = (now_ms / 3000) % 2;
            if (phase == 0)
                lv_label_set_text_fmt(meters_, "饱食 %u  饮水 %u", view.satiety, life.hydration);
            else
                lv_label_set_text_fmt(meters_, "精力 %u  健康 %u", life.energy, life.health);
            lv_label_set_text(hint_, maomi::PetHomeHint(view));
        }
        const uint64_t blink = now_ms / 200;
        if (blink != blink_) {
            blink_ = blink;
            const bool closed = view.sleeping || blink % 25 == 0;
            for (auto* eye : eyes_)
                lv_obj_set_height(eye, closed ? 3 : 10);
            // Changing this small label does not allocate another full-screen image.
            if (blink % 15 == 0) {
                if ((blink / 15) % 2 == 0)
                    lv_label_set_text_fmt(meters_, "饱食 %u  饮水 %u", view.satiety,
                                          life.hydration);
                else
                    lv_label_set_text_fmt(meters_, "精力 %u  健康 %u", life.energy, life.health);
            }
        }
    }
};
#endif
