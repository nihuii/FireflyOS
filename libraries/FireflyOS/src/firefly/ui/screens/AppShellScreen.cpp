#include "AppShellScreen.h"

namespace firefly {

bool AppShellScreen::create(lv_obj_t * parent, const UiTokens & tokens) {
    if(!parent) return false;
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(tokens.bg_base), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    title_ = lv_label_create(root_);
    lv_obj_set_style_text_color(title_, lv_color_hex(tokens.text_primary), 0);
    lv_obj_set_style_text_font(title_, &lv_font_montserrat_24, 0);
    lv_label_set_text(title_, "App");
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 72);

    status_ = lv_label_create(root_);
    lv_obj_set_width(status_, 330);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_, lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(status_, "Reserved for a later plan\nPress BOOT to return");
    lv_obj_center(status_);
    hide();
    return true;
}

void AppShellScreen::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void AppShellScreen::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void AppShellScreen::refresh(const SystemState &) {}

void AppShellScreen::setTitle(const char * title) {
    if(title_) lv_label_set_text(title_, title ? title : "App");
}

}  // namespace firefly
