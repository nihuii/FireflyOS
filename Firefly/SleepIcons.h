#pragma once

#include <Arduino.h>
#include <lvgl.h>

#define SLEEP_ICON_COUNT 4

extern const lv_img_dsc_t sleep_icon_0;
extern const lv_img_dsc_t sleep_icon_1;
extern const lv_img_dsc_t sleep_icon_2;
extern const lv_img_dsc_t sleep_icon_3;

const lv_img_dsc_t * get_sleep_icon_by_index(uint8_t index);
