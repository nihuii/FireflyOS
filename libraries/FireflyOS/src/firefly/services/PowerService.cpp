#include "PowerService.h"

namespace firefly {

PowerMode PowerService::evaluateIdle(uint32_t now_ms) const {
    uint32_t elapsed_ms = now_ms - last_activity_ms_;
    if(elapsed_ms < timing_.idle_to_dim_ms) {
        return PowerMode::Active;
    }

    elapsed_ms -= timing_.idle_to_dim_ms;
    if(elapsed_ms < timing_.dim_to_glance_ms) {
        return PowerMode::IdleDim;
    }

    elapsed_ms -= timing_.dim_to_glance_ms;
    if(elapsed_ms < timing_.glance_to_off_ms) {
        return PowerMode::Glance;
    }
    return PowerMode::ScreenOff;
}

PowerMode PowerService::evaluate(uint32_t now_ms) const {
    if(!battery_.valid) {
        return evaluateIdle(now_ms);
    }
    if(!isTemperatureSafe(battery_)) {
        return PowerMode::ThermalProtection;
    }
    if(battery_.charging || battery_.vbus_present) {
        return PowerMode::Charging;
    }
    if(battery_.percent <= kCriticalBatteryPercent) {
        return PowerMode::CriticalBattery;
    }
    if(battery_.percent <= kLowBatteryPercent) {
        return PowerMode::LowBattery;
    }
    if(battery_.percent <= kSaverPercent) {
        return PowerMode::Saver;
    }
    return evaluateIdle(now_ms);
}

bool PowerService::isTemperatureSafe(const BatteryState & state) {
    return state.valid &&
           state.temperature_c >= kMinSafeTemperatureC &&
           state.temperature_c <= kMaxSafeTemperatureC;
}

}  // namespace firefly
