#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "../../services/AudioService.h"
#include "../../services/StorageService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

class RecorderApp {
public:
    static constexpr uint32_t kMinimumFreeBytes = 2UL * 1024UL * 1024UL;

    bool create(lv_obj_t * parent, UiComponents & components,
                AudioService & audio);
    void destroy();
    void show();
    void hide();
    void bindStorage(StorageService & storage, bool sd_available,
                     uint64_t free_bytes);
    void tick(uint32_t now_ms, int64_t epoch_seconds);
    void onSdRemoved();
    void stopForSafety();
    bool recording() const { return recording_; }
    lv_obj_t * root() const { return root_; }

    static bool makeRecordingName(int64_t epoch_seconds,
                                  uint32_t fallback_sequence,
                                  char * out,
                                  size_t out_size);

private:
    static void recordEvent(lv_event_t * event);
    bool startRecording();
    bool finishRecording(bool save);
    void refreshUi(uint32_t now_ms);
    void setStatus(const char * text, bool error = false);

    lv_obj_t * root_ = nullptr;
    lv_obj_t * status_ = nullptr;
    lv_obj_t * timer_ = nullptr;
    lv_obj_t * record_button_ = nullptr;
    lv_obj_t * record_button_label_ = nullptr;
    AudioService * audio_ = nullptr;
    StorageService * storage_ = nullptr;
    uint64_t free_bytes_ = 0;
    int64_t current_epoch_ = 0;
    uint32_t fallback_sequence_ = 1;
    uint32_t started_at_ = 0;
    uint32_t last_ui_update_ = 0;
    char final_path_[128]{};
    char temporary_path_[136]{};
    bool sd_available_ = false;
    bool recording_ = false;
};

}  // namespace firefly
