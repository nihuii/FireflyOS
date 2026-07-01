#include "ControlCenter.h"

#include <stdio.h>

namespace firefly {

bool ControlCenter::create(lv_obj_t * parent, const UiTokens & tokens) {
    if(!parent) return false;
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(tokens.bg_base), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    return true;
}

void ControlCenter::bind(lv_obj_t * root, lv_obj_t * detail,
                         lv_obj_t * volume_slider, lv_obj_t * volume_value,
                         lv_obj_t * brightness_slider, lv_obj_t * brightness_value) {
    root_ = root;
    detail_ = detail;
    volume_slider_ = volume_slider;
    volume_value_ = volume_value;
    brightness_slider_ = brightness_slider;
    brightness_value_ = brightness_value;
}

void ControlCenter::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ControlCenter::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ControlCenter::refresh(const SystemState & state) {
    refresh(state, volume_, brightness_);
}

void ControlCenter::refresh(const SystemState & state, uint8_t volume,
                            uint8_t brightness) {
    refresh(state, volume, brightness, static_cast<uint32_t>(-1));
}

void ControlCenter::refresh(const SystemState & state, uint8_t volume,
                            uint8_t brightness, uint32_t revision) {
    if(revision == rendered_revision_ && volume == volume_ &&
       brightness == brightness_) return;
    rendered_revision_ = revision;
    volume_ = volume;
    brightness_ = brightness;
    char text[96];
    snprintf(text, sizeof(text), "Battery %d%%%s\nVolume %u%%  Brightness %u%%",
             state.battery.valid ? state.battery.percent : 0,
             state.battery.charging ? "  Charging" : "",
             static_cast<unsigned>(volume_),
             static_cast<unsigned>((brightness_ * 100U) / 255U));
    if(detail_) lv_label_set_text(detail_, text);
    if(volume_slider_) lv_slider_set_value(volume_slider_, volume_, LV_ANIM_OFF);
    if(brightness_slider_) lv_slider_set_value(brightness_slider_, brightness_, LV_ANIM_OFF);
    char value[8];
    snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(volume_));
    if(volume_value_) lv_label_set_text(volume_value_, value);
    snprintf(value, sizeof(value), "%u%%",
             static_cast<unsigned>((brightness_ * 100U) / 255U));
    if(brightness_value_) lv_label_set_text(brightness_value_, value);
}

}  // namespace firefly
