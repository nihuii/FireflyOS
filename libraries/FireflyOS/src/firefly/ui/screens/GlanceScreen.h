#pragma once

#include "../Screen.h"

namespace firefly {

class GlanceScreen : public Screen {
public:
    using ImageProvider = const void * (*)(uint8_t index);

    bool create(lv_obj_t * parent, const UiTokens & tokens) override;
    void bind(lv_obj_t * root, lv_obj_t * image, lv_obj_t * time, lv_obj_t * date,
              ImageProvider provider, uint8_t image_count);
    void show() override;
    void hide() override;
    void refresh(const SystemState & state) override;
    void advanceImage();
    void presentCurrentImage();
    uint8_t currentImage() const { return image_index_; }

private:
    lv_obj_t * root_ = nullptr;
    lv_obj_t * image_ = nullptr;
    lv_obj_t * time_ = nullptr;
    lv_obj_t * date_ = nullptr;
    ImageProvider provider_ = nullptr;
    uint8_t image_count_ = 0;
    uint8_t image_index_ = 0;
    bool has_presented_ = false;
};

}  // namespace firefly
