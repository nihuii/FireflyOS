#pragma once

#include "../Screen.h"

namespace firefly {

class AppShellScreen : public Screen {
public:
    bool create(lv_obj_t * parent, const UiTokens & tokens) override;
    void show() override;
    void hide() override;
    void refresh(const SystemState & state) override;
    void setTitle(const char * title);
    lv_obj_t * root() const { return root_; }

private:
    lv_obj_t * root_ = nullptr;
    lv_obj_t * title_ = nullptr;
    lv_obj_t * status_ = nullptr;
};

}  // namespace firefly
