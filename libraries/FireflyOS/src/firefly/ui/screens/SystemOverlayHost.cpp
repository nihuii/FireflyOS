#include "SystemOverlayHost.h"

namespace firefly {

bool SystemOverlayHost::attach(lv_obj_t * host) {
    host_ = host;
    clear();
    return host_ != nullptr;
}

bool SystemOverlayHost::show(uint8_t priority, lv_obj_t * overlay) {
    if(!host_ || !overlay || priority < 1 || priority > 5) return false;
    if(current_ && priority < priority_) return false;
    if(current_ && current_ != overlay) lv_obj_add_flag(current_, LV_OBJ_FLAG_HIDDEN);
    current_ = overlay;
    priority_ = priority;
    lv_obj_clear_flag(current_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(host_);
    return true;
}

void SystemOverlayHost::close(lv_obj_t * overlay) {
    if(!overlay || current_ != overlay) return;
    lv_obj_add_flag(current_, LV_OBJ_FLAG_HIDDEN);
    current_ = nullptr;
    priority_ = 0;
}

void SystemOverlayHost::clear() {
    if(current_) lv_obj_add_flag(current_, LV_OBJ_FLAG_HIDDEN);
    current_ = nullptr;
    priority_ = 0;
}

}  // namespace firefly
