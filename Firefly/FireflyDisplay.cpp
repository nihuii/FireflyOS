#include "FireflyApp.h"

void my_disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {
    const uint32_t width = area->x2 - area->x1 + 1U;
    const uint32_t height = area->y2 - area->y1 + 1U;
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, width, height);
    lv_disp_flush_ready(disp_drv);
}

void my_rounder_cb(lv_disp_drv_t * disp_drv, lv_area_t * area) {
    LV_UNUSED(disp_drv);
    area->x1 &= ~1;
    area->x2 |= 1;
    area->y1 &= ~1;
    area->y2 |= 1;
}

void my_touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data) {
    LV_UNUSED(indev_drv);

    if(touch_touched()) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch_last_x;
        data->point.y = touch_last_y;
        last_activity_time = millis();
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
