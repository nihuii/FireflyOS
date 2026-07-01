#include "SettingsApp.h"

#include <stdio.h>

namespace firefly {

bool SettingsCommandQueue::post(const SettingsCommand & command) {
    if(command.type == SettingsCommandType::None || count_ >= kCapacity) {
        return false;
    }

    commands_[tail_] = command;
    tail_ = static_cast<uint8_t>((tail_ + 1U) % kCapacity);
    ++count_;
    return true;
}

bool SettingsCommandQueue::take(SettingsCommand & command) {
    if(count_ == 0) {
        command = {};
        return false;
    }

    command = commands_[head_];
    head_ = static_cast<uint8_t>((head_ + 1U) % kCapacity);
    --count_;
    return true;
}

bool SettingsApp::create(lv_obj_t * parent, UiComponents & components, TimeService & time) {
    LV_UNUSED(components);
    if(!parent) {
        return false;
    }

    time_ = &time;
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, 410, 502);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x020607), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(root_);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEFFFFB), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 58);

    status_label_ = lv_label_create(root_);
    lv_label_set_text(status_label_, "Service-backed settings shell");
    lv_obj_set_width(status_label_, 330);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0x8BA6AA), 0);
    lv_obj_center(status_label_);
    hide();
    return true;
}

bool SettingsApp::bindLegacyPanel(lv_obj_t * panel) {
    if(!panel) {
        return false;
    }
    root_ = panel;
    return true;
}

void SettingsApp::show() {
    if(root_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsApp::hide() {
    if(root_) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsApp::refresh(const SystemState & state) {
    if(!status_label_) {
        return;
    }

    char text[64];
    if(state.time.valid) {
        snprintf(text, sizeof(text), "RTC valid · battery %d%%",
                 static_cast<int>(state.battery.percent));
    } else if(time_ && time_->now().valid) {
        snprintf(text, sizeof(text), "RTC cached · battery %d%%",
                 static_cast<int>(state.battery.percent));
    } else {
        snprintf(text, sizeof(text), "RTC invalid · set time manually");
    }
    lv_label_set_text(status_label_, text);
}

bool SettingsApp::postCommand(const SettingsCommand & command) {
    return queue_.post(command);
}

bool SettingsApp::takeCommand(SettingsCommand & command) {
    return queue_.take(command);
}

}  // namespace firefly
