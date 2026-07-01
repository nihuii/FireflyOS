#pragma once

#include <lvgl.h>

#include "UiTokens.h"

namespace firefly {

class UiComponents {
public:
    static lv_obj_t * createPage(lv_obj_t * parent, const UiTokens & tokens);
    static lv_obj_t * createCard(lv_obj_t * parent, const UiTokens & tokens);
    static lv_obj_t * createPrimaryButton(lv_obj_t * parent,
                                          const UiTokens & tokens,
                                          const char * text);
    static lv_obj_t * createTitle(lv_obj_t * parent,
                                  const UiTokens & tokens,
                                  const char * text);
    static void styleSlider(lv_obj_t * slider, const UiTokens & tokens);
    static void styleSwitch(lv_obj_t * sw, const UiTokens & tokens);

    // Migration bridges preserve current page colors while removing local style lambdas.
    static void styleCard(lv_obj_t * obj,
                          lv_color_t color,
                          lv_coord_t radius,
                          lv_opa_t opacity = LV_OPA_COVER);
    static void styleSettingsCard(lv_obj_t * obj,
                                  lv_color_t color,
                                  lv_coord_t radius,
                                  lv_color_t accent,
                                  lv_opa_t opacity = LV_OPA_80);
    static void styleSlider(lv_obj_t * slider,
                            lv_color_t track,
                            lv_color_t indicator,
                            lv_color_t knob);
    static void styleSwitch(lv_obj_t * sw,
                            lv_color_t surface,
                            lv_color_t action);
};

}  // namespace firefly
