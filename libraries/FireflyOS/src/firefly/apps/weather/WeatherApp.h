#pragma once

#include <lvgl.h>

#include "../../services/WeatherService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

class WeatherApp {
public:
    using RefreshCallback = void (*)(void * context);

    bool create(lv_obj_t * parent, UiComponents & components);
    void destroy();
    void show();
    void hide();
    void refresh(const WeatherSnapshot & snapshot,
                 WeatherFreshness freshness,
                 WeatherServiceState state,
                 bool phone_connected);
    void setRefreshCallback(RefreshCallback callback,
                            void * context = nullptr) {
        refresh_callback_ = callback;
        refresh_context_ = context;
    }
    lv_obj_t * root() const { return root_; }

private:
    static void refreshEvent(lv_event_t * event);
    static const lv_img_dsc_t * iconForCode(uint16_t code);
    static const char * statusFor(WeatherFreshness freshness,
                                  WeatherServiceState state,
                                  bool phone_connected);
    void setUpdating(bool updating);

    lv_obj_t * root_ = nullptr;
    lv_obj_t * city_label_ = nullptr;
    lv_obj_t * icon_image_ = nullptr;
    lv_obj_t * temperature_label_ = nullptr;
    lv_obj_t * range_label_ = nullptr;
    lv_obj_t * status_label_ = nullptr;
    lv_obj_t * refresh_button_ = nullptr;
    lv_obj_t * refresh_button_label_ = nullptr;
    RefreshCallback refresh_callback_ = nullptr;
    void * refresh_context_ = nullptr;
    bool visible_ = false;
};

}  // namespace firefly
