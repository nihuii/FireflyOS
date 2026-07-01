#pragma once

#include "../Screen.h"

namespace firefly {

class ControlCenter : public Screen {
public:
    bool create(lv_obj_t * parent, const UiTokens & tokens) override;
    void bind(lv_obj_t * root, lv_obj_t * detail, lv_obj_t * volume_slider,
              lv_obj_t * volume_value, lv_obj_t * brightness_slider,
              lv_obj_t * brightness_value);
    void show() override;
    void hide() override;
    void refresh(const SystemState & state) override;
    void refresh(const SystemState & state, uint8_t volume, uint8_t brightness);
    void refresh(const SystemState & state, uint8_t volume, uint8_t brightness,
                 uint32_t revision);
    lv_obj_t * root() const { return root_; }

private:
    lv_obj_t * root_ = nullptr;
    lv_obj_t * detail_ = nullptr;
    lv_obj_t * volume_slider_ = nullptr;
    lv_obj_t * volume_value_ = nullptr;
    lv_obj_t * brightness_slider_ = nullptr;
    lv_obj_t * brightness_value_ = nullptr;
    uint8_t volume_ = 0;
    uint8_t brightness_ = 0;
    uint32_t rendered_revision_ = static_cast<uint32_t>(-1);
};

}  // namespace firefly
