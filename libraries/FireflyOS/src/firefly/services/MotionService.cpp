#include "MotionService.h"

#include <math.h>

namespace firefly {

MotionPowerMode MotionPowerPolicy::modeFor(bool screen_off,
                                           bool entering_light_sleep) {
    (void)screen_off;
    return entering_light_sleep
        ? MotionPowerMode::LowPower
        : MotionPowerMode::Normal;
}

bool StepDetector::update(const MotionSample & sample) {
    if(!sample.valid) return false;
    const float magnitude = sqrtf(sample.ax * sample.ax +
                                  sample.ay * sample.ay +
                                  sample.az * sample.az);
    if(!filter_initialized_) {
        filtered_magnitude_ = magnitude;
        filter_initialized_ = true;
    } else {
        filtered_magnitude_ += config_.filter_alpha *
                               (magnitude - filtered_magnitude_);
    }

    if(filtered_magnitude_ <= config_.low_threshold) {
        armed_ = true;
    }
    if(!armed_ || filtered_magnitude_ < config_.high_threshold) {
        return false;
    }

    armed_ = false;
    const uint32_t interval_ms = sample.timestamp_ms - last_step_ms_;
    if(has_last_step_ && interval_ms < config_.min_interval_ms) {
        return false;
    }
    if(has_last_step_ && interval_ms > config_.max_interval_ms) {
        has_last_step_ = false;
    }
    last_step_ms_ = sample.timestamp_ms;
    has_last_step_ = true;
    ++total_steps_;
    return true;
}

bool WristRaiseDetector::update(const MotionSample & sample,
                                const MotionContext & context) {
    if(!sample.valid) return false;
    const float previous_az = previous_az_;
    const bool had_previous = has_previous_;
    previous_az_ = sample.az;
    has_previous_ = true;

    if(context.screen_on || context.charging || context.high_rate_app ||
       !had_previous) {
        return false;
    }
    if(has_last_trigger_ &&
       sample.timestamp_ms - last_trigger_ms_ < kCooldownMs) {
        return false;
    }

    const float angular_velocity = sqrtf(sample.gx * sample.gx +
                                         sample.gy * sample.gy +
                                         sample.gz * sample.gz);
    const bool posture_changed = previous_az < 0.2f && sample.az > 0.55f;
    const bool screen_facing_user = sample.ay < -0.25f && sample.az > 0.55f;
    if(!posture_changed || !screen_facing_user || angular_velocity < 35.0f) {
        return false;
    }

    has_last_trigger_ = true;
    last_trigger_ms_ = sample.timestamp_ms;
    return true;
}

bool MotionService::begin() {
    const bool available = device_.begin();
    portENTER_CRITICAL(&mux_);
    summary_.sensor_available = available;
    portEXIT_CRITICAL(&mux_);
    return available;
}

bool MotionService::setLowPower(bool enabled) {
    return device_.setLowPower(enabled);
}

bool MotionService::poll() {
    return poll(MotionContext{});
}

bool MotionService::poll(const MotionContext & context) {
    return processSample(device_.read(), context);
}

bool MotionService::processSample(const MotionSample & sample,
                                  const MotionContext & context) {
    if(!sample.valid) {
        portENTER_CRITICAL(&mux_);
        if(diagnostics_.invalid_samples < UINT32_MAX) {
            ++diagnostics_.invalid_samples;
        }
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    if(!pushSample(sample)) return false;
    portENTER_CRITICAL(&mux_);
    if(diagnostics_.valid_samples < UINT32_MAX) {
        ++diagnostics_.valid_samples;
    }
    const bool stepped = step_detector_.update(sample);
    const bool wrist_raised = wrist_detector_.update(sample, context);

    summary_.steps = step_detector_.totalSteps();
    diagnostics_.steps = summary_.steps;
    if(stepped) {
        const uint32_t minute_bucket = sample.timestamp_ms / 60000UL;
        if(!has_active_minute_ || minute_bucket != last_active_minute_bucket_) {
            if(summary_.active_minutes < UINT16_MAX) ++summary_.active_minutes;
            last_active_minute_bucket_ = minute_bucket;
            has_active_minute_ = true;
        }
    }
    if(wrist_raised) {
        wrist_raise_pending_ = true;
        if(diagnostics_.wrist_events < UINT32_MAX) {
            ++diagnostics_.wrist_events;
        }
    }
    portEXIT_CRITICAL(&mux_);
    return true;
}

bool MotionService::pushSample(const MotionSample & sample) {
    if(!sample.valid) return false;

    portENTER_CRITICAL(&mux_);
    uint8_t index = 0;
    if(count_ < kSampleCapacity) {
        index = static_cast<uint8_t>((head_ + count_) % kSampleCapacity);
        ++count_;
    } else {
        index = head_;
        head_ = static_cast<uint8_t>((head_ + 1U) % kSampleCapacity);
    }
    samples_[index] = sample;
    portEXIT_CRITICAL(&mux_);
    return true;
}

MotionSample MotionService::sampleAt(uint8_t index) const {
    portENTER_CRITICAL(&mux_);
    const MotionSample sample = index < count_
        ? samples_[(head_ + index) % kSampleCapacity]
        : MotionSample{};
    portEXIT_CRITICAL(&mux_);
    return sample;
}

uint8_t MotionService::sampleCount() const {
    portENTER_CRITICAL(&mux_);
    const uint8_t result = count_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

MotionSample MotionService::latest() const {
    portENTER_CRITICAL(&mux_);
    const MotionSample sample = count_ == 0
        ? MotionSample{}
        : samples_[(head_ + count_ - 1U) % kSampleCapacity];
    portEXIT_CRITICAL(&mux_);
    return sample;
}

MotionSummary MotionService::summary() const {
    portENTER_CRITICAL(&mux_);
    const MotionSummary result = summary_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

MotionDiagnostics MotionService::diagnostics() const {
    portENTER_CRITICAL(&mux_);
    const MotionDiagnostics result = diagnostics_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

bool MotionService::consumeWristRaise() {
    portENTER_CRITICAL(&mux_);
    const bool result = wrist_raise_pending_;
    wrist_raise_pending_ = false;
    portEXIT_CRITICAL(&mux_);
    return result;
}

void MotionService::setDayKey(uint32_t day_key) {
    if(day_key == 0) return;
    portENTER_CRITICAL(&mux_);
    if(day_key == day_key_) {
        portEXIT_CRITICAL(&mux_);
        return;
    }
    step_detector_.reset();
    day_key_ = day_key;
    summary_.steps = 0;
    diagnostics_.steps = 0;
    summary_.active_minutes = 0;
    has_active_minute_ = false;
    portEXIT_CRITICAL(&mux_);
}

void MotionService::restoreDailySummary(uint32_t day_key, uint32_t steps,
                                        uint16_t active_minutes) {
    if(day_key == 0) return;
    portENTER_CRITICAL(&mux_);
    step_detector_.restoreTotalSteps(steps);
    day_key_ = day_key;
    summary_.steps = steps;
    diagnostics_.steps = steps;
    summary_.active_minutes = active_minutes;
    has_active_minute_ = false;
    portEXIT_CRITICAL(&mux_);
}

}  // namespace firefly
