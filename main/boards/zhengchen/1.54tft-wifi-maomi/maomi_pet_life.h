#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace maomi {

struct PetLifeState {
    int64_t updated_at = 0;
    int64_t sleep_until = 0;
    int64_t awake_until = 0;
    int64_t last_play = 0;
    int64_t last_pet = 0;
    int32_t supply_date = 0;
    int32_t last_pet_date = 0;
    int32_t last_play_date = 0;
    uint32_t pet_days = 0;
    uint32_t play_days = 0;
    uint32_t owned = 0;
    uint8_t happiness = 70;
    uint8_t energy = 100;
    uint8_t hydration = 100;
    uint8_t health = 100;
    uint8_t outfit = 0;
    uint8_t room = 0;
};

enum class PetItemKind { kFood, kLitter, kSnack, kToy, kOutfit, kRoom };
struct PetItem {
    const char* id;
    const char* name;
    uint16_t price;
    uint8_t care_days;
    PetItemKind kind;
    uint8_t code;
    uint32_t mask;
};
inline constexpr std::array<PetItem, 9> kPetItems{{
    {"food", "猫粮", 4, 0, PetItemKind::kFood, 0, 0},
    {"litter", "猫砂", 2, 0, PetItemKind::kLitter, 0, 0},
    {"snack", "零食", 12, 0, PetItemKind::kSnack, 0, 0},
    {"ball", "小球", 12, 0, PetItemKind::kToy, 1, 1},
    {"wand", "逗猫棒", 24, 7, PetItemKind::kToy, 2, 2},
    {"scarf", "围巾", 20, 7, PetItemKind::kOutfit, 1, 4},
    {"star_hat", "星星帽", 36, 21, PetItemKind::kOutfit, 2, 8},
    {"cushion", "软垫", 18, 0, PetItemKind::kRoom, 1, 16},
    {"cat_bed", "小猫窝", 32, 7, PetItemKind::kRoom, 2, 32},
}};

inline const PetItem* FindPetItem(std::string_view id) {
    for (const auto& item : kPetItems)
        if (id == item.id)
            return &item;
    return nullptr;
}
inline bool PetSleeping(const PetLifeState& life, int64_t epoch, int hour) {
    if (life.awake_until > epoch)
        return false;
    if (life.sleep_until > epoch)
        return true;
    return hour >= 21 || hour < 7;
}
inline const char* PetGrowth(uint32_t days) {
    return days >= 21 ? "adult" : days >= 7 ? "young" : "kitten";
}
inline const char* PetPersonality(const PetLifeState& life) {
    if (life.play_days >= 3 && life.play_days > life.pet_days)
        return "playful";
    if (life.pet_days >= 3 && life.pet_days >= life.play_days)
        return "gentle";
    return "curious";
}
inline uint8_t PetMeter(int value) { return static_cast<uint8_t>(std::clamp(value, 0, 100)); }

// Bounded hourly simulation, including night sleep during a long absence. The engine owns
// persistence and only supplies validated time. Backwards time never replenishes meters.
inline void AdvancePetLife(PetLifeState& life, int64_t epoch, int hour, int satiety,
                           int64_t food_decay_at, int poop) {
    if (!life.updated_at) {
        life.updated_at = epoch;
        return;
    }
    if (epoch <= life.updated_at)
        return;
    if (epoch - life.updated_at > 72 * 3600)
        life.updated_at = epoch - 72 * 3600;
    while (epoch - life.updated_at >= 3600) {
        life.updated_at += 3600;
        const int at_hour = (hour - int((epoch - life.updated_at) / 3600) % 24 + 24) % 24;
        const bool sleeping = PetSleeping(life, life.updated_at, at_hour);
        life.energy = PetMeter(life.energy + (sleeping ? 12 : -4));
        life.hydration = PetMeter(life.hydration - 3);
        if (!sleeping)
            life.happiness = PetMeter(life.happiness - 2);
        const auto hunger_loss = std::max<int64_t>(0, life.updated_at - food_decay_at) / 1728;
        const bool neglected = satiety - hunger_loss < 25 || life.hydration < 25 || poop >= 2;
        life.health =
            static_cast<uint8_t>(std::clamp(int(life.health) + (neglected ? -4 : 2), 40, 100));
    }
}

}  // namespace maomi
