#include "SystemOverlayHost.h"

namespace firefly {

bool SystemOverlayHost::attach(lv_obj_t * host) {
    host_ = host;
    clear();
    return host_ != nullptr;
}

bool SystemOverlayHost::show(uint8_t priority, lv_obj_t * overlay) {
    if(!host_ || !overlay || priority < 1 || priority > 5) return false;
    if(slots_[priority] && slots_[priority] != overlay) {
        lv_obj_add_flag(slots_[priority], LV_OBJ_FLAG_HIDDEN);
    }
    slots_[priority] = overlay;
    if(!acceptsPriority(priority_, priority)) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    activateHighest();
    return true;
}

void SystemOverlayHost::close(lv_obj_t * overlay) {
    if(!overlay) return;
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    for(uint8_t i = 1; i <= 5; ++i) {
        if(slots_[i] == overlay) slots_[i] = nullptr;
    }
    activateHighest();
}

void SystemOverlayHost::clear() {
    if(current_) lv_obj_add_flag(current_, LV_OBJ_FLAG_HIDDEN);
    for(uint8_t i = 1; i <= 5; ++i) slots_[i] = nullptr;
    current_ = nullptr;
    priority_ = 0;
}

void SystemOverlayHost::activateHighest() {
    lv_obj_t * next = nullptr;
    uint8_t next_priority = 0;
    for(int8_t i = 5; i >= 1; --i) {
        if(slots_[i]) {
            next = slots_[i];
            next_priority = static_cast<uint8_t>(i);
            break;
        }
    }
    if(current_ && current_ != next) lv_obj_add_flag(current_, LV_OBJ_FLAG_HIDDEN);
    current_ = next;
    priority_ = next_priority;
    if(current_) {
        lv_obj_clear_flag(current_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(current_);
        lv_obj_move_foreground(host_);
    }
}

}  // namespace firefly
