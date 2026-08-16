#include "SdCardDevice.h"

#include <SD_MMC.h>
#include <stdio.h>
#include <string.h>

namespace firefly {
namespace {

const char * const kManagedDirectories[] = {
    "/FireflyOS/Music",
    "/FireflyOS/Recordings",
    "/FireflyOS/Pictures",
    "/FireflyOS/Themes",
    "/FireflyOS/Updates",
    "/FireflyOS/Backups",
    "/FireflyOS/Logs",
};

bool hasManagedTopLevel(const char * path) {
    static const char * const names[] = {
        "Music", "Recordings", "Pictures", "Themes",
        "Updates", "Backups", "Logs",
    };
    for(const char * name : names) {
        const size_t length = strlen(name);
        if(strncmp(path, name, length) == 0 &&
           (path[length] == '\0' || path[length] == '/')) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool SdFailureMonitor::noteResult(bool success) {
    if(success) {
        consecutive_failures_ = 0;
        return false;
    }
    if(consecutive_failures_ < UINT8_MAX) ++consecutive_failures_;
    return consecutive_failures_ == 2;
}

bool SdCardDevice::begin() {
    if(mounted()) end();
    failure_monitor_.reset();
    removed_event_pending_.store(false, std::memory_order_release);

    if(!SD_MMC.setPins(kClockPin, kCommandPin, kDataPin) ||
       !SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 5) ||
       SD_MMC.cardType() == CARD_NONE) {
        SD_MMC.end();
        return false;
    }
    mounted_.store(true, std::memory_order_release);
    if(!ensureFireflyDirectories()) {
        end();
        return false;
    }
    failure_monitor_.reset();
    return true;
}

void SdCardDevice::end() {
    if(mounted()) SD_MMC.end();
    mounted_.store(false, std::memory_order_release);
    failure_monitor_.reset();
}

uint64_t SdCardDevice::totalBytes() const {
    return validateSession() ? SD_MMC.totalBytes() : 0;
}

uint64_t SdCardDevice::usedBytes() const {
    return validateSession() ? SD_MMC.usedBytes() : 0;
}

bool SdCardDevice::ensureFireflyDirectories() {
    if(!mounted()) return false;
    if(!SD_MMC.exists("/FireflyOS") && !SD_MMC.mkdir("/FireflyOS")) {
        noteIoResult(false);
        return false;
    }
    for(const char * path : kManagedDirectories) {
        const bool ok = SD_MMC.exists(path) || SD_MMC.mkdir(path);
        noteIoResult(ok);
        if(!ok || !mounted()) return false;
    }
    return true;
}

bool SdCardDevice::exists(const char * relative_path) const {
    char path[sizeof("/FireflyOS/") + kMaxRelativePath];
    if(!makeManagedPath(relative_path, path, sizeof(path)) ||
       !validateSession()) {
        return false;
    }
    return SD_MMC.exists(path);
}

bool SdCardDevice::validateSession() const {
    if(!mounted()) return false;
    const bool present = SD_MMC.cardType() != CARD_NONE;
    noteIoResult(present);
    return present && mounted();
}

bool SdCardDevice::takeRemovedEvent() {
    return removed_event_pending_.exchange(
        false, std::memory_order_acq_rel);
}

fs::FS & SdCardDevice::filesystem() {
    return SD_MMC;
}

bool SdCardDevice::isSafeRelativePath(const char * relative_path) {
    if(!relative_path || !relative_path[0] || relative_path[0] == '/' ||
       !hasManagedTopLevel(relative_path)) {
        return false;
    }
    const size_t length = strlen(relative_path);
    if(length > kMaxRelativePath || relative_path[length - 1] == '/') {
        return false;
    }

    const char * component = relative_path;
    for(const char * cursor = relative_path; ; ++cursor) {
        const char value = *cursor;
        if(value == '\\' || value == ':' ||
           (static_cast<unsigned char>(value) < 0x20 && value != '\0')) {
            return false;
        }
        if(value == '/' || value == '\0') {
            const size_t component_length = static_cast<size_t>(cursor - component);
            if(component_length == 0 ||
               (component_length == 1 && component[0] == '.') ||
               (component_length == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if(value == '\0') break;
            component = cursor + 1;
        }
    }
    return true;
}

bool SdCardDevice::makeManagedPath(const char * relative_path,
                                   char * out,
                                   size_t out_size) {
    if(!out || !isSafeRelativePath(relative_path)) return false;
    const int written = snprintf(out, out_size, "/FireflyOS/%s", relative_path);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

void SdCardDevice::noteIoResult(bool success) const {
    if(!mounted() || !failure_monitor_.noteResult(success)) return;
    SD_MMC.end();
    mounted_.store(false, std::memory_order_release);
    removed_event_pending_.store(true, std::memory_order_release);
}

}  // namespace firefly
