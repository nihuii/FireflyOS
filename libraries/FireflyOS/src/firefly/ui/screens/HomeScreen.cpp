#include "HomeScreen.h"

#include <string.h>

namespace firefly {

bool HomeScreen::create(lv_obj_t * parent, const UiTokens & tokens) {
    if(!parent) return false;
    root_ = parent;
    tokens_ = tokens;
    lv_obj_clean(root_);
    pager_ = lv_tileview_create(root_);
    lv_obj_set_size(pager_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(pager_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(pager_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(pager_, pagerEventCallback, LV_EVENT_VALUE_CHANGED, this);
    return true;
}

void HomeScreen::pagerEventCallback(lv_event_t * event) {
    HomeScreen * screen = static_cast<HomeScreen *>(lv_event_get_user_data(event));
    if(!screen || !screen->pager_) return;
    lv_obj_t * active = lv_tileview_get_tile_act(screen->pager_);
    for(uint8_t page = 0; page < screen->page_count_; ++page) {
        if(screen->pages_[page] == active) {
            screen->updateDots(page);
            return;
        }
    }
}

void HomeScreen::updateDots(uint8_t active_page) {
    if(!dots_) return;
    static const char * two_pages[] = {"●  ○", "○  ●"};
    static const char * three_pages[] = {"●  ○  ○", "○  ●  ○", "○  ○  ●"};
    if(page_count_ <= 1) lv_label_set_text(dots_, "●");
    else if(page_count_ == 2) lv_label_set_text(dots_, two_pages[active_page < 2 ? active_page : 0]);
    else lv_label_set_text(dots_, three_pages[active_page < 3 ? active_page : 0]);
}

const char * HomeScreen::symbolFor(const char * id) {
    if(!id) return LV_SYMBOL_DUMMY;
    if(strcmp(id, "settings") == 0) return LV_SYMBOL_SETTINGS;
    if(strcmp(id, "clock") == 0) return LV_SYMBOL_BELL;
    if(strcmp(id, "calendar") == 0) return LV_SYMBOL_LIST;
    if(strcmp(id, "tools") == 0) return LV_SYMBOL_SETTINGS;
    if(strcmp(id, "activity") == 0) return LV_SYMBOL_EYE_OPEN;
    if(strcmp(id, "weather") == 0) return LV_SYMBOL_GPS;
    if(strcmp(id, "music") == 0) return LV_SYMBOL_AUDIO;
    if(strcmp(id, "recorder") == 0) return LV_SYMBOL_EDIT;
    if(strcmp(id, "files") == 0) return LV_SYMBOL_DIRECTORY;
    if(strcmp(id, "diagnostics") == 0) return LV_SYMBOL_WARNING;
    return LV_SYMBOL_EYE_OPEN;
}

bool HomeScreen::populate(const AppRegistry & registry, lv_event_cb_t callback) {
    if(!pager_) return false;
    page_count_ = (registry.count() + 5U) / 6U;
    if(page_count_ > kMaxPages) page_count_ = kMaxPages;
    for(uint8_t page = 0; page < page_count_; ++page) {
        lv_obj_t * tile = lv_tileview_add_tile(pager_, page, 0,
            page_count_ > 1 ? LV_DIR_HOR : LV_DIR_NONE);
        pages_[page] = tile;
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);
        for(uint8_t slot = 0; slot < 6; ++slot) {
            const uint8_t index = page * 6U + slot;
            if(index >= registry.count()) break;
            const AppDescriptor & app = registry.at(index);
            const uint8_t col = slot % 3U;
            const uint8_t row = slot / 3U;
            lv_obj_t * button = lv_btn_create(tile);
            lv_obj_set_size(button, 72, 72);
            lv_obj_set_pos(button, 35 + col * 120, 88 + row * 146);
            lv_obj_set_style_radius(button, 24, 0);
            lv_obj_set_style_bg_color(button, lv_color_hex(tokens_.firefly_secondary), 0);
            lv_obj_set_style_bg_opa(button, LV_OPA_80, 0);
            lv_obj_set_style_shadow_width(button, 0, 0);
            if(callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED,
                                             const_cast<AppDescriptor *>(&app));
            lv_obj_t * symbol = lv_label_create(button);
            lv_obj_set_style_text_color(symbol, lv_color_hex(tokens_.text_primary), 0);
            lv_label_set_text(symbol, symbolFor(app.id));
            lv_obj_center(symbol);
            lv_obj_t * label = lv_label_create(tile);
            lv_obj_set_width(label, 100);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(tokens_.text_primary), 0);
            lv_label_set_text(label, app.name);
            lv_obj_set_pos(label, 21 + col * 120, 166 + row * 146);
        }
    }
    dots_ = lv_label_create(root_);
    lv_obj_set_style_text_color(dots_, lv_color_hex(tokens_.text_secondary), 0);
    lv_obj_align(dots_, LV_ALIGN_BOTTOM_MID, 0, -20);
    updateDots(0);
    return true;
}

void HomeScreen::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void HomeScreen::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void HomeScreen::refresh(const SystemState &) {}

}  // namespace firefly
