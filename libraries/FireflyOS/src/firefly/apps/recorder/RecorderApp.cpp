#include "RecorderApp.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../ui/UiTheme.h"

namespace firefly {

bool RecorderApp::create(lv_obj_t * parent, UiComponents & components,
                         AudioService & audio) {
    LV_UNUSED(components);
    if(!parent) return false;
    audio_ = &audio;
    const UiTokens tokens = UiTheme::fireflyDefault();
    root_ = UiComponents::createPage(parent, tokens);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * title = UiComponents::createTitle(root_, tokens, "Recorder");
    lv_obj_set_pos(title, 28, 54);
    status_ = lv_label_create(root_);
    lv_obj_set_width(status_, 350);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_, lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(status_, "Ready | 16 kHz mono");
    lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_t * ring = lv_obj_create(root_);
    lv_obj_set_size(ring, 190, 190);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 132);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, lv_color_hex(0x180B0D), 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0xFF626A), 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    timer_ = lv_label_create(ring);
    lv_label_set_text(timer_, "00:00:00");
    lv_obj_set_style_text_font(timer_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(timer_, lv_color_hex(tokens.text_primary), 0);
    lv_obj_center(timer_);
    record_button_ = lv_btn_create(root_);
    lv_obj_set_size(record_button_, 300, 64);
    lv_obj_align(record_button_, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_obj_set_style_bg_color(record_button_, lv_color_hex(0x9D2931), 0);
    lv_obj_set_style_radius(record_button_, 22, 0);
    lv_obj_add_event_cb(record_button_, recordEvent, LV_EVENT_CLICKED, this);
    record_button_label_ = lv_label_create(record_button_);
    lv_label_set_text(record_button_label_, "Start recording");
    lv_obj_center(record_button_label_);
    lv_obj_t * warning = lv_label_create(root_);
    lv_label_set_text(warning, "Recording is always visibly marked");
    lv_obj_set_style_text_color(warning, lv_color_hex(0xFF9AA0), 0);
    lv_obj_align(warning, LV_ALIGN_BOTTOM_MID, 0, -22);
    return true;
}

void RecorderApp::destroy() {
    if(recording_) finishRecording(true);
    if(root_) lv_obj_del(root_);
    root_ = status_ = timer_ = record_button_ = record_button_label_ = nullptr;
}

void RecorderApp::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void RecorderApp::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void RecorderApp::bindStorage(StorageService & storage, bool sd_available,
                              uint64_t free_bytes) {
    storage_ = &storage;
    sd_available_ = sd_available;
    free_bytes_ = free_bytes;
}

void RecorderApp::tick(uint32_t now_ms, int64_t epoch_seconds) {
    current_epoch_ = epoch_seconds;
    if(recording_) {
        audio_->serviceRecording();
        if(audio_->activeUse() != AudioUse::Recorder) {
            recording_ = false;
            setStatus("Recording interrupted", true);
        }
    }
    if(last_ui_update_ == 0 || now_ms - last_ui_update_ >= 1000UL) {
        last_ui_update_ = now_ms;
        refreshUi(now_ms);
    }
}

void RecorderApp::onSdRemoved() {
    sd_available_ = false;
    if(recording_) {
        audio_->stop();
        recording_ = false;
        setStatus("Save failed | SD card removed", true);
    } else {
        setStatus("SD card unavailable", true);
    }
    refreshUi(0);
}

void RecorderApp::stopForSafety() {
    if(recording_) finishRecording(sd_available_);
}

bool RecorderApp::makeRecordingName(int64_t epoch_seconds,
                                    uint32_t fallback_sequence,
                                    char * out,
                                    size_t out_size) {
    if(!out || out_size == 0) return false;
    const time_t raw = static_cast<time_t>(epoch_seconds);
    struct tm local{};
    int written = 0;
    if(epoch_seconds > 0 && localtime_r(&raw, &local) &&
       local.tm_year + 1900 >= 2024) {
        written = snprintf(out, out_size,
            "REC_%04d%02d%02d_%02d%02d%02d.wav",
            local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
            local.tm_hour, local.tm_min, local.tm_sec);
    } else {
        written = snprintf(out, out_size, "REC_%06lu.wav",
                           static_cast<unsigned long>(fallback_sequence));
    }
    return written > 0 && static_cast<size_t>(written) < out_size;
}

bool RecorderApp::startRecording() {
    if(!audio_ || !storage_ || !sd_available_) {
        setStatus("SD card unavailable", true);
        return false;
    }
    if(free_bytes_ < kMinimumFreeBytes) {
        setStatus("Need at least 2 MB free", true);
        return false;
    }
    char name[48];
    bool unique_name = false;
    for(uint8_t attempt = 0; attempt < 32; ++attempt) {
        if(!makeRecordingName(current_epoch_, fallback_sequence_++,
                              name, sizeof(name))) return false;
        snprintf(final_path_, sizeof(final_path_),
                 "/FireflyOS/Recordings/%s", name);
        if(!storage_->managedExists(final_path_)) {
            unique_name = true;
            break;
        }
        current_epoch_ = 0;
    }
    if(!unique_name) {
        setStatus("Unable to allocate a recording name", true);
        return false;
    }
    snprintf(temporary_path_, sizeof(temporary_path_), "%s.tmp", final_path_);
    if(storage_->managedExists(temporary_path_)) storage_->removeManaged(temporary_path_);
    if(!audio_->startRecording(*storage_, temporary_path_, 16000)) {
        setStatus("Microphone unavailable", true);
        return false;
    }
    started_at_ = millis();
    recording_ = true;
    setStatus("RECORDING | HIGH POWER");
    refreshUi(started_at_);
    return true;
}

bool RecorderApp::finishRecording(bool save) {
    if(!recording_) return false;
    audio_->stop();
    recording_ = false;
    bool saved = false;
    if(save && storage_ && storage_->managedExists(temporary_path_)) {
        saved = storage_->renameManaged(temporary_path_, final_path_);
    }
    if(saved) setStatus("Saved to Recordings");
    else setStatus("Recording not saved | Temporary file hidden", true);
    refreshUi(millis());
    return saved;
}

void RecorderApp::refreshUi(uint32_t now_ms) {
    if(!timer_) return;
    const uint32_t elapsed = recording_ ? (now_ms - started_at_) / 1000UL : 0;
    char text[16];
    snprintf(text, sizeof(text), "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(elapsed / 3600UL),
             static_cast<unsigned long>((elapsed / 60UL) % 60UL),
             static_cast<unsigned long>(elapsed % 60UL));
    lv_label_set_text(timer_, text);
    lv_label_set_text(record_button_label_,
                      recording_ ? "Stop & save" : "Start recording");
}

void RecorderApp::setStatus(const char * text, bool error) {
    if(!status_) return;
    lv_label_set_text(status_, text ? text : "");
    lv_obj_set_style_text_color(status_,
        lv_color_hex(error ? 0xFF9AA0 : 0x8BA6AA), 0);
}

void RecorderApp::recordEvent(lv_event_t * event) {
    RecorderApp * app = static_cast<RecorderApp *>(lv_event_get_user_data(event));
    if(!app) return;
    if(app->recording_) app->finishRecording(true);
    else app->startRecording();
}

}  // namespace firefly
