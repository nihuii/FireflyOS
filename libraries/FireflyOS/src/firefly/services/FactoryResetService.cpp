#include "FactoryResetService.h"

namespace firefly {

uint32_t FactoryResetService::beginRequest() {
    portENTER_CRITICAL(&mux_);
    ++next_generation_;
    if(next_generation_ == 0) ++next_generation_;
    snapshot_ = {};
    snapshot_.state = FactoryResetState::Preview;
    snapshot_.generation = next_generation_;
    const uint32_t generation = snapshot_.generation;
    portEXIT_CRITICAL(&mux_);
    return generation;
}

bool FactoryResetService::confirmInternal(uint32_t generation) {
    portENTER_CRITICAL(&mux_);
    const bool valid = snapshot_.state == FactoryResetState::Preview &&
        generation != 0 && generation == snapshot_.generation;
    if(valid) {
        snapshot_.state = FactoryResetState::InternalConfirmed;
        snapshot_.failure = FactoryResetFailure::None;
    }
    portEXIT_CRITICAL(&mux_);
    return valid;
}

bool FactoryResetService::confirmSdErase(uint32_t generation) {
    portENTER_CRITICAL(&mux_);
    const bool valid = snapshot_.state == FactoryResetState::InternalConfirmed &&
        generation != 0 && generation == snapshot_.generation;
    if(valid) {
        snapshot_.state = FactoryResetState::SdConfirmed;
        snapshot_.erase_sd = true;
    }
    portEXIT_CRITICAL(&mux_);
    return valid;
}

bool FactoryResetService::execute(bool erase_sd) {
    portENTER_CRITICAL(&mux_);
    const bool confirmed = erase_sd
        ? snapshot_.state == FactoryResetState::SdConfirmed && snapshot_.erase_sd
        : snapshot_.state == FactoryResetState::InternalConfirmed;
    if(!confirmed) {
        if(snapshot_.state != FactoryResetState::Completed &&
           snapshot_.state != FactoryResetState::Failed) {
            snapshot_.failure = FactoryResetFailure::InvalidConfirmation;
        }
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    snapshot_.state = FactoryResetState::Running;
    const uint32_t generation = snapshot_.generation;
    portEXIT_CRITICAL(&mux_);

    bool internal_ok = true;
    internal_ok = owners_.clearPairing() && internal_ok;
    internal_ok = owners_.clearWifi() && internal_ok;
    internal_ok = owners_.clearNotifications() && internal_ok;
    internal_ok = owners_.clearWeather() && internal_ok;
    internal_ok = owners_.clearSettings() && internal_ok;
    internal_ok = owners_.clearCaches() && internal_ok;
    const bool sd_ok = !erase_sd || owners_.clearManagedSdRoot();

    portENTER_CRITICAL(&mux_);
    if(snapshot_.state != FactoryResetState::Running ||
       snapshot_.generation != generation) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    if(!internal_ok || !sd_ok) {
        snapshot_.state = FactoryResetState::Failed;
        snapshot_.failure = !internal_ok
            ? FactoryResetFailure::InternalClearFailed
            : FactoryResetFailure::SdClearFailed;
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    portEXIT_CRITICAL(&mux_);

    const bool rebooting = rebooter_.requestReboot();
    portENTER_CRITICAL(&mux_);
    if(snapshot_.state != FactoryResetState::Running ||
       snapshot_.generation != generation) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    snapshot_.state = rebooting ? FactoryResetState::Completed
                                : FactoryResetState::Failed;
    snapshot_.failure = rebooting ? FactoryResetFailure::None
                                  : FactoryResetFailure::RebootFailed;
    portEXIT_CRITICAL(&mux_);
    return rebooting;
}

bool FactoryResetService::cancel() {
    portENTER_CRITICAL(&mux_);
    const bool allowed = snapshot_.state == FactoryResetState::Preview ||
        snapshot_.state == FactoryResetState::InternalConfirmed ||
        snapshot_.state == FactoryResetState::SdConfirmed;
    if(allowed) snapshot_ = {};
    portEXIT_CRITICAL(&mux_);
    return allowed;
}

FactoryResetSnapshot FactoryResetService::snapshot() const {
    portENTER_CRITICAL(&mux_);
    const FactoryResetSnapshot copy = snapshot_;
    portEXIT_CRITICAL(&mux_);
    return copy;
}

}  // namespace firefly
