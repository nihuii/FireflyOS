#include "UiShell.h"

namespace firefly {

lv_obj_t * UiShell::createLayer(lv_obj_t * parent) {
    lv_obj_t * layer = lv_obj_create(parent);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, LV_PCT(100), LV_PCT(100));
    lv_obj_align(layer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return layer;
}

bool UiShell::create(lv_obj_t * screen, const UiTokens &) {
    if(!screen) return false;
    root_ = createLayer(screen);
    app_host_ = createLayer(root_);
    status_bar_ = createLayer(root_);
    panel_host_ = createLayer(root_);
    overlay_host_ = createLayer(root_);
    overlays_.attach(overlay_host_);
    return app_host_ && status_bar_ && panel_host_ && overlay_host_;
}

void UiShell::showRoute(Route route) {
    navigation_.open(route);
}

void UiShell::showControlCenter(bool visible) {
    if(!panel_host_) return;
    if(visible) lv_obj_clear_flag(panel_host_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(panel_host_, LV_OBJ_FLAG_HIDDEN);
}

void UiShell::showNotificationCenter(bool visible) {
    showControlCenter(visible);
}

bool UiShell::showOverlay(uint8_t priority, lv_obj_t * overlay) {
    return overlays_.show(priority, overlay);
}

void UiShell::closeOverlay(lv_obj_t * overlay) {
    overlays_.close(overlay);
}

void UiShell::refresh(const SystemState &, uint32_t revision) {
    if(revision == rendered_revision_) return;
    rendered_revision_ = revision;
}

}  // namespace firefly
