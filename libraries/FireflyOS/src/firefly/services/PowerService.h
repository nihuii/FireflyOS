#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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

enum class WakeSource : uint8_t {
    Boot,
    PowerButton,
    RtcAlarm,
    Imu,
    Charger,
    Count
};

struct WakeVerification {
    uint16_t attempts = 0;
    uint16_t successes = 0;
};

struct SleepHooks {
    SleepHooks(bool (*prepare_hook)() = nullptr,
               void (*restore_hook)() = nullptr)
        : prepare(prepare_hook), restore(restore_hook) {}

    bool (*prepare)();
    void (*restore)();
};

enum class SleepAttemptResult : uint8_t {
    Entered,
    GateClosed,
    PrepareFailed,
    EnterFailed
};

class PowerService {
public:
    static constexpr int16_t kSaverPercent = 25;
    static constexpr int16_t kLowBatteryPercent = 15;
    static constexpr int16_t kCriticalBatteryPercent = 5;
    static constexpr int16_t kMinSafeTemperatureC = 0;
    static constexpr int16_t kMaxSafeTemperatureC = 45;

    PowerService();
    void configure(const PowerTiming & timing);
    PowerTiming timing() const;
    void onActivity(uint32_t now_ms);
    uint32_t lastActivityMs() const;
    void setBatteryState(const BatteryState & state);
    BatteryState batteryState() const;
    bool allowsWifiSession(bool high_power) const;
    void setWifiSessionActive(bool active);
    bool wifiSessionActive() const;
    PowerMode evaluateIdle(uint32_t now_ms) const;
    PowerMode evaluate(uint32_t now_ms) const;
    static bool isTemperatureSafe(const BatteryState & state);
    void setSleepHooks(const SleepHooks & hooks);
    void recordWakeVerification(WakeSource source,
                                uint16_t attempts,
                                uint16_t successes);
    WakeVerification wakeVerification(WakeSource source) const;
    bool canEnterLightSleep() const;
    bool prepareForLightSleep();
    void restoreFromLightSleep();
    SleepAttemptResult attemptLightSleep(bool (*enter)());

private:
    static constexpr uint8_t kWakeSourceCount =
        static_cast<uint8_t>(WakeSource::Count);
    PowerTiming timing_{};
    BatteryState battery_{};
    uint32_t last_activity_ms_ = 0;
    SleepHooks sleep_hooks_{};
    WakeVerification wake_verification_[kWakeSourceCount]{};
    bool sleep_prepared_ = false;
    bool wifi_session_active_ = false;
    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace firefly
