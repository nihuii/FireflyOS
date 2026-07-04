#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#include "../../ui/UiComponents.h"
#include "../../services/FileScanService.h"
#include "../../services/StorageService.h"

namespace firefly {

struct FileListItem {
    char name[48]{};
    uint32_t size = 0;
    bool directory = false;
};

class FilesApp {
public:
    static constexpr uint8_t kPageSize = 32;
    static constexpr uint8_t kManagedDirectoryCount = 7;
    static constexpr uint8_t kScanEntriesPerTick = 4;

    bool create(lv_obj_t * parent, UiComponents & components);
    void destroy();
    void show();
    void hide();
    void requestRefresh();
    void bindStorage(StorageService & storage,
                     FileScanService & scanner,
                     bool sd_available);
    void tick();
    void onPageReady();
    void onSdRemoved();
    lv_obj_t * root() const { return root_; }

    static bool canDeleteFile(const char * directory,
                              const FileListItem & item);

private:
    struct RowContext {
        FilesApp * app = nullptr;
        uint8_t index = 0;
    };

    static void rootRowEvent(lv_event_t * event);
    static void fileRowEvent(lv_event_t * event);
    static void nextPageEvent(lv_event_t * event);
    static void backEvent(lv_event_t * event);
    static void confirmDeleteEvent(lv_event_t * event);
    static void cancelDeleteEvent(lv_event_t * event);
    bool scanPage();
    bool deletePending();
    void showRoots();
    void renderPage();
    void showMessage(const char * message);
    void closeConfirmation();

    lv_obj_t * root_ = nullptr;
    lv_obj_t * title_ = nullptr;
    lv_obj_t * status_ = nullptr;
    lv_obj_t * list_ = nullptr;
    lv_obj_t * back_ = nullptr;
    lv_obj_t * confirmation_ = nullptr;
    StorageService * storage_ = nullptr;
    FileScanService * scanner_ = nullptr;
    FileListItem items_[kPageSize]{};
    RowContext row_contexts_[kPageSize]{};
    RowContext root_contexts_[kManagedDirectoryCount]{};
    char current_directory_[16]{};
    uint16_t offset_ = 0;
    uint8_t count_ = 0;
    uint32_t scan_generation_ = 0;
    int8_t pending_delete_ = -1;
    bool has_more_ = false;
    bool refresh_pending_ = false;
    bool sd_available_ = false;
};

}  // namespace firefly
