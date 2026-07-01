#pragma once

#include "../Screen.h"
#include "../../core/NotificationModel.h"

namespace firefly {

class NotificationCenter : public Screen {
public:
    static constexpr uint8_t kVisibleLimit = 3;

    bool create(lv_obj_t * parent, const UiTokens & tokens) override;
    void show() override;
    void hide() override;
    void refresh(const SystemState & state) override;
    void setNotifications(const NotificationSummary * notifications, uint8_t count);
    void setControlCallback(lv_event_cb_t callback);
    void setClearCallback(lv_event_cb_t callback);
    void clear();
    uint8_t count() const { return count_; }
    lv_obj_t * root() const { return root_; }

private:
    void render();

    lv_obj_t * root_ = nullptr;
    lv_obj_t * empty_ = nullptr;
    lv_obj_t * cards_[kVisibleLimit]{};
    lv_obj_t * app_labels_[kVisibleLimit]{};
    lv_obj_t * title_labels_[kVisibleLimit]{};
    lv_obj_t * body_labels_[kVisibleLimit]{};
    lv_obj_t * control_button_ = nullptr;
    lv_obj_t * clear_button_ = nullptr;
    NotificationSummary notifications_[kVisibleLimit]{};
    uint8_t count_ = 0;
};

}  // namespace firefly
