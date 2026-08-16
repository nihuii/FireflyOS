#pragma once

#include <lvgl.h>

#include "../../services/UpdateService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

class UpdateApp {
public:
    using ActionCallback = void (*)(void * context);

    bool create(lv_obj_t * parent, UiComponents & components);
    void destroy();
    void show();
    void hide();
    void refresh(const UpdateSnapshot & snapshot);

    void setStartCallback(ActionCallback callback, void * context = nullptr) {
        start_callback_ = callback;
        start_context_ = context;
    }
    void setCancelCallback(ActionCallback callback, void * context = nullptr) {
        cancel_callback_ = callback;
        cancel_context_ = context;
    }
    void setDiagnosticsCallback(ActionCallback callback,
                                void * context = nullptr) {
        diagnostics_callback_ = callback;
        diagnostics_context_ = context;
    }
    lv_obj_t * root() const { return root_; }

private:
    static void primaryEvent(lv_event_t * event);
    static void secondaryEvent(lv_event_t * event);
    static void animateIcon(void * object, int32_t angle);
    static const char * failureText(UpdateFailure failure);
    void setAnimation(bool active);
    void setButtonVisible(lv_obj_t * button, bool visible);

    lv_obj_t * root_ = nullptr;
    lv_obj_t * icon_label_ = nullptr;
    lv_obj_t * state_label_ = nullptr;
    lv_obj_t * detail_label_ = nullptr;
    lv_obj_t * progress_bar_ = nullptr;
    lv_obj_t * percent_label_ = nullptr;
    lv_obj_t * primary_button_ = nullptr;
    lv_obj_t * primary_label_ = nullptr;
    lv_obj_t * secondary_button_ = nullptr;
    lv_obj_t * secondary_label_ = nullptr;
    ActionCallback start_callback_ = nullptr;
    ActionCallback cancel_callback_ = nullptr;
    ActionCallback diagnostics_callback_ = nullptr;
    void * start_context_ = nullptr;
    void * cancel_context_ = nullptr;
    void * diagnostics_context_ = nullptr;
    bool visible_ = false;
    bool secondary_is_cancel_ = false;
};

}  // namespace firefly
