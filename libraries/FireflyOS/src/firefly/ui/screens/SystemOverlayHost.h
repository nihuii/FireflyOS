#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace firefly {

class SystemOverlayHost {
public:
    static bool acceptsPriority(uint8_t current, uint8_t incoming) {
        return incoming >= 1 && incoming <= 5 && incoming >= current;
    }
    bool attach(lv_obj_t * host);
    bool show(uint8_t priority, lv_obj_t * overlay);
    void close(lv_obj_t * overlay);
    void clear();

    uint8_t priority() const { return priority_; }
    lv_obj_t * current() const { return current_; }

private:
    void activateHighest();
    lv_obj_t * host_ = nullptr;
    lv_obj_t * current_ = nullptr;
    lv_obj_t * slots_[6]{};
    uint8_t priority_ = 0;
};

}  // namespace firefly
