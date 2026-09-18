// Run through scripts/test_maomi_pet_home.py: real LVGL without an ESP32/display.
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>
#include "maomi_pet_home.h"

static uint32_t frame[240 * 240];
static uint32_t buffer[240 * 240];
static void Flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    const auto* source = reinterpret_cast<const uint32_t*>(pixels);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            frame[y * 240 + x] = *source++;
    lv_display_flush_ready(display);
}
static void Save(const char* path) {
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);
    std::ofstream out(path, std::ios::binary);
    out << "P6\n240 240\n255\n";
    for (auto pixel : frame) {
        const char rgb[] = {char(pixel >> 16), char(pixel >> 8), char(pixel)};
        out.write(rgb, 3);
    }
    assert(out.good());
}
int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<char> font_data{std::istreambuf_iterator<char>(input), {}};
    assert(!font_data.empty());
    lv_init();
    auto* display = lv_display_create(240, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, Flush);
    auto* old_font = lv_tiny_ttf_create_data(font_data.data(), font_data.size(), 20);
    auto* new_font = lv_tiny_ttf_create_data(font_data.data(), font_data.size(), 20);
    assert(old_font && new_font);
    lv_obj_set_style_text_font(lv_screen_active(), old_font, 0);
    MaomiPetHome home;
    maomi::PetHomeView view;
    view.adopted = true;
    view.name = "小橘";
    home.Update(true, view, 200);
    Save("kitten.ppm");
    // Regression for the device's previous boot crash: release the font after
    // rebinding the screen. Every label must inherit the replacement safely.
    lv_obj_set_style_text_font(lv_screen_active(), new_font, 0);
    lv_tiny_ttf_destroy(old_font);
    view.age_days = 30;
    view.care_days = 21;
    view.coins = 88;
    view.life.outfit = 2;
    view.life.room = 2;
    view.life.owned = 63;
    home.Update(true, view, 400);
    Save("adult.ppm");
    view.sleeping = true;
    home.Update(true, view, 600);
    Save("sleeping.ppm");
    // Timers/reminders can hide and restore the existing home, without recreating it.
    home.Update(false, view, 800);
    Save("hidden.ppm");
    home.Update(true, view, 1000);
    Save("resumed.ppm");
    for (uint64_t ms = 1200; ms < 15000; ms += 200) {
        home.Update(true, view, ms);
        lv_tick_inc(200);
        lv_timer_handler();
    }
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_text_font(lv_screen_active(), LV_FONT_DEFAULT, 0);
    lv_tiny_ttf_destroy(new_font);
    lv_display_delete(display);
    lv_deinit();
    std::cout << "LVGL home render, theme font replacement and resume passed\n";
}
