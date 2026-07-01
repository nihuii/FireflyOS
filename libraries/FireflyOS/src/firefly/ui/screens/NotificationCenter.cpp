#include "NotificationCenter.h"

namespace firefly {

bool NotificationCenter::create(lv_obj_t * parent, const UiTokens & tokens) {
    if(!parent) return false;
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(tokens.bg_base), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 24, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(root_);
    lv_obj_set_style_text_color(title, lv_color_hex(tokens.text_primary), 0);
    lv_label_set_text(title, "Notifications");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 20);

    clear_button_ = lv_btn_create(root_);
    lv_obj_set_size(clear_button_, 88, 48);
    lv_obj_align(clear_button_, LV_ALIGN_TOP_MID, 18, 4);
    lv_obj_set_style_bg_color(clear_button_, lv_color_hex(tokens.bg_surface), 0);
    lv_obj_set_style_shadow_width(clear_button_, 0, 0);
    lv_obj_t * clear_label = lv_label_create(clear_button_);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);

    control_button_ = lv_btn_create(root_);
    lv_obj_set_size(control_button_, 104, 48);
    lv_obj_align(control_button_, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(control_button_, lv_color_hex(tokens.firefly_secondary), 0);
    lv_obj_set_style_shadow_width(control_button_, 0, 0);
    lv_obj_t * control_label = lv_label_create(control_button_);
    lv_label_set_text(control_label, "Controls");
    lv_obj_center(control_label);

    empty_ = lv_label_create(root_);
    lv_obj_set_style_text_color(empty_, lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(empty_, LV_SYMBOL_BELL "  No notifications");
    lv_obj_center(empty_);

    for(uint8_t i = 0; i < kVisibleLimit; ++i) {
        cards_[i] = lv_obj_create(root_);
        lv_obj_set_size(cards_[i], 346, 106);
        lv_obj_set_pos(cards_[i], 8, 80 + i * 118);
        lv_obj_set_style_radius(cards_[i], tokens.radius_card, 0);
        lv_obj_set_style_bg_color(cards_[i], lv_color_hex(tokens.bg_surface), 0);
        lv_obj_set_style_bg_opa(cards_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cards_[i], 1, 0);
        lv_obj_set_style_border_color(cards_[i], lv_color_hex(tokens.firefly_secondary), 0);
        lv_obj_set_style_pad_all(cards_[i], 12, 0);
        lv_obj_clear_flag(cards_[i], LV_OBJ_FLAG_SCROLLABLE);
        app_labels_[i] = lv_label_create(cards_[i]);
        title_labels_[i] = lv_label_create(cards_[i]);
        body_labels_[i] = lv_label_create(cards_[i]);
        lv_obj_set_width(body_labels_[i], 316);
        lv_label_set_long_mode(body_labels_[i], LV_LABEL_LONG_DOT);
        lv_obj_align(app_labels_[i], LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_align(title_labels_[i], LV_ALIGN_TOP_LEFT, 0, 26);
        lv_obj_align(body_labels_[i], LV_ALIGN_TOP_LEFT, 0, 54);
        lv_obj_set_style_text_color(app_labels_[i], lv_color_hex(tokens.firefly_primary), 0);
        lv_obj_set_style_text_color(title_labels_[i], lv_color_hex(tokens.text_primary), 0);
        lv_obj_set_style_text_color(body_labels_[i], lv_color_hex(tokens.text_secondary), 0);
    }
    hide();
    render();
    return true;
}

void NotificationCenter::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void NotificationCenter::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void NotificationCenter::refresh(const SystemState &) {}

void NotificationCenter::setNotifications(const NotificationSummary * notifications,
                                          uint8_t count) {
    if(!notifications) count = 0;
    count_ = count > kVisibleLimit ? kVisibleLimit : count;
    for(uint8_t i = 0; i < count_; ++i) notifications_[i] = notifications[i];
    render();
}

void NotificationCenter::setControlCallback(lv_event_cb_t callback) {
    if(control_button_ && callback) {
        lv_obj_add_event_cb(control_button_, callback, LV_EVENT_CLICKED, nullptr);
    }
}

void NotificationCenter::setClearCallback(lv_event_cb_t callback) {
    if(clear_button_ && callback) {
        lv_obj_add_event_cb(clear_button_, callback, LV_EVENT_CLICKED, nullptr);
    }
}

void NotificationCenter::clear() {
    count_ = 0;
    render();
}

void NotificationCenter::render() {
    if(empty_) {
        if(count_ == 0) lv_obj_clear_flag(empty_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }
    for(uint8_t i = 0; i < kVisibleLimit; ++i) {
        if(!cards_[i]) continue;
        if(i >= count_) {
            lv_obj_add_flag(cards_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(cards_[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(app_labels_[i], notifications_[i].app_name);
        lv_label_set_text(title_labels_[i], notifications_[i].title);
        lv_label_set_text(body_labels_[i], notifications_[i].body);
    }
}

}  // namespace firefly
