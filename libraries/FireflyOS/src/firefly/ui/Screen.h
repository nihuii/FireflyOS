#pragma once

#include <lvgl.h>

#include "UiTokens.h"
#include "../core/SystemState.h"

namespace firefly {

class Screen {
public:
    virtual ~Screen() = default;
    virtual bool create(lv_obj_t * parent, const UiTokens & tokens) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void refresh(const SystemState & state) = 0;
};

}  // namespace firefly
