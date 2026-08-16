#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

#include "UpdateService.h"
#include "UpdateSources.h"
#include "WifiService.h"

namespace firefly {

enum class UpdateCommand : uint8_t {
    Check,
    Start,
    Cancel
};

class UpdateCoordinator {
public:
    static constexpr uint8_t kCommandCapacity = 4;

    UpdateCoordinator(UpdateService & update,
                      WifiService & wifi,
                      UpdateManifestSource & sd_manifest,
                      UpdateSource & sd_package,
                      UpdateManifestSource & https_manifest,
                      UpdateSource & https_package);

    bool postCheck();
    bool postStart();
    bool postCancel();
    void runOnce(uint32_t now_ms, const UpdateRuntimeGate & gate);

private:
    bool post(UpdateCommand command);
    void handleCheck(uint32_t now_ms, const UpdateRuntimeGate & gate);
    void handleStart(uint32_t now_ms, const UpdateRuntimeGate & gate);
    void handleCancel(uint32_t now_ms);
    void serviceOnlineDiscovery(uint32_t now_ms,
                                const UpdateRuntimeGate & gate);
    void releaseWifi(uint32_t now_ms);
    void releaseWifiForTerminal(uint32_t now_ms);
    static UpdateFailure mapManifestFailure(UpdateIoResult result);

    UpdateService & update_;
    WifiService & wifi_;
    UpdateManifestSource & sd_manifest_;
    UpdateSource & sd_package_;
    UpdateManifestSource & https_manifest_;
    UpdateSource & https_package_;
    StaticQueue_t command_queue_storage_{};
    uint8_t command_queue_buffer_[kCommandCapacity * sizeof(UpdateCommand)]{};
    QueueHandle_t command_queue_ = nullptr;
    uint32_t online_started_ms_ = 0;
    bool online_waiting_ = false;
    bool wifi_owned_ = false;
};

static_assert(UpdateCoordinator::kCommandCapacity <= 4,
              "update command queue must remain bounded");

}  // namespace firefly

