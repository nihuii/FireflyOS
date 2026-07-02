#pragma once

#include <stdint.h>

#include "../core/SystemState.h"

namespace firefly {

enum class PowerMode : uint8_t {
    Active,
    IdleDim,
    Glance,
    ScreenOff,
    LightSleep,
    Charging,
    Saver,
    LowBattery,
    CriticalBattery,
    ThermalProtection
};

struct PowerTiming {
    PowerTiming(uint32_t idle_ms = 30000,
                uint32_t dim_ms = 0,
                uint32_t glance_ms = 2000)
        : idle_to_dim_ms(idle_ms),
          dim_to_glance_ms(dim_ms),
          glance_to_off_ms(glance_ms) {}

    uint32_t idle_to_dim_ms;
    uint32_t dim_to_glance_ms;
    uint32_t glance_to_off_ms;
};

class PowerService {
public:
    static constexpr int16_t kSaverPercent = 25;
    static constexpr int16_t kLowBatteryPercent = 15;
    static constexpr int16_t kCriticalBatteryPercent = 5;
    static constexpr int16_t kMinSafeTemperatureC = 0;
    static constexpr int16_t kMaxSafeTemperatureC = 45;

    void configure(const PowerTiming & timing) { timing_ = timing; }
    const PowerTiming & timing() const { return timing_; }
    void onActivity(uint32_t now_ms) { last_activity_ms_ = now_ms; }
    uint32_t lastActivityMs() const { return last_activity_ms_; }
    void setBatteryState(const BatteryState & state) { battery_ = state; }
    const BatteryState & batteryState() const { return battery_; }
    PowerMode evaluateIdle(uint32_t now_ms) const;
    PowerMode evaluate(uint32_t now_ms) const;
    static bool isTemperatureSafe(const BatteryState & state);

private:
    PowerTiming timing_{};
    BatteryState battery_{};
    uint32_t last_activity_ms_ = 0;
};

}  // namespace firefly
