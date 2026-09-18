#pragma once

#include <string>
#include "maomi_pet_core.h"
#include "maomi_pet_life.h"

namespace maomi {
struct PetHomeView {
    bool adopted = false;
    bool sleeping = false;
    bool birthday = false;
    uint32_t age_days = 0;
    uint32_t care_days = 0;
    uint32_t coins = 0;
    uint8_t satiety = 100;
    uint8_t poop = 0;
    PetLifeState life;
    std::string name;
    std::string mood;
};

inline bool PetHomeVisible(DeviceState state, PetPriority priority, bool timer, bool learning,
                           bool hazard) {
    return state == kDeviceStateIdle && priority >= PetPriority::kAutonomous && !timer &&
           !learning && !hazard;
}
inline const char* PetHomeHint(const PetHomeView& view) {
    if (view.life.health < 60)
        return "说：带猫咪看医生";
    if (view.sleeping)
        return "正在睡觉  说：起床";
    if (view.satiety < 25)
        return "说：喂猫粮";
    if (view.poop)
        return "说：铲屎";
    if (view.life.hydration < 25)
        return "说：喝水";
    if (view.life.energy < 25)
        return "说：睡一会儿";
    if (view.life.happiness < 30)
        return "说：摸摸猫咪";
    if (view.birthday)
        return "生日快乐！";
    return "说：开始背单词";
}
}  // namespace maomi
