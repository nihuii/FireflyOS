#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "../../services/AudioService.h"
#include "../../services/StorageService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

enum class RemoteMediaCommand : uint8_t;

enum class MusicControlTarget : uint8_t {
    LocalLibrary,
    PhoneRemote,
};

class MusicControlSelector {
public:
    MusicControlTarget target() const { return target_; }
    void toggle() {
        target_ = target_ == MusicControlTarget::LocalLibrary
            ? MusicControlTarget::PhoneRemote
            : MusicControlTarget::LocalLibrary;
    }
    void noteScanCompleted(uint16_t) {}
    void noteLocalTrackSelected() {
        target_ = MusicControlTarget::LocalLibrary;
    }

private:
    MusicControlTarget target_ = MusicControlTarget::LocalLibrary;
};

class MusicQueueNavigator {
public:
    static bool play(uint16_t current, uint16_t count, uint16_t & target) {
        if(count == 0) return false;
        target = current < count ? current : 0;
        return true;
    }
    static bool previous(uint16_t current, uint16_t count,
                         uint16_t & target) {
        if(count == 0) return false;
        target = current == 0 || current >= count
            ? static_cast<uint16_t>(count - 1)
            : static_cast<uint16_t>(current - 1);
        return true;
    }
    static bool next(uint16_t current, uint16_t count, uint16_t & target) {
        if(count == 0) return false;
        target = current >= count - 1
            ? 0
            : static_cast<uint16_t>(current + 1);
        return true;
    }
};

struct MusicTrack {
    char path[96]{};
    char title[48]{};
    uint32_t duration_ms = 0;
};

class MusicApp {
public:
    using PhoneMediaCallback = bool (*)(RemoteMediaCommand, uint8_t);
    using LocalVolumeCallback = bool (*)(uint8_t);
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
    void setPhoneMediaCallback(PhoneMediaCallback callback) {
        phone_media_callback_ = callback;
    }
    void setLocalVolumeCallback(LocalVolumeCallback callback,
                                uint8_t current_volume) {
        local_volume_callback_ = callback;
        local_volume_ = current_volume > 100 ? 100 : current_volume;
        if(volume_) {
            lv_slider_set_value(volume_, local_volume_, LV_ANIM_OFF);
        }
    }
    bool applyLocalVolume(uint8_t volume);
    uint8_t localVolume() const { return local_volume_; }
    MusicControlTarget controlTarget() const {
        return control_selector_.target();
    }
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
    static void targetToggleEvent(lv_event_t * event);
    static void volumeEvent(lv_event_t * event);
    bool scanLibrary();
    void scanStep();
    void finishScan();
    bool playIndex(uint16_t index);
    bool dispatchPhoneRemote(RemoteMediaCommand command, uint8_t volume = 0);
    void updateStatus(const char * detail);
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
    PhoneMediaCallback phone_media_callback_ = nullptr;
    LocalVolumeCallback local_volume_callback_ = nullptr;
    MusicControlSelector control_selector_{};
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
    uint8_t local_volume_ = 50;
};

}  // namespace firefly
