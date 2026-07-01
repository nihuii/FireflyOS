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

bool UiShell::showRoute(Route route) {
    const Route previous = navigation_.current();
    if(route == previous) return true;
    if(!navigation_.open(route)) return false;
    if(route_handler_) route_handler_(previous, navigation_.current());
    return true;
}

Route UiShell::back() {
    const Route previous = navigation_.current();
    const Route current = navigation_.back();
    if(route_handler_ && previous != current) route_handler_(previous, current);
    return current;
}

void UiShell::syncRoute(Route route) {
    if(route == navigation_.current()) return;
    navigation_.open(route);
}

void UiShell::bindPanelPages(lv_obj_t * control_page,
                             lv_obj_t * notification_page) {
    control_page_ = control_page;
    notification_page_ = notification_page;
}

void UiShell::showControlCenter(bool visible) {
    if(!control_page_) return;
    if(visible) {
        lv_obj_clear_flag(control_page_, LV_OBJ_FLAG_HIDDEN);
        if(notification_page_) lv_obj_add_flag(notification_page_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(control_page_, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiShell::showNotificationCenter(bool visible) {
    if(!notification_page_) return;
    if(visible) {
        lv_obj_clear_flag(notification_page_, LV_OBJ_FLAG_HIDDEN);
        if(control_page_) lv_obj_add_flag(control_page_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(notification_page_, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiShell::bringAppToFront(lv_obj_t * app) {
    if(app && lv_obj_get_parent(app) == app_host_) lv_obj_move_foreground(app);
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
