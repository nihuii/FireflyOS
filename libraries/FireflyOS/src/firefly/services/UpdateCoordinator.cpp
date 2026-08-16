#include "UpdateCoordinator.h"

namespace firefly {

UpdateCoordinator::UpdateCoordinator(
    UpdateService & update,
    WifiService & wifi,
    UpdateManifestSource & sd_manifest,
    UpdateSource & sd_package,
    UpdateManifestSource & https_manifest,
    UpdateSource & https_package)
    : update_(update),
      wifi_(wifi),
      sd_manifest_(sd_manifest),
      sd_package_(sd_package),
      https_manifest_(https_manifest),
      https_package_(https_package) {
    command_queue_ = xQueueCreateStatic(
        kCommandCapacity, sizeof(UpdateCommand), command_queue_buffer_,
        &command_queue_storage_);
}

bool UpdateCoordinator::post(UpdateCommand command) {
    return command_queue_ &&
        xQueueSend(command_queue_, &command, 0) == pdTRUE;
}

bool UpdateCoordinator::postCheck() { return post(UpdateCommand::Check); }
bool UpdateCoordinator::postStart() { return post(UpdateCommand::Start); }
bool UpdateCoordinator::postCancel() { return post(UpdateCommand::Cancel); }

void UpdateCoordinator::handleCheck(uint32_t now_ms,
                                    const UpdateRuntimeGate & gate) {
    releaseWifi(now_ms);
    if(!update_.reset()) return;
    const UpdateFailure preflight = UpdateService::preflightFailure(gate);
    if(preflight != UpdateFailure::None) {
        update_.reportFailure(preflight);
        return;
    }

    UpdateManifest manifest{};
    const UpdateIoResult sd_result = sd_manifest_.fetch(manifest);
    if(sd_result == UpdateIoResult::Ok) {
        update_.offer(manifest, sd_package_, gate, now_ms);
        return;
    }
    if(sd_result != UpdateIoResult::Unavailable &&
       sd_result != UpdateIoResult::NoEndpoint) {
        update_.reportFailure(mapManifestFailure(sd_result));
        return;
    }
    if(!HttpsManifestSource::configured()) {
        update_.reportFailure(UpdateFailure::NoHttpsEndpoint);
        return;
    }
    if(!wifi_.request(WifiPurpose::Ota, now_ms)) {
        update_.reportFailure(UpdateFailure::SourceUnavailable);
        return;
    }
    wifi_owned_ = true;
    online_waiting_ = true;
    online_started_ms_ = now_ms;
}

void UpdateCoordinator::handleStart(uint32_t now_ms,
                                    const UpdateRuntimeGate & gate) {
    if(!update_.start(gate, now_ms)) releaseWifiForTerminal(now_ms);
}

void UpdateCoordinator::handleCancel(uint32_t now_ms) {
    update_.cancel(now_ms);
    online_waiting_ = false;
    releaseWifi(now_ms);
}

void UpdateCoordinator::serviceOnlineDiscovery(
    uint32_t now_ms,
    const UpdateRuntimeGate & gate) {
    if(!online_waiting_) return;
    const WifiMode mode = wifi_.mode();
    if(mode == WifiMode::Connecting) {
        if(static_cast<uint32_t>(now_ms - online_started_ms_) <
           WifiService::kConnectionTimeoutMs) return;
        online_waiting_ = false;
        update_.reportFailure(UpdateFailure::Timeout);
        releaseWifi(now_ms);
        return;
    }
    if(mode != WifiMode::Connected) {
        online_waiting_ = false;
        update_.reportFailure(UpdateFailure::SourceUnavailable);
        releaseWifi(now_ms);
        return;
    }

    UpdateManifest manifest{};
    const UpdateIoResult result = https_manifest_.fetch(manifest);
    online_waiting_ = false;
    if(result != UpdateIoResult::Ok) {
        update_.reportFailure(mapManifestFailure(result));
        releaseWifi(now_ms);
        return;
    }
    if(!update_.offer(manifest, https_package_, gate, now_ms)) {
        releaseWifi(now_ms);
    }
}

void UpdateCoordinator::releaseWifi(uint32_t now_ms) {
    if(wifi_owned_) wifi_.release(WifiPurpose::Ota, now_ms);
    wifi_owned_ = false;
}

void UpdateCoordinator::releaseWifiForTerminal(uint32_t now_ms) {
    const UpdateState state = update_.snapshot().state;
    const bool keep = state == UpdateState::Available ||
        state == UpdateState::Downloading ||
        state == UpdateState::Verifying ||
        state == UpdateState::Writing;
    if(!keep) releaseWifi(now_ms);
}

UpdateFailure UpdateCoordinator::mapManifestFailure(UpdateIoResult result) {
    if(result == UpdateIoResult::NoEndpoint) {
        return UpdateFailure::NoHttpsEndpoint;
    }
    if(result == UpdateIoResult::Timeout) return UpdateFailure::Timeout;
    if(result == UpdateIoResult::Error) return UpdateFailure::ManifestInvalid;
    return UpdateFailure::SourceUnavailable;
}

void UpdateCoordinator::runOnce(uint32_t now_ms,
                                const UpdateRuntimeGate & gate) {
    UpdateCommand command{};
    if(command_queue_ &&
       xQueueReceive(command_queue_, &command, 0) == pdTRUE) {
        switch(command) {
            case UpdateCommand::Check: handleCheck(now_ms, gate); break;
            case UpdateCommand::Start: handleStart(now_ms, gate); break;
            case UpdateCommand::Cancel: handleCancel(now_ms); break;
        }
    }

    serviceOnlineDiscovery(now_ms, gate);
    const UpdateState state = update_.snapshot().state;
    if(state == UpdateState::Downloading ||
       state == UpdateState::Verifying ||
       state == UpdateState::Writing) {
        update_.tick(now_ms);
    }
    if(wifi_owned_ && state == UpdateState::Available &&
       wifi_.mode() != WifiMode::Connected) {
        update_.cancel(now_ms);
    }
    releaseWifiForTerminal(now_ms);
}

}  // namespace firefly

