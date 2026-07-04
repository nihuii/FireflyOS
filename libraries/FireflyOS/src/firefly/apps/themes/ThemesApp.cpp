#include "ThemesApp.h"

#include <stdio.h>
#include <string.h>

#include "../../ui/UiTheme.h"

namespace firefly {

bool ThemesApp::create(lv_obj_t * parent, UiComponents & components,
                       ThemePackageService & packages,
                       StorageService & storage) {
    LV_UNUSED(components);
    if(!parent) return false;
    packages_ = &packages;
    storage_ = &storage;
    const UiTokens tokens = UiTheme::fireflyDefault();
    root_ = UiComponents::createPage(parent, tokens);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * title = UiComponents::createTitle(root_, tokens, "Themes");
    lv_obj_set_pos(title, 28, 54);
    status_ = lv_label_create(root_);
    lv_label_set_text(status_, "Validated themes only");
    lv_obj_set_style_text_color(status_, lv_color_hex(tokens.text_secondary), 0);
    lv_obj_set_pos(status_, 30, 88);
    list_ = lv_obj_create(root_);
    lv_obj_set_size(list_, 354, 190);
    lv_obj_set_pos(list_, 28, 116);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_, 0, 0);
    lv_obj_set_style_pad_row(list_, 7, 0);
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_, 0, 0);
    preview_ = lv_obj_create(root_);
    lv_obj_set_size(preview_, 240, 96);
    lv_obj_align(preview_, LV_ALIGN_TOP_MID, 0, 320);
    UiComponents::styleSettingsCard(preview_, lv_color_hex(0x102A2D), 22,
                                    lv_color_hex(tokens.firefly_primary), LV_OPA_90);
    preview_name_ = lv_label_create(preview_);
    lv_label_set_text(preview_name_, "Select a theme");
    lv_obj_set_width(preview_name_, 200);
    lv_obj_set_style_text_align(preview_name_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(preview_name_);
    apply_button_ = UiComponents::createPrimaryButton(root_, tokens, "Apply theme");
    lv_obj_set_size(apply_button_, 280, 48);
    lv_obj_align(apply_button_, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_event_cb(apply_button_, applyEvent, LV_EVENT_CLICKED, this);
    return true;
}

void ThemesApp::destroy() {
    releasePreview();
    if(scan_directory_ && storage_) storage_->closeManaged(scan_directory_);
    if(root_) lv_obj_del(root_);
    root_ = status_ = list_ = preview_ = preview_name_ = apply_button_ = nullptr;
}

void ThemesApp::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    requestRefresh();
}

void ThemesApp::hide() {
    releasePreview();
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ThemesApp::bindStorage(StorageService & storage, bool sd_available) {
    storage_ = &storage;
    sd_available_ = sd_available;
}

void ThemesApp::requestRefresh() { refresh_pending_ = true; }

void ThemesApp::tick() {
    if(refresh_pending_) {
        refresh_pending_ = false;
        scanThemes();
    }
    if(scan_active_) scanStep();
}

void ThemesApp::onSdRemoved() {
    sd_available_ = false;
    theme_count_ = 0;
    last_issue_ = ThemeValidationIssue{};
    selected_ = -1;
    if(scan_directory_ && storage_) storage_->closeManaged(scan_directory_);
    scan_active_ = false;
    releasePreview();
    if(status_) lv_label_set_text(status_, "SD card unavailable");
    renderList();
}

void ThemesApp::releasePreview() {
    preview_loaded_ = false;
    if(preview_name_) lv_label_set_text(preview_name_, "Select a theme");
}

bool ThemesApp::importSelected() {
    if(!packages_ || !storage_ || !sd_available_ ||
       selected_ < 0 || selected_ >= theme_count_) return false;
    SystemSettings previous{};
    if(!storage_->loadSettings(previous)) {
        ThemeValidationIssue issue{};
        issue.error = ThemeValidationError::StorageUnavailable;
        strlcpy(issue.resource, "settings", sizeof(issue.resource));
        showError(issue);
        return false;
    }
    ThemeValidationIssue issue{};
    if(packages_->importPackage(*storage_, themes_[selected_].root, &issue)) {
        memcpy(applied_palette_, themes_[selected_].manifest.palette,
               sizeof(applied_palette_));
        applied_palette_pending_ = true;
        lv_label_set_text(status_, "Theme applied without restart");
        return true;
    }
    storage_->saveSettings(previous);
    showError(issue);
    return false;
}

bool ThemesApp::takeAppliedPalette(uint32_t out[5]) {
    if(!out || !applied_palette_pending_) return false;
    memcpy(out, applied_palette_, sizeof(applied_palette_));
    applied_palette_pending_ = false;
    return true;
}

bool ThemesApp::scanThemes() {
    if(scan_directory_ && storage_) storage_->closeManaged(scan_directory_);
    scan_active_ = false;
    theme_count_ = 0;
    last_issue_ = ThemeValidationIssue{};
    selected_ = -1;
    releasePreview();
    if(!storage_ || !sd_available_) {
        lv_label_set_text(status_, "SD card unavailable");
        renderList();
        return false;
    }
    scan_directory_ = storage_->openManaged("/FireflyOS/Themes", FILE_READ);
    bool is_directory = false;
    if(!scan_directory_ ||
       !storage_->managedFileIsDirectory(scan_directory_, is_directory) ||
       !is_directory) {
        if(scan_directory_) storage_->closeManaged(scan_directory_);
        lv_label_set_text(status_, "Themes folder unavailable");
        renderList();
        return false;
    }
    scan_active_ = true;
    lv_label_set_text(status_, "Validating themes...");
    renderList();
    return true;
}

void ThemesApp::scanStep() {
    if(!scan_active_ || !scan_directory_) return;
    if(theme_count_ >= kMaxThemeCount) {
        finishScan();
        return;
    }
    fs::File entry = storage_->openNextManaged(scan_directory_);
    if(!entry) {
        finishScan();
        return;
    }
    bool is_directory = false;
    char root[96]{};
    const bool metadata_ok =
        storage_->managedFileIsDirectory(entry, is_directory) &&
        (!is_directory || storage_->managedFilePath(entry, root, sizeof(root)));
    if(metadata_ok && is_directory) {
        ThemeValidationIssue issue{};
        ThemeManifest manifest{};
        storage_->closeManaged(entry);
        if(validateThemeRoot(root, manifest, issue)) {
            strlcpy(themes_[theme_count_].root, root,
                    sizeof(themes_[theme_count_].root));
            themes_[theme_count_].manifest = manifest;
            ++theme_count_;
        } else {
            last_issue_ = issue;
        }
    } else {
        storage_->closeManaged(entry);
    }
}

void ThemesApp::finishScan() {
    if(scan_directory_ && storage_) storage_->closeManaged(scan_directory_);
    scan_active_ = false;
    if(theme_count_ == 0 && last_issue_.error != ThemeValidationError::None) {
        showError(last_issue_);
        renderList();
        return;
    }
    char status[48];
    snprintf(status, sizeof(status), "%u validated themes",
             static_cast<unsigned>(theme_count_));
    lv_label_set_text(status_, status);
    renderList();
}

bool ThemesApp::validateThemeRoot(const char * root,
                                  ThemeManifest & manifest,
                                  ThemeValidationIssue & issue) const {
    if(!packages_ || !storage_) {
        issue = ThemeValidationIssue{};
        issue.error = ThemeValidationError::StorageUnavailable;
        strlcpy(issue.resource, "storage", sizeof(issue.resource));
        return false;
    }
    return packages_->validatePackage(*storage_, root, manifest, issue);
}

void ThemesApp::renderList() {
    if(!list_) return;
    lv_obj_clean(list_);
    if(theme_count_ == 0) {
        lv_obj_t * empty = lv_label_create(list_);
        lv_label_set_text(empty, sd_available_ ? "No valid themes" : "SD card unavailable");
        return;
    }
    for(uint8_t i = 0; i < theme_count_; ++i) {
        row_contexts_[i].app = this;
        row_contexts_[i].index = i;
        lv_obj_t * row = lv_btn_create(list_);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 58);
        lv_obj_add_event_cb(row, themeEvent, LV_EVENT_CLICKED, &row_contexts_[i]);
        lv_obj_t * label = lv_label_create(row);
        lv_label_set_text(label, themes_[i].manifest.id);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 2, -8);
        lv_obj_t * author = lv_label_create(row);
        lv_label_set_text(author, themes_[i].manifest.author);
        lv_obj_set_style_text_color(author, lv_color_hex(0x8BA6AA), 0);
        lv_obj_align(author, LV_ALIGN_LEFT_MID, 2, 13);
    }
}

void ThemesApp::showError(const ThemeValidationIssue & issue) {
    char text[160];
    if(issue.resource[0] && issue.limit > 0) {
        const char * relation = issue.error == ThemeValidationError::ResourceTooLarge
            ? ">" : "expected";
        snprintf(text, sizeof(text), "%s: %s (%lu %s %lu)",
                 issue.resource, ThemePackageService::errorText(issue.error),
                 static_cast<unsigned long>(issue.actual),
                 relation,
                 static_cast<unsigned long>(issue.limit));
    } else if(issue.resource[0]) {
        snprintf(text, sizeof(text), "%s: %s", issue.resource,
                 ThemePackageService::errorText(issue.error));
    } else {
        strlcpy(text, ThemePackageService::errorText(issue.error), sizeof(text));
    }
    lv_label_set_text(status_, text);
}

void ThemesApp::themeEvent(lv_event_t * event) {
    RowContext * context = static_cast<RowContext *>(lv_event_get_user_data(event));
    if(!context || !context->app || context->index >= context->app->theme_count_) return;
    ThemesApp * app = context->app;
    app->selected_ = context->index;
    app->preview_loaded_ = true;
    lv_label_set_text(app->preview_name_,
                      app->themes_[context->index].manifest.id);
}

void ThemesApp::applyEvent(lv_event_t * event) {
    ThemesApp * app = static_cast<ThemesApp *>(lv_event_get_user_data(event));
    if(app) app->importSelected();
}

}  // namespace firefly
