#include "PowerService.h"

namespace firefly {
namespace {

class PowerRecursiveLock {
public:
    explicit PowerRecursiveLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = !mutex_ ||
            xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~PowerRecursiveLock() {
        if(locked_ && mutex_) xSemaphoreGiveRecursive(mutex_);
    }
private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

}  // namespace

PowerService::PowerService() {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
}

void PowerService::configure(const PowerTiming & timing) {
    PowerRecursiveLock lock(mutex_);
    timing_ = timing;
}

PowerTiming PowerService::timing() const {
    PowerRecursiveLock lock(mutex_);
    return timing_;
}

void PowerService::onActivity(uint32_t now_ms) {
    PowerRecursiveLock lock(mutex_);
    last_activity_ms_ = now_ms;
}

uint32_t PowerService::lastActivityMs() const {
    PowerRecursiveLock lock(mutex_);
    return last_activity_ms_;
}

void PowerService::setBatteryState(const BatteryState & state) {
    PowerRecursiveLock lock(mutex_);
    battery_ = state;
}

BatteryState PowerService::batteryState() const {
    PowerRecursiveLock lock(mutex_);
    return battery_;
}

void PowerService::setWifiSessionActive(bool active) {
    PowerRecursiveLock lock(mutex_);
    wifi_session_active_ = active;
}

bool PowerService::wifiSessionActive() const {
    PowerRecursiveLock lock(mutex_);
    return wifi_session_active_;
}

void PowerService::setSleepHooks(const SleepHooks & hooks) {
    PowerRecursiveLock lock(mutex_);
    sleep_hooks_ = hooks;
}

PowerMode PowerService::evaluateIdle(uint32_t now_ms) const {
    PowerRecursiveLock lock(mutex_);
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
    PowerRecursiveLock lock(mutex_);
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

bool PowerService::allowsWifiSession(bool high_power) const {
    PowerRecursiveLock lock(mutex_);
    const bool percent_known = battery_.valid && battery_.percent >= 0 &&
        battery_.percent <= 100;
    if(!percent_known) return !high_power;
    if(battery_.percent <= kCriticalBatteryPercent) return false;
    if(battery_.charging || battery_.vbus_present) return true;
    return !high_power || battery_.percent > kLowBatteryPercent;
}

void PowerService::recordWakeVerification(WakeSource source,
                                          uint16_t attempts,
                                          uint16_t successes) {
    PowerRecursiveLock lock(mutex_);
    const uint8_t index = static_cast<uint8_t>(source);
    if(index >= kWakeSourceCount) return;
    wake_verification_[index].attempts = attempts;
    wake_verification_[index].successes =
        successes > attempts ? attempts : successes;
}

WakeVerification PowerService::wakeVerification(WakeSource source) const {
    PowerRecursiveLock lock(mutex_);
    const uint8_t index = static_cast<uint8_t>(source);
    return index < kWakeSourceCount
        ? wake_verification_[index]
        : WakeVerification{};
}

bool PowerService::canEnterLightSleep() const {
    PowerRecursiveLock lock(mutex_);
    if(wifi_session_active_) return false;
    static const WakeSource required[] = {
        WakeSource::Boot,
        WakeSource::PowerButton,
        WakeSource::RtcAlarm
    };
    for(uint8_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        const WakeVerification verification = wakeVerification(required[i]);
        if(verification.attempts < 100 ||
           verification.successes != verification.attempts) {
            return false;
        }
    }
    return true;
}

bool PowerService::prepareForLightSleep() {
    PowerRecursiveLock lock(mutex_);
    if(!canEnterLightSleep() || !sleep_hooks_.prepare || sleep_prepared_) {
        return false;
    }
    sleep_prepared_ = sleep_hooks_.prepare();
    return sleep_prepared_;
}

void PowerService::restoreFromLightSleep() {
    PowerRecursiveLock lock(mutex_);
    if(!sleep_prepared_) return;
    sleep_prepared_ = false;
    if(sleep_hooks_.restore) sleep_hooks_.restore();
}

SleepAttemptResult PowerService::attemptLightSleep(bool (*enter)()) {
    PowerRecursiveLock lock(mutex_);
    if(!canEnterLightSleep()) return SleepAttemptResult::GateClosed;
    if(!prepareForLightSleep()) return SleepAttemptResult::PrepareFailed;
    const bool entered = enter && enter();
    restoreFromLightSleep();
    return entered ? SleepAttemptResult::Entered
                   : SleepAttemptResult::EnterFailed;
}

}  // namespace firefly
