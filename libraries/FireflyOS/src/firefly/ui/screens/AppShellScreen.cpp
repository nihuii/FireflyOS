#include "AppShellScreen.h"

#include <string.h>

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

    weather_city_ = lv_label_create(root_);
    lv_obj_set_width(weather_city_, 330);
    lv_obj_set_style_text_align(weather_city_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(weather_city_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(
        weather_city_, lv_color_hex(tokens.text_primary), 0
    );
    lv_obj_align(weather_city_, LV_ALIGN_TOP_MID, 0, 132);

    weather_temperature_ = lv_label_create(root_);
    lv_obj_set_width(weather_temperature_, 330);
    lv_obj_set_style_text_align(
        weather_temperature_, LV_TEXT_ALIGN_CENTER, 0
    );
    lv_obj_set_style_text_font(
        weather_temperature_, &lv_font_montserrat_24, 0
    );
    lv_obj_set_style_text_color(
        weather_temperature_, lv_color_hex(tokens.firefly_primary), 0
    );
    lv_obj_align(weather_temperature_, LV_ALIGN_TOP_MID, 0, 184);

    weather_range_ = lv_label_create(root_);
    lv_obj_set_width(weather_range_, 350);
    lv_obj_set_style_text_align(weather_range_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(
        weather_range_, lv_color_hex(tokens.text_secondary), 0
    );
    lv_obj_align(weather_range_, LV_ALIGN_TOP_MID, 0, 236);

    weather_code_ = lv_label_create(root_);
    lv_obj_set_width(weather_code_, 330);
    lv_obj_set_style_text_align(weather_code_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(
        weather_code_, lv_color_hex(tokens.text_secondary), 0
    );
    lv_obj_align(weather_code_, LV_ALIGN_TOP_MID, 0, 276);

    weather_status_ = lv_label_create(root_);
    lv_obj_set_width(weather_status_, 350);
    lv_obj_set_style_text_align(weather_status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(
        weather_status_, lv_color_hex(tokens.text_secondary), 0
    );
    lv_obj_align(weather_status_, LV_ALIGN_TOP_MID, 0, 326);
    setWeatherVisible(false);
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
    if(!title || strcmp(title, "Weather") != 0) setWeatherVisible(false);
}

void AppShellScreen::setStatus(const char * status) {
    if(status_) lv_label_set_text(status_, status ? status : "");
}

void AppShellScreen::showWeather(const CompanionWeatherView & weather) {
    if(weather_city_) lv_label_set_text(weather_city_, weather.city);
    if(weather_temperature_) {
        lv_label_set_text(weather_temperature_, weather.current);
    }
    if(weather_range_) lv_label_set_text(weather_range_, weather.range);
    if(weather_code_) lv_label_set_text(weather_code_, weather.code);
    if(weather_status_) lv_label_set_text(weather_status_, weather.status);
    setWeatherVisible(true);
}

void AppShellScreen::setWeatherVisible(bool visible) {
    lv_obj_t * weather_labels[] = {
        weather_city_,
        weather_temperature_,
        weather_range_,
        weather_code_,
        weather_status_,
    };
    for(lv_obj_t * label : weather_labels) {
        if(!label) continue;
        if(visible) lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    if(status_) {
        if(visible) lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(status_, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace firefly
