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

void PowerService::recordWakeVerification(WakeSource source,
                                          uint16_t attempts,
                                          uint16_t successes) {
    const uint8_t index = static_cast<uint8_t>(source);
    if(index >= kWakeSourceCount) return;
    wake_verification_[index].attempts = attempts;
    wake_verification_[index].successes =
        successes > attempts ? attempts : successes;
}

WakeVerification PowerService::wakeVerification(WakeSource source) const {
    const uint8_t index = static_cast<uint8_t>(source);
    return index < kWakeSourceCount
        ? wake_verification_[index]
        : WakeVerification{};
}

bool PowerService::canEnterLightSleep() const {
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
    if(!canEnterLightSleep() || !sleep_hooks_.prepare || sleep_prepared_) {
        return false;
    }
    sleep_prepared_ = sleep_hooks_.prepare();
    return sleep_prepared_;
}

void PowerService::restoreFromLightSleep() {
    if(!sleep_prepared_) return;
    sleep_prepared_ = false;
    if(sleep_hooks_.restore) sleep_hooks_.restore();
}

SleepAttemptResult PowerService::attemptLightSleep(bool (*enter)()) {
    if(!canEnterLightSleep()) return SleepAttemptResult::GateClosed;
    if(!prepareForLightSleep()) return SleepAttemptResult::PrepareFailed;
    const bool entered = enter && enter();
    restoreFromLightSleep();
    return entered ? SleepAttemptResult::Entered
                   : SleepAttemptResult::EnterFailed;
}

}  // namespace firefly
