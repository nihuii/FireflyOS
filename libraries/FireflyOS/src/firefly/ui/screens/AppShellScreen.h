#pragma once

#include "../Screen.h"
#include "../../services/CompanionSyncService.h"

namespace firefly {

class AppShellScreen : public Screen {
public:
    bool create(lv_obj_t * parent, const UiTokens & tokens) override;
    void show() override;
    void hide() override;
    void refresh(const SystemState & state) override;
    void setTitle(const char * title);
    void showWeather(const CompanionWeatherView & weather);
    lv_obj_t * root() const { return root_; }

private:
    void setWeatherVisible(bool visible);

    lv_obj_t * root_ = nullptr;
    lv_obj_t * title_ = nullptr;
    lv_obj_t * status_ = nullptr;
    lv_obj_t * weather_city_ = nullptr;
    lv_obj_t * weather_temperature_ = nullptr;
    lv_obj_t * weather_range_ = nullptr;
    lv_obj_t * weather_code_ = nullptr;
    lv_obj_t * weather_status_ = nullptr;
};

}  // namespace firefly
