#include "GlanceScreen.h"

namespace firefly {

bool GlanceScreen::create(lv_obj_t * parent, const UiTokens & tokens) {
    if(!parent) return false;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(tokens.bg_base), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    return true;
}

void GlanceScreen::bind(lv_obj_t * root, lv_obj_t * image, lv_obj_t * time,
                        lv_obj_t * date, ImageProvider provider, uint8_t image_count) {
    root_ = root;
    image_ = image;
    time_ = time;
    date_ = date;
    provider_ = provider;
    image_count_ = image_count;
    presentCurrentImage();
}

void GlanceScreen::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void GlanceScreen::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void GlanceScreen::refresh(const SystemState &) {
    presentCurrentImage();
}

void GlanceScreen::advanceImage() {
    if(has_presented_ && image_count_ > 0) {
        image_index_ = (image_index_ + 1U) % image_count_;
    }
    presentCurrentImage();
}

void GlanceScreen::presentCurrentImage() {
    if(!image_ || !provider_ || image_count_ == 0) return;
    lv_img_set_src(image_, provider_(image_index_));
    has_presented_ = true;
}

}  // namespace firefly
