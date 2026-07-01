#include "LockScreen.h"

#include <stdio.h>

namespace firefly {

bool LockScreen::create(lv_obj_t * parent, const UiTokens & tokens) {
    if(!parent) return false;
    root_ = parent;
    status_ = lv_label_create(root_);
    lv_obj_set_style_text_color(status_, lv_color_hex(tokens.text_secondary), 0);
    lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, 24);
    lv_label_set_text(status_, "Standalone  --%");
    alarm_ = lv_label_create(root_);
    lv_obj_set_style_text_color(alarm_, lv_color_hex(tokens.text_secondary), 0);
    lv_obj_align(alarm_, LV_ALIGN_BOTTOM_MID, 0, -54);
    lv_label_set_text(alarm_, "NEXT  --:--");
    return true;
}

void LockScreen::bind(lv_obj_t * root, lv_obj_t * date, lv_obj_t * time,
                      lv_obj_t * week) {
    root_ = root;
    date_ = date;
    time_ = time;
    week_ = week;
}

void LockScreen::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void LockScreen::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void LockScreen::refresh(const SystemState & state) {
    if(!status_) return;
    char text[48];
    snprintf(text, sizeof(text), "%s  %d%%",
             state.phone_connected ? "Phone" : "Standalone",
             state.battery.valid ? state.battery.percent : 0);
    lv_label_set_text(status_, text);
}

void LockScreen::setNextAlarm(const char * text) {
    if(alarm_) lv_label_set_text(alarm_, text ? text : "NEXT  --:--");
}

}  // namespace firefly
