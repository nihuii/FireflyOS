#pragma once

#include "../Screen.h"

namespace firefly {

class LockScreen : public Screen {
public:
    bool create(lv_obj_t * parent, const UiTokens & tokens) override;
    void bind(lv_obj_t * root, lv_obj_t * date, lv_obj_t * time, lv_obj_t * week);
    void show() override;
    void hide() override;
    void refresh(const SystemState & state) override;
    void setNextAlarm(const char * text);

private:
    lv_obj_t * root_ = nullptr;
    lv_obj_t * date_ = nullptr;
    lv_obj_t * time_ = nullptr;
    lv_obj_t * week_ = nullptr;
    lv_obj_t * status_ = nullptr;
    lv_obj_t * alarm_ = nullptr;
};

}  // namespace firefly
