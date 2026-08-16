#include "WeatherApp.h"
#include "WeatherIcons.h"

#include <stdio.h>
#include <stdlib.h>

#include "../../ui/UiTheme.h"

namespace firefly {
namespace {

void formatTenths(char * output, size_t capacity, int16_t value) {
    const int magnitude = value < 0 ? -static_cast<int>(value) : value;
    snprintf(output, capacity, "%s%d.%d", value < 0 ? "-" : "",
             magnitude / 10, magnitude % 10);
}

}  // namespace

bool WeatherApp::create(lv_obj_t * parent, UiComponents & components) {
    LV_UNUSED(components);
    if(!parent) return false;
    const UiTokens tokens = UiTheme::fireflyDefault();
    root_ = UiComponents::createPage(parent, tokens);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * title = lv_label_create(root_);
    lv_label_set_text(title, "Weather");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(tokens.text_primary), 0);
    lv_obj_set_pos(title, 28, 52);

    city_label_ = lv_label_create(root_);
    lv_obj_set_width(city_label_, 250);
    lv_label_set_long_mode(city_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(city_label_,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(city_label_, "No location");
    lv_obj_set_pos(city_label_, 30, 88);

    icon_image_ = lv_img_create(root_);
    lv_img_set_src(icon_image_, &weather_icon_cloudy);
    lv_obj_set_style_img_recolor(icon_image_,
                                 lv_color_hex(tokens.firefly_primary), 0);
    lv_obj_set_style_img_recolor_opa(icon_image_, LV_OPA_COVER, 0);
    lv_obj_set_pos(icon_image_, 64, 150);
    lv_obj_add_flag(icon_image_, LV_OBJ_FLAG_HIDDEN);

    temperature_label_ = lv_label_create(root_);
    lv_obj_set_width(temperature_label_, 210);
    lv_obj_set_style_text_align(temperature_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(temperature_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(temperature_label_,
                                lv_color_hex(tokens.text_primary), 0);
    lv_label_set_text(temperature_label_, "--.- C");
    lv_obj_set_pos(temperature_label_, 164, 146);

    range_label_ = lv_label_create(root_);
    lv_obj_set_width(range_label_, 346);
    lv_obj_set_style_text_align(range_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(range_label_,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(range_label_, "High --.-  |  Low --.-");
    lv_obj_set_pos(range_label_, 32, 226);

    lv_obj_t * status_card = UiComponents::createCard(root_, tokens);
    lv_obj_set_size(status_card, 350, 92);
    lv_obj_set_pos(status_card, 30, 270);
    status_label_ = lv_label_create(status_card);
    lv_obj_set_width(status_label_, 318);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(status_label_, "Set a location from the phone");
    lv_obj_center(status_label_);

    refresh_button_ = lv_btn_create(root_);
    lv_obj_set_size(refresh_button_, 350, 54);
    lv_obj_set_pos(refresh_button_, 30, 402);
    lv_obj_set_style_radius(refresh_button_, 18, 0);
    lv_obj_set_style_bg_color(refresh_button_,
                              lv_color_hex(tokens.firefly_primary), 0);
    lv_obj_add_event_cb(refresh_button_, refreshEvent, LV_EVENT_CLICKED, this);
    refresh_button_label_ = lv_label_create(refresh_button_);
    lv_label_set_text(refresh_button_label_, "Refresh");
    lv_obj_set_style_text_color(refresh_button_label_,
                                lv_color_hex(tokens.bg_base), 0);
    lv_obj_center(refresh_button_label_);
    return true;
}

void WeatherApp::destroy() {
    if(root_) lv_obj_del(root_);
    root_ = nullptr;
    city_label_ = nullptr;
    icon_image_ = nullptr;
    temperature_label_ = nullptr;
    range_label_ = nullptr;
    status_label_ = nullptr;
    refresh_button_ = nullptr;
    refresh_button_label_ = nullptr;
    visible_ = false;
}

void WeatherApp::show() {
    visible_ = true;
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void WeatherApp::hide() {
    visible_ = false;
    setUpdating(false);
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

const lv_img_dsc_t * WeatherApp::iconForCode(uint16_t code) {
    if(code == 0) return &weather_icon_clear_day;
    if(code == 1 || code == 2) return &weather_icon_partly_cloudy;
    if(code == 3) return &weather_icon_cloudy;
    if(code == 45 || code == 48) return &weather_icon_fog;
    if(code >= 51 && code <= 57) return &weather_icon_light_rain;
    if(code == 61 || code == 63) return &weather_icon_light_rain;
    if(code == 65) return &weather_icon_heavy_rain;
    if(code == 66 || code == 67) return &weather_icon_sleet;
    if(code >= 71 && code <= 77) return &weather_icon_snow;
    if(code >= 80 && code <= 82) return &weather_icon_showers;
    if(code >= 85 && code <= 86) return &weather_icon_snow;
    if(code >= 95 && code <= 99) return &weather_icon_thunderstorm;
    return &weather_icon_cloudy;
}

const char * WeatherApp::statusFor(WeatherFreshness freshness,
                                   WeatherServiceState state,
                                   bool phone_connected) {
    switch(state) {
        case WeatherServiceState::WaitingForWifi: return "Connecting to Wi-Fi";
        case WeatherServiceState::Updating: return "Updating weather";
        case WeatherServiceState::NoLocation: return "No location | Set it on phone";
        case WeatherServiceState::NoNetwork: return "No network | Cached data remains";
        case WeatherServiceState::ResponseTooLarge: return "Service response was too large";
        case WeatherServiceState::ServiceError: return "Weather service unavailable";
        default: break;
    }
    if(freshness == WeatherFreshness::Old) return "Older than 24 h | Refresh recommended";
    if(freshness == WeatherFreshness::Stale) return "Older than 3 h | Showing cache";
    if(freshness == WeatherFreshness::Expired) {
        return phone_connected ? "Waiting for phone weather" : "No weather data";
    }
    return phone_connected ? "Fresh | Phone source available" : "Fresh cached data";
}

void WeatherApp::setUpdating(bool updating) {
    if(!refresh_button_label_) return;
    lv_label_set_text(refresh_button_label_, updating ? "Updating..." : "Refresh");
    if(refresh_button_) {
        if(updating) lv_obj_add_state(refresh_button_, LV_STATE_DISABLED);
        else lv_obj_clear_state(refresh_button_, LV_STATE_DISABLED);
    }
}

void WeatherApp::refresh(const WeatherSnapshot & snapshot,
                         WeatherFreshness freshness,
                         WeatherServiceState state,
                         bool phone_connected) {
    if(!root_) return;
    const bool updating = state == WeatherServiceState::WaitingForWifi ||
        state == WeatherServiceState::Updating;
    setUpdating(visible_ && updating);
    lv_label_set_text(status_label_, statusFor(freshness, state,
                                               phone_connected));
    if(!snapshot.valid) {
        lv_label_set_text(city_label_, "No location");
        if(icon_image_) lv_obj_add_flag(icon_image_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(temperature_label_, "--.- C");
        lv_label_set_text(range_label_, "High --.-  |  Low --.-");
        return;
    }
    lv_label_set_text(city_label_, snapshot.city);
    if(icon_image_) {
        lv_img_set_src(icon_image_, iconForCode(snapshot.weather_code));
        lv_obj_clear_flag(icon_image_, LV_OBJ_FLAG_HIDDEN);
    }
    char text[64]{};
    char current[12]{};
    char high[12]{};
    char low[12]{};
    formatTenths(current, sizeof(current), snapshot.temperature_tenths_c);
    formatTenths(high, sizeof(high), snapshot.high_tenths_c);
    formatTenths(low, sizeof(low), snapshot.low_tenths_c);
    snprintf(text, sizeof(text), "%s C", current);
    lv_label_set_text(temperature_label_, text);
    snprintf(text, sizeof(text), "High %s  |  Low %s", high, low);
    lv_label_set_text(range_label_, text);
}

void WeatherApp::refreshEvent(lv_event_t * event) {
    auto * self = static_cast<WeatherApp *>(lv_event_get_user_data(event));
    if(!self || !self->visible_ || !self->refresh_callback_) return;
    self->setUpdating(true);
    self->refresh_callback_(self->refresh_context_);
}

}  // namespace firefly
