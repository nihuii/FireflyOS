#include "MusicApp.h"

#include <stdio.h>
#include <string.h>

#include "../../ui/UiTheme.h"
#include "../../services/CompanionSyncService.h"

namespace firefly {
namespace {

bool endsWithWav(const char * name) {
    if(!name) return false;
    const size_t length = strlen(name);
    return length >= 4 &&
        (strcasecmp(name + length - 4, ".wav") == 0);
}

const char * baseName(const char * path) {
    if(!path) return "";
    const char * slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void formatTime(uint32_t ms, char * out, size_t out_size) {
    const uint32_t seconds = ms / 1000UL;
    snprintf(out, out_size, "%02lu:%02lu",
             static_cast<unsigned long>(seconds / 60UL),
             static_cast<unsigned long>(seconds % 60UL));
}

}  // namespace

bool MusicApp::create(lv_obj_t * parent, UiComponents & components,
                      AudioService & audio) {
    LV_UNUSED(components);
    if(!parent) return false;
    audio_ = &audio;
    const UiTokens tokens = UiTheme::fireflyDefault();
    root_ = UiComponents::createPage(parent, tokens);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * title = UiComponents::createTitle(root_, tokens, "Music");
    lv_obj_set_pos(title, 28, 54);
    status_ = lv_label_create(root_);
    lv_obj_set_width(status_, 300);
    lv_label_set_text(
        status_,
        "Target: Local library | Hold Refresh: Phone remote\n"
        "Local WAV library"
    );
    lv_obj_set_style_text_color(status_, lv_color_hex(tokens.text_secondary), 0);
    lv_obj_set_pos(status_, 30, 88);
    lv_obj_t * refresh = lv_btn_create(root_);
    lv_obj_set_size(refresh, 48, 48);
    lv_obj_set_pos(refresh, 334, 48);
    lv_obj_add_event_cb(refresh, refreshEvent, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(
        refresh, targetToggleEvent, LV_EVENT_LONG_PRESSED, this
    );
    lv_obj_t * refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh_label);

    list_ = lv_obj_create(root_);
    lv_obj_set_size(list_, 354, 184);
    lv_obj_set_pos(list_, 28, 112);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_, 0, 0);
    lv_obj_set_style_pad_row(list_, 7, 0);
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_, 0, 0);

    track_label_ = lv_label_create(root_);
    lv_obj_set_width(track_label_, 300);
    lv_obj_set_style_text_align(track_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(track_label_, lv_color_hex(tokens.text_primary), 0);
    lv_label_set_text(track_label_, "Target: Local | Nothing playing");
    lv_obj_align(track_label_, LV_ALIGN_TOP_MID, 0, 310);
    progress_ = lv_bar_create(root_);
    lv_obj_set_size(progress_, 300, 8);
    lv_obj_align(progress_, LV_ALIGN_TOP_MID, 0, 344);
    lv_bar_set_range(progress_, 0, 1000);
    time_label_ = lv_label_create(root_);
    lv_label_set_text(time_label_, "Hold Refresh: Phone remote");
    lv_obj_set_style_text_color(time_label_, lv_color_hex(tokens.text_secondary), 0);
    lv_obj_align(time_label_, LV_ALIGN_TOP_MID, 0, 360);

    lv_obj_t * previous = lv_btn_create(root_);
    lv_obj_set_size(previous, 56, 48);
    lv_obj_set_pos(previous, 76, 394);
    lv_obj_add_event_cb(previous, previousEvent, LV_EVENT_CLICKED, this);
    lv_obj_t * previous_label = lv_label_create(previous);
    lv_label_set_text(previous_label, LV_SYMBOL_PREV);
    lv_obj_center(previous_label);
    lv_obj_t * play = lv_btn_create(root_);
    lv_obj_set_size(play, 72, 56);
    lv_obj_set_pos(play, 169, 390);
    lv_obj_add_event_cb(play, playPauseEvent, LV_EVENT_CLICKED, this);
    play_label_ = lv_label_create(play);
    lv_label_set_text(play_label_, LV_SYMBOL_PLAY);
    lv_obj_center(play_label_);
    lv_obj_t * next = lv_btn_create(root_);
    lv_obj_set_size(next, 56, 48);
    lv_obj_set_pos(next, 278, 394);
    lv_obj_add_event_cb(next, nextEvent, LV_EVENT_CLICKED, this);
    lv_obj_t * next_label = lv_label_create(next);
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);
    lv_obj_center(next_label);
    volume_ = lv_slider_create(root_);
    lv_obj_set_size(volume_, 250, 18);
    lv_obj_align(volume_, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_slider_set_range(volume_, 0, 100);
    lv_slider_set_value(volume_, local_volume_, LV_ANIM_OFF);
    lv_obj_set_ext_click_area(volume_, 15);
    lv_obj_add_event_cb(volume_, volumeEvent, LV_EVENT_VALUE_CHANGED, this);
    UiComponents::styleSlider(volume_, tokens);
    return true;
}

void MusicApp::destroy() {
    if(scan_directory_ && storage_) storage_->closeManaged(scan_directory_);
    if(root_) lv_obj_del(root_);
    root_ = status_ = list_ = track_label_ = progress_ = nullptr;
    time_label_ = play_label_ = volume_ = nullptr;
}

void MusicApp::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    requestRefresh();
}

void MusicApp::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void MusicApp::bindStorage(StorageService & storage, bool sd_available) {
    storage_ = &storage;
    sd_available_ = sd_available;
}

void MusicApp::requestRefresh() { refresh_pending_ = true; }

void MusicApp::tick(uint32_t now_ms, bool power_saver) {
    if(refresh_pending_) {
        refresh_pending_ = false;
        scanLibrary();
    }
    if(scan_active_) scanStep();
    const uint32_t interval = power_saver ? 1000UL : 250UL;
    if(last_ui_update_ == 0 || now_ms - last_ui_update_ >= interval) {
        last_ui_update_ = now_ms;
        refreshPlayer(now_ms);
    }
}

void MusicApp::onSdRemoved() {
    sd_available_ = false;
    if(scan_directory_ && storage_) storage_->closeManaged(scan_directory_);
    scan_active_ = false;
    if(audio_) audio_->discardPlayback();
    updateStatus("SD card unavailable | Refresh after insert");
    refreshPlayer(0, true);
}

bool MusicApp::playing() const {
    return audio_ && audio_->activeUse() == AudioUse::Music &&
           !audio_->playbackPaused();
}

bool MusicApp::scanLibrary() {
    if(scan_directory_ && storage_) storage_->closeManaged(scan_directory_);
    scan_active_ = false;
    track_count_ = 0;
    index_limit_reached_ = false;
    if(!storage_ || !sd_available_) {
        updateStatus("SD unavailable");
        renderList();
        return false;
    }
    scan_directory_ = storage_->openManaged("/FireflyOS/Music", FILE_READ);
    bool is_directory = false;
    if(!scan_directory_ ||
       !storage_->managedFileIsDirectory(scan_directory_, is_directory) ||
       !is_directory) {
        if(scan_directory_) storage_->closeManaged(scan_directory_);
        updateStatus("Music folder unavailable");
        renderList();
        return false;
    }
    scan_active_ = true;
    updateStatus("Scanning local WAV files...");
    renderList();
    return true;
}

void MusicApp::scanStep() {
    if(!scan_active_ || !scan_directory_) return;
    for(uint8_t processed = 0; processed < kScanEntriesPerTick; ++processed) {
        fs::File entry = storage_->openNextManaged(scan_directory_);
        if(!entry) {
            finishScan();
            return;
        }
        bool is_directory = false;
        char entry_name[64]{};
        char entry_path[96]{};
        const bool metadata_ok =
            storage_->managedFileIsDirectory(entry, is_directory) &&
            storage_->managedFileName(entry, entry_name, sizeof(entry_name)) &&
            storage_->managedFilePath(entry, entry_path, sizeof(entry_path));
        if(metadata_ok && !is_directory && endsWithWav(entry_name)) {
            if(track_count_ >= kMaxTracks) {
                index_limit_reached_ = true;
                storage_->closeManaged(entry);
                finishScan();
                return;
            }
            MusicTrack & track = tracks_[track_count_];
            strlcpy(track.path, entry_path, sizeof(track.path));
            strlcpy(track.title, baseName(entry_name), sizeof(track.title));
            uint8_t header[AudioService::kWavHeaderBytes]{};
            WavInfo info{};
            if(storage_->readManaged(entry, header, sizeof(header)) == sizeof(header) &&
               AudioService::parseWavHeader(header, sizeof(header), info)) {
                const uint32_t bytes_per_second = info.sample_rate *
                    info.channels * sizeof(int16_t);
                track.duration_ms = bytes_per_second == 0 ? 0 :
                    static_cast<uint32_t>((static_cast<uint64_t>(info.data_bytes) *
                                          1000ULL) / bytes_per_second);
                ++track_count_;
            }
        }
        storage_->closeManaged(entry);
    }
}

void MusicApp::finishScan() {
    if(scan_directory_ && storage_) storage_->closeManaged(scan_directory_);
    scan_active_ = false;
    control_selector_.noteScanCompleted(track_count_);
    char status[64];
    snprintf(status, sizeof(status), index_limit_reached_
             ? "%u tracks | 128 track limit reached" : "%u local WAV tracks",
             static_cast<unsigned>(track_count_));
    if(track_count_ == 0) strlcpy(status, "No local WAV files", sizeof(status));
    updateStatus(status);
    if(current_index_ >= track_count_) current_index_ = 0;
    renderList();
}

bool MusicApp::playIndex(uint16_t index) {
    if(!audio_ || !storage_ || !sd_available_ || index >= track_count_) return false;
    if(audio_->activeUse() == AudioUse::Music || audio_->playbackPaused()) {
        audio_->stop();
    }
    if(!audio_->startWav(*storage_, tracks_[index].path, AudioUse::Music)) {
        updateStatus("Unable to play this WAV");
        return false;
    }
    current_index_ = index;
    refreshPlayer(0, true);
    return true;
}

bool MusicApp::dispatchPhoneRemote(RemoteMediaCommand command,
                                   uint8_t volume) {
    const bool sent = phone_media_callback_ &&
        phone_media_callback_(command, volume);
    updateStatus(sent
        ? "Phone command sent"
        : "Phone remote unavailable | Connect paired phone");
    return sent;
}

bool MusicApp::applyLocalVolume(uint8_t volume) {
    if(volume > 100 || !local_volume_callback_ ||
       !local_volume_callback_(volume)) {
        return false;
    }
    local_volume_ = volume;
    return true;
}

void MusicApp::updateStatus(const char * detail) {
    if(!status_) return;
    char text[128]{};
    const bool local =
        control_selector_.target() == MusicControlTarget::LocalLibrary;
    snprintf(text, sizeof(text), "Target: %s | Hold Refresh: %s\n%s",
             local ? "Local library" : "Phone remote",
             local ? "Phone remote" : "Local library",
             detail ? detail : "");
    lv_label_set_text(status_, text);
}

void MusicApp::renderList() {
    if(!list_) return;
    lv_obj_clean(list_);
    if(track_count_ == 0) {
        lv_obj_t * empty = lv_label_create(list_);
        lv_label_set_text(empty, sd_available_ ? "No valid WAV files" : "SD card unavailable");
        return;
    }
    if(list_start_ >= track_count_) list_start_ = 0;
    const uint16_t end = (list_start_ + 5U < track_count_)
        ? list_start_ + 5U : track_count_;
    for(uint16_t index = list_start_; index < end; ++index) {
        RowContext & context = row_contexts_[index - list_start_];
        context.app = this;
        context.index = index;
        lv_obj_t * row = lv_btn_create(list_);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 54);
        lv_obj_add_event_cb(row, trackEvent, LV_EVENT_CLICKED, &context);
        lv_obj_t * label = lv_label_create(row);
        lv_label_set_text(label, tracks_[index].title);
        lv_obj_set_width(label, 280);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 2, 0);
    }
}

void MusicApp::refreshPlayer(uint32_t now_ms, bool force) {
    LV_UNUSED(now_ms);
    LV_UNUSED(force);
    if(control_selector_.target() == MusicControlTarget::PhoneRemote) {
        lv_label_set_text(track_label_, "Target: Phone remote");
        lv_bar_set_value(progress_, 0, LV_ANIM_OFF);
        lv_label_set_text(time_label_, "Controls phone | Hold Refresh: Local");
        lv_label_set_text(play_label_, LV_SYMBOL_PLAY);
        return;
    }
    if(!audio_ || track_count_ == 0) {
        lv_label_set_text(track_label_, "Target: Local | Nothing playing");
        lv_bar_set_value(progress_, 0, LV_ANIM_OFF);
        lv_label_set_text(
            time_label_, "Hold Refresh: Phone remote"
        );
        lv_label_set_text(play_label_, LV_SYMBOL_PLAY);
        return;
    }
    char track_text[96]{};
    snprintf(track_text, sizeof(track_text), "Target: Local | %s",
             tracks_[current_index_].title);
    lv_label_set_text(track_label_, track_text);
    const uint32_t position = audio_->playbackPositionMs();
    const uint32_t duration = audio_->playbackDurationMs();
    lv_bar_set_value(progress_, duration == 0 ? 0 :
        static_cast<int32_t>((static_cast<uint64_t>(position) * 1000ULL) / duration),
        LV_ANIM_OFF);
    char current[12], total[12], combined[64];
    formatTime(position, current, sizeof(current));
    formatTime(duration, total, sizeof(total));
    snprintf(combined, sizeof(combined), "%s / %s | Hold Refresh: Phone",
             current, total);
    lv_label_set_text(time_label_, combined);
    lv_label_set_text(play_label_, playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

void MusicApp::trackEvent(lv_event_t * event) {
    RowContext * context = static_cast<RowContext *>(lv_event_get_user_data(event));
    if(context && context->app) {
        context->app->control_selector_.noteLocalTrackSelected();
        context->app->playIndex(context->index);
    }
}

void MusicApp::playPauseEvent(lv_event_t * event) {
    MusicApp * app = static_cast<MusicApp *>(lv_event_get_user_data(event));
    if(!app || !app->audio_) return;
    if(app->controlTarget() == MusicControlTarget::PhoneRemote) {
        app->dispatchPhoneRemote(RemoteMediaCommand::PlayPause);
        return;
    }
    uint16_t target = 0;
    if(!MusicQueueNavigator::play(
           app->current_index_, app->track_count_, target)) {
        app->updateStatus("No local WAV files");
        app->refreshPlayer(0, true);
        return;
    }
    if(app->playing()) app->audio_->pausePlayback();
    else if(app->audio_->playbackPaused()) app->audio_->resumePlayback();
    else app->playIndex(target);
    app->refreshPlayer(0, true);
}

void MusicApp::previousEvent(lv_event_t * event) {
    MusicApp * app = static_cast<MusicApp *>(lv_event_get_user_data(event));
    if(!app) return;
    if(app->controlTarget() == MusicControlTarget::PhoneRemote) {
        app->dispatchPhoneRemote(RemoteMediaCommand::Previous);
        return;
    }
    uint16_t target = 0;
    if(!MusicQueueNavigator::previous(
           app->current_index_, app->track_count_, target)) {
        app->updateStatus("No local WAV files");
        app->refreshPlayer(0, true);
        return;
    }
    app->playIndex(target);
}

void MusicApp::nextEvent(lv_event_t * event) {
    MusicApp * app = static_cast<MusicApp *>(lv_event_get_user_data(event));
    if(!app) return;
    if(app->controlTarget() == MusicControlTarget::PhoneRemote) {
        app->dispatchPhoneRemote(RemoteMediaCommand::Next);
        return;
    }
    uint16_t target = 0;
    if(!MusicQueueNavigator::next(
           app->current_index_, app->track_count_, target)) {
        app->updateStatus("No local WAV files");
        app->refreshPlayer(0, true);
        return;
    }
    app->playIndex(target);
}

void MusicApp::refreshEvent(lv_event_t * event) {
    MusicApp * app = static_cast<MusicApp *>(lv_event_get_user_data(event));
    if(app) app->requestRefresh();
}

void MusicApp::targetToggleEvent(lv_event_t * event) {
    MusicApp * app = static_cast<MusicApp *>(lv_event_get_user_data(event));
    if(!app) return;
    app->control_selector_.toggle();
    app->updateStatus(
        app->controlTarget() == MusicControlTarget::PhoneRemote
            ? "Phone controls selected"
            : "Local controls selected"
    );
    app->refreshPlayer(0, true);
}

void MusicApp::volumeEvent(lv_event_t * event) {
    MusicApp * app = static_cast<MusicApp *>(lv_event_get_user_data(event));
    if(!app) return;
    const uint8_t volume = static_cast<uint8_t>(
        lv_slider_get_value(lv_event_get_target(event))
    );
    if(app->controlTarget() == MusicControlTarget::PhoneRemote) {
        app->dispatchPhoneRemote(RemoteMediaCommand::Volume, volume);
    } else if(!app->applyLocalVolume(volume)) {
        lv_slider_set_value(
            lv_event_get_target(event), app->localVolume(), LV_ANIM_OFF
        );
        app->updateStatus("Unable to save local volume");
    } else {
        app->updateStatus("Local volume saved");
    }
}

}  // namespace firefly
