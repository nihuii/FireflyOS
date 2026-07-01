#pragma once

#include <lvgl.h>

#include "NavigationController.h"
#include "UiTokens.h"
#include "../core/SystemState.h"
#include "screens/SystemOverlayHost.h"

namespace firefly {

class UiShell {
public:
    using RouteHandler = void (*)(Route previous, Route current);

    bool create(lv_obj_t * screen, const UiTokens & tokens);
    void setRouteHandler(RouteHandler handler) { route_handler_ = handler; }
    bool showRoute(Route route);
    Route back();
    void syncRoute(Route route);
    void bindPanelPages(lv_obj_t * control_page, lv_obj_t * notification_page);
    void showControlCenter(bool visible);
    void showNotificationCenter(bool visible);
    void bringAppToFront(lv_obj_t * app);
    bool showOverlay(uint8_t priority, lv_obj_t * overlay);
    void closeOverlay(lv_obj_t * overlay);
    void refresh(const SystemState & state, uint32_t revision);

    NavigationController & navigation() { return navigation_; }
    lv_obj_t * appHost() const { return app_host_; }
    lv_obj_t * statusBarHost() const { return status_bar_; }
    lv_obj_t * panelHost() const { return panel_host_; }
    lv_obj_t * overlayHost() const { return overlay_host_; }

private:
    static lv_obj_t * createLayer(lv_obj_t * parent);

    NavigationController navigation_{};
    SystemOverlayHost overlays_{};
    lv_obj_t * root_ = nullptr;
    lv_obj_t * app_host_ = nullptr;
    lv_obj_t * status_bar_ = nullptr;
    lv_obj_t * panel_host_ = nullptr;
    lv_obj_t * overlay_host_ = nullptr;
    lv_obj_t * control_page_ = nullptr;
    lv_obj_t * notification_page_ = nullptr;
    RouteHandler route_handler_ = nullptr;
    uint32_t rendered_revision_ = 0;
};

}  // namespace firefly
