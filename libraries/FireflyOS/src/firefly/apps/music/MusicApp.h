#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "../../services/AudioService.h"
#include "../../services/StorageService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

struct MusicTrack {
    char path[96]{};
    char title[48]{};
    uint32_t duration_ms = 0;
};

class MusicApp {
public:
    static constexpr uint16_t kMaxTracks = 128;
    static constexpr uint8_t kScanEntriesPerTick = 4;

    bool create(lv_obj_t * parent, UiComponents & components,
                AudioService & audio);
    void destroy();
    void show();
    void hide();
    void bindStorage(StorageService & storage, bool sd_available);
    void requestRefresh();
    void tick(uint32_t now_ms, bool power_saver);
    void onSdRemoved();
    bool playing() const;
    bool indexLimitReached() const { return index_limit_reached_; }
    lv_obj_t * root() const { return root_; }

private:
    struct RowContext {
        MusicApp * app = nullptr;
        uint16_t index = 0;
    };
    static void trackEvent(lv_event_t * event);
    static void playPauseEvent(lv_event_t * event);
    static void previousEvent(lv_event_t * event);
    static void nextEvent(lv_event_t * event);
    static void refreshEvent(lv_event_t * event);
    static void volumeEvent(lv_event_t * event);
    bool scanLibrary();
    void scanStep();
    void finishScan();
    bool playIndex(uint16_t index);
    void renderList();
    void refreshPlayer(uint32_t now_ms, bool force = false);

    lv_obj_t * root_ = nullptr;
    lv_obj_t * status_ = nullptr;
    lv_obj_t * list_ = nullptr;
    lv_obj_t * track_label_ = nullptr;
    lv_obj_t * progress_ = nullptr;
    lv_obj_t * time_label_ = nullptr;
    lv_obj_t * play_label_ = nullptr;
    lv_obj_t * volume_ = nullptr;
    AudioService * audio_ = nullptr;
    StorageService * storage_ = nullptr;
    fs::File scan_directory_{};
    MusicTrack tracks_[kMaxTracks]{};
    RowContext row_contexts_[5]{};
    uint16_t track_count_ = 0;
    uint16_t current_index_ = 0;
    uint16_t list_start_ = 0;
    uint32_t last_ui_update_ = 0;
    bool refresh_pending_ = false;
    bool sd_available_ = false;
    bool index_limit_reached_ = false;
    bool scan_active_ = false;
};

}  // namespace firefly
