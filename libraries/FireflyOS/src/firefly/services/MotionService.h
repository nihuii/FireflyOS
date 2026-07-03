#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

#include "../hal/DeviceInterfaces.h"

namespace firefly {

struct StepCounterConfig {
    StepCounterConfig(float alpha = 0.35f,
                      float high = 1.15f,
                      float low = 1.02f,
                      uint16_t min_interval = 250,
                      uint16_t max_interval = 1200)
        : filter_alpha(alpha),
          high_threshold(high),
          low_threshold(low),
          min_interval_ms(min_interval),
          max_interval_ms(max_interval) {}

    float filter_alpha;
    float high_threshold;
    float low_threshold;
    uint16_t min_interval_ms;
    uint16_t max_interval_ms;
};

class StepDetector {
public:
    explicit StepDetector(const StepCounterConfig & config = {})
        : config_(config) {}

    bool update(const MotionSample & sample);
    uint32_t totalSteps() const { return total_steps_; }
    void reset() { total_steps_ = 0; has_last_step_ = false; }
    void restoreTotalSteps(uint32_t steps) {
        total_steps_ = steps;
        has_last_step_ = false;
    }

private:
    StepCounterConfig config_{};
    float filtered_magnitude_ = 1.0f;
    bool filter_initialized_ = false;
    bool armed_ = true;
    bool has_last_step_ = false;
    uint32_t last_step_ms_ = 0;
    uint32_t total_steps_ = 0;
};

struct MotionContext {
    bool screen_on = false;
    bool charging = false;
    bool high_rate_app = false;
};

class WristRaiseDetector {
public:
    static constexpr uint32_t kCooldownMs = 3000;

    bool update(const MotionSample & sample, const MotionContext & context);

private:
    float previous_az_ = 0.0f;
    bool has_previous_ = false;
    bool has_last_trigger_ = false;
    uint32_t last_trigger_ms_ = 0;
};

struct MotionSummary {
    uint32_t steps = 0;
    uint16_t active_minutes = 0;
    bool sensor_available = false;
};

enum class MotionPowerMode : uint8_t {
    Normal,
    LowPower,
};

class MotionPowerPolicy {
public:
    static MotionPowerMode modeFor(bool screen_off,
                                   bool entering_light_sleep);
};

struct MotionDiagnostics {
    uint32_t valid_samples = 0;
    uint32_t invalid_samples = 0;
    uint32_t steps = 0;
    uint32_t wrist_events = 0;
};

class MotionService {
public:
    static constexpr uint8_t kSampleCapacity = 32;

    explicit MotionService(MotionDevice & device) : device_(device) {}

    bool begin();
    bool setLowPower(bool enabled);
    bool poll();
    bool poll(const MotionContext & context);
    bool processSample(const MotionSample & sample,
                       const MotionContext & context);
    bool pushSample(const MotionSample & sample);
    uint8_t sampleCount() const;
    MotionSample sampleAt(uint8_t index) const;
    MotionSample latest() const;
    MotionSummary summary() const;
    MotionDiagnostics diagnostics() const;
    bool consumeWristRaise();
    void setDayKey(uint32_t day_key);
    void restoreDailySummary(uint32_t day_key, uint32_t steps,
                             uint16_t active_minutes);

private:
    MotionDevice & device_;
    MotionSample samples_[kSampleCapacity]{};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
    StepDetector step_detector_{};
    WristRaiseDetector wrist_detector_{};
    MotionSummary summary_{};
    MotionDiagnostics diagnostics_{};
    uint32_t day_key_ = 0;
    uint32_t last_active_minute_bucket_ = 0;
    bool has_active_minute_ = false;
    bool wrist_raise_pending_ = false;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace firefly
