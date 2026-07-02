#pragma once

#include <lvgl.h>

#include "../../services/MotionService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

class ActivityApp {
public:
    static constexpr uint32_t kDefaultGoalSteps = 8000;

    bool create(lv_obj_t * parent, UiComponents & components);
    void destroy();
    void show();
    void hide();
    void refresh(const MotionSummary & summary);
    lv_obj_t * root() const { return root_; }

private:
    lv_obj_t * root_ = nullptr;
    lv_obj_t * status_label_ = nullptr;
    lv_obj_t * goal_arc_ = nullptr;
    lv_obj_t * steps_label_ = nullptr;
    lv_obj_t * active_label_ = nullptr;
    lv_obj_t * goal_label_ = nullptr;
};

}  // namespace firefly
