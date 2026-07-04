#include "FileScanService.h"

#include <stdio.h>
#include <string.h>

namespace firefly {
namespace {

const char * baseName(const char * path) {
    if(!path) return "";
    const char * slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool validDirectoryName(const char * directory) {
    static const char * const names[] = {
        "Music", "Recordings", "Pictures", "Themes",
        "Updates", "Backups", "Logs",
    };
    if(!directory) return false;
    for(const char * name : names) {
        if(strcmp(directory, name) == 0) return true;
    }
    return false;
}

}  // namespace

bool FileScanService::request(StorageService & storage,
                              const char * directory,
                              uint16_t offset,
                              uint32_t generation) {
    if(!validDirectoryName(directory) || !storage.sdAvailable()) return false;
    portENTER_CRITICAL(&mux_);
    pending_storage_ = &storage;
    strlcpy(pending_directory_, directory, sizeof(pending_directory_));
    pending_offset_ = offset;
    pending_generation_ = generation;
    request_pending_ = true;
    cancel_pending_ = false;
    portEXIT_CRITICAL(&mux_);
    return true;
}

void FileScanService::cancel() {
    portENTER_CRITICAL(&mux_);
    request_pending_ = false;
    cancel_pending_ = true;
    event_pending_ = false;
    result_ready_ = false;
    portEXIT_CRITICAL(&mux_);
}

void FileScanService::service(EventBus & events, uint32_t timestamp_ms) {
    bool cancel = false;
    bool start = false;
    bool post_result = false;
    uint32_t generation = 0;
    portENTER_CRITICAL(&mux_);
    cancel = cancel_pending_;
    if(cancel) cancel_pending_ = false;
    start = request_pending_;
    if(start) request_pending_ = false;
    post_result = event_pending_;
    generation = result_.generation;
    portEXIT_CRITICAL(&mux_);

    if(cancel) {
        closeDirectory();
        scanning_ = false;
        return;
    }
    if(post_result) {
        if(events.post(SystemEvent(EventType::FilesPageReady, generation,
                                   timestamp_ms, EventPriority::Normal))) {
            portENTER_CRITICAL(&mux_);
            event_pending_ = false;
            portEXIT_CRITICAL(&mux_);
        }
        return;
    }
    if(start) startPendingRequest();
    if(!scanning_ || !storage_) return;

    for(uint8_t processed = 0; processed < kEntriesPerTick; ++processed) {
        fs::File entry = storage_->openNextManaged(directory_);
        if(!entry) {
            finish(!storage_->sdAvailable());
            return;
        }
        if(skipped_ < working_.offset) {
            ++skipped_;
            storage_->closeManaged(entry);
            continue;
        }
        if(working_.count >= kPageSize) {
            working_.has_more = true;
            storage_->closeManaged(entry);
            finish();
            return;
        }
        FileScanItem & item = working_.items[working_.count++];
        char entry_name[64]{};
        uint64_t entry_size = 0;
        bool entry_is_directory = false;
        const bool metadata_ok =
            storage_->managedFileName(entry, entry_name, sizeof(entry_name)) &&
            storage_->managedFileSize(entry, entry_size) &&
            storage_->managedFileIsDirectory(entry, entry_is_directory);
        if(!metadata_ok) {
            --working_.count;
            storage_->closeManaged(entry);
            if(!storage_->sdAvailable()) {
                finish(true);
                return;
            }
            continue;
        }
        strlcpy(item.name, baseName(entry_name), sizeof(item.name));
        item.size = entry_size > UINT32_MAX ? UINT32_MAX : entry_size;
        item.directory = entry_is_directory;
        storage_->closeManaged(entry);
    }
}

bool FileScanService::takeResult(FileScanPage & page) {
    portENTER_CRITICAL(&mux_);
    if(!result_ready_) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    page = result_;
    result_ready_ = false;
    portEXIT_CRITICAL(&mux_);
    return true;
}

void FileScanService::startPendingRequest() {
    closeDirectory();
    portENTER_CRITICAL(&mux_);
    storage_ = pending_storage_;
    working_ = FileScanPage{};
    working_.offset = pending_offset_;
    working_.generation = pending_generation_;
    char directory_name[sizeof(pending_directory_)]{};
    strlcpy(directory_name, pending_directory_, sizeof(directory_name));
    portEXIT_CRITICAL(&mux_);

    char path[64];
    snprintf(path, sizeof(path), "/FireflyOS/%s", directory_name);
    directory_ = storage_->openManaged(path, FILE_READ);
    skipped_ = 0;
    bool is_directory = false;
    scanning_ = directory_ &&
        storage_->managedFileIsDirectory(directory_, is_directory) &&
        is_directory;
    if(!scanning_) finish(!storage_->sdAvailable());
}

void FileScanService::finish(bool storage_unavailable) {
    closeDirectory();
    scanning_ = false;
    working_.storage_unavailable = storage_unavailable;
    portENTER_CRITICAL(&mux_);
    result_ = working_;
    result_ready_ = true;
    event_pending_ = true;
    portEXIT_CRITICAL(&mux_);
}

void FileScanService::closeDirectory() {
    if(directory_) {
        if(storage_) storage_->closeManaged(directory_);
        else directory_.close();
    }
}

}  // namespace firefly
