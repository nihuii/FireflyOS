#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "../../services/StorageService.h"
#include "../../services/ThemePackageService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

class ThemesApp {
public:
    static constexpr uint8_t kMaxThemeCount = 16;

    bool create(lv_obj_t * parent, UiComponents & components,
                ThemePackageService & packages, StorageService & storage);
    void destroy();
    void show();
    void hide();
    void bindStorage(StorageService & storage, bool sd_available);
    void requestRefresh();
    void tick();
    void onSdRemoved();
    void releasePreview();
    bool importSelected();
    bool takeAppliedPalette(uint32_t out[5]);
    const char * appliedThemeId() const { return applied_theme_id_; }
    lv_obj_t * root() const { return root_; }

private:
    struct ThemeEntry {
        char root[96]{};
        ThemeManifest manifest{};
    };
    struct RowContext {
        ThemesApp * app = nullptr;
        uint8_t index = 0;
    };
    static void themeEvent(lv_event_t * event);
    static void applyEvent(lv_event_t * event);
    bool scanThemes();
    void scanStep();
    void finishScan();
    bool validateThemeRoot(const char * root, ThemeManifest & manifest,
                           ThemeValidationIssue & issue) const;
    void renderList();
    void showError(const ThemeValidationIssue & issue);

    lv_obj_t * root_ = nullptr;
    lv_obj_t * status_ = nullptr;
    lv_obj_t * list_ = nullptr;
    lv_obj_t * preview_ = nullptr;
    lv_obj_t * preview_name_ = nullptr;
    lv_obj_t * apply_button_ = nullptr;
    ThemePackageService * packages_ = nullptr;
    StorageService * storage_ = nullptr;
    fs::File scan_directory_{};
    ThemeEntry themes_[kMaxThemeCount]{};
    RowContext row_contexts_[kMaxThemeCount]{};
    uint8_t theme_count_ = 0;
    ThemeValidationIssue last_issue_{};
    int8_t selected_ = -1;
    bool sd_available_ = false;
    bool refresh_pending_ = false;
    bool preview_loaded_ = false;
    bool scan_active_ = false;
    bool applied_palette_pending_ = false;
    uint32_t applied_palette_[5]{};
    char applied_theme_id_[24]{};
};

}  // namespace firefly
