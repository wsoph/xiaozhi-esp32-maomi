#include "maomi_variant.h"

#include <esp_log.h>

#ifndef MAOMI_VARIANT_FORCE_INIT_FAILURE
#define MAOMI_VARIANT_FORCE_INIT_FAILURE 0
#endif

namespace {
constexpr char TAG[] = "MaomiVariant";
}

bool MaomiVariant::Initialize() {
#if MAOMI_VARIANT_FORCE_INIT_FAILURE
    enabled_ = false;
    ESP_LOGE(TAG, "Maomi variant initialization forced to fail; continuing in base mode");
    return false;
#else
    enabled_ = true;
    ESP_LOGI(TAG, "Maomi variant initialized");
    return true;
#endif
}

bool MaomiVariant::IsEnabled() const { return enabled_; }
