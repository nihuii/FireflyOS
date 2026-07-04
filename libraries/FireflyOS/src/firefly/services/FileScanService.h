#pragma once

#include <stdint.h>

#include "../core/EventBus.h"
#include "StorageService.h"

namespace firefly {

struct FileScanItem {
    char name[48]{};
    uint32_t size = 0;
    bool directory = false;
};

struct FileScanPage {
    FileScanItem items[32]{};
    uint16_t offset = 0;
    uint32_t generation = 0;
    uint8_t count = 0;
    bool has_more = false;
    bool storage_unavailable = false;
};

class FileScanService {
public:
    static constexpr uint8_t kPageSize = 32;
    static constexpr uint8_t kEntriesPerTick = 4;

    bool request(StorageService & storage,
                 const char * directory,
                 uint16_t offset,
                 uint32_t generation);
    void cancel();
    void service(EventBus & events, uint32_t timestamp_ms);
    bool takeResult(FileScanPage & page);

private:
    void startPendingRequest();
    void finish(bool storage_unavailable = false);
    void closeDirectory();

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    StorageService * pending_storage_ = nullptr;
    StorageService * storage_ = nullptr;
    fs::File directory_{};
    FileScanPage working_{};
    FileScanPage result_{};
    char pending_directory_[16]{};
    uint16_t pending_offset_ = 0;
    uint32_t pending_generation_ = 0;
    uint16_t skipped_ = 0;
    bool request_pending_ = false;
    bool cancel_pending_ = false;
    bool scanning_ = false;
    bool result_ready_ = false;
    bool event_pending_ = false;
};

}  // namespace firefly
