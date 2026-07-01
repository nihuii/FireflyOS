#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace firefly {

class SystemOverlayHost {
public:
    bool attach(lv_obj_t * host);
    bool show(uint8_t priority, lv_obj_t * overlay);
    void close(lv_obj_t * overlay);
    void clear();

    uint8_t priority() const { return priority_; }
    lv_obj_t * current() const { return current_; }

private:
    lv_obj_t * host_ = nullptr;
    lv_obj_t * current_ = nullptr;
    uint8_t priority_ = 0;
};

}  // namespace firefly
