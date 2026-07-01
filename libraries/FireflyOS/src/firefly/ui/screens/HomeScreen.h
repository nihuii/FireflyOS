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
    static constexpr uint8_t kMaxPages = (AppRegistry::kMaxApps + 5U) / 6U;
    static const char * symbolFor(const char * id);
    static void pagerEventCallback(lv_event_t * event);
    void updateDots(uint8_t active_page);
    lv_obj_t * root_ = nullptr;
    lv_obj_t * pager_ = nullptr;
    lv_obj_t * dots_ = nullptr;
    lv_obj_t * pages_[kMaxPages]{};
    uint8_t page_count_ = 0;
    UiTokens tokens_{};
};

}  // namespace firefly
