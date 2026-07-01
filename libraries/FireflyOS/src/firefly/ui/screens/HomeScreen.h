#pragma once

#include "../Screen.h"
#include "../../core/AppRegistry.h"

namespace firefly {

class HomeScreen : public Screen {
public:
    bool create(lv_obj_t * parent, const UiTokens & tokens) override;
    bool populate(const AppRegistry & registry, lv_event_cb_t callback);
    void show() override;
    void hide() override;
    void refresh(const SystemState & state) override;

private:
    static const char * symbolFor(const char * id);
    lv_obj_t * root_ = nullptr;
    lv_obj_t * pager_ = nullptr;
    lv_obj_t * dots_ = nullptr;
    UiTokens tokens_{};
};

}  // namespace firefly
