#include "FilesApp.h"

#include <stdio.h>
#include <string.h>

#include "../../ui/UiTheme.h"

namespace firefly {
namespace {

constexpr const char * kManagedDirectories[] = {
    "Music", "Recordings", "Pictures", "Themes",
    "Updates", "Backups", "Logs",
};

bool safeName(const char * name) {
    return name && name[0] && !strstr(name, "..") &&
           !strchr(name, '/') && !strchr(name, '\\') && !strchr(name, ':');
}

lv_obj_t * makeRow(lv_obj_t * parent, const char * title,
                   const char * detail, lv_event_cb_t callback,
                   void * context, lv_event_code_t code) {
    lv_obj_t * row = lv_btn_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 60);
    UiComponents::styleSettingsCard(row, lv_color_hex(0x0D171D), 18,
                                    lv_color_hex(0x62E8CA), LV_OPA_90);
    if(callback) lv_obj_add_event_cb(row, callback, code, context);
    lv_obj_t * name = lv_label_create(row);
    lv_label_set_text(name, title ? title : "");
    lv_obj_set_style_text_color(name, lv_color_hex(0xEFFFFB), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 4, detail ? -9 : 0);
    if(detail) {
        lv_obj_t * meta = lv_label_create(row);
        lv_label_set_text(meta, detail);
        lv_obj_set_style_text_color(meta, lv_color_hex(0x8BA6AA), 0);
        lv_obj_align(meta, LV_ALIGN_LEFT_MID, 4, 13);
    }
    return row;
}

}  // namespace

bool FilesApp::create(lv_obj_t * parent, UiComponents & components) {
    LV_UNUSED(components);
    if(!parent) return false;
    root_ = UiComponents::createPage(parent, UiTheme::fireflyDefault());
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    title_ = UiComponents::createTitle(root_, UiTheme::fireflyDefault(), "Files");
    lv_obj_set_pos(title_, 28, 54);
    status_ = lv_label_create(root_);
    lv_label_set_text(status_, "Managed folders");
    lv_obj_set_style_text_color(status_, lv_color_hex(0x8BA6AA), 0);
    lv_obj_set_pos(status_, 30, 88);
    back_ = lv_btn_create(root_);
    lv_obj_set_size(back_, 48, 48);
    lv_obj_set_pos(back_, 334, 48);
    lv_obj_add_event_cb(back_, backEvent, LV_EVENT_CLICKED, this);
    lv_obj_t * back_label = lv_label_create(back_);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    lv_obj_add_flag(back_, LV_OBJ_FLAG_HIDDEN);
    list_ = lv_obj_create(root_);
    lv_obj_set_size(list_, 354, 350);
    lv_obj_set_pos(list_, 28, 118);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_, 0, 0);
    lv_obj_set_style_pad_row(list_, 8, 0);
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_, 0, 0);
    showRoots();
    return true;
}

void FilesApp::destroy() {
    if(scanner_) scanner_->cancel();
    if(root_) lv_obj_del(root_);
    root_ = title_ = status_ = list_ = back_ = confirmation_ = nullptr;
}

void FilesApp::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    requestRefresh();
}

void FilesApp::hide() {
    closeConfirmation();
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void FilesApp::requestRefresh() { refresh_pending_ = true; }

void FilesApp::bindStorage(StorageService & storage,
                           FileScanService & scanner,
                           bool sd_available) {
    storage_ = &storage;
    scanner_ = &scanner;
    sd_available_ = sd_available;
}

void FilesApp::tick() {
    if(refresh_pending_) {
        refresh_pending_ = false;
        if(!sd_available_) {
            onSdRemoved();
            return;
        }
        if(current_directory_[0]) scanPage();
        else showRoots();
    }
}

void FilesApp::onPageReady() {
    if(!scanner_) return;
    FileScanPage page{};
    if(!scanner_->takeResult(page) || page.generation != scan_generation_) return;
    if(page.storage_unavailable) {
        onSdRemoved();
        return;
    }
    count_ = page.count;
    has_more_ = page.has_more;
    for(uint8_t i = 0; i < count_; ++i) {
        strlcpy(items_[i].name, page.items[i].name, sizeof(items_[i].name));
        items_[i].size = page.items[i].size;
        items_[i].directory = page.items[i].directory;
    }
    renderPage();
}

void FilesApp::onSdRemoved() {
    sd_available_ = false;
    if(scanner_) scanner_->cancel();
    count_ = 0;
    has_more_ = false;
    showMessage("SD card unavailable");
}

bool FilesApp::canDeleteFile(const char * directory,
                             const FileListItem & item) {
    if(item.directory || !safeName(item.name) || !directory) return false;
    return strcmp(directory, "Music") == 0 ||
           strcmp(directory, "Recordings") == 0 ||
           strcmp(directory, "Pictures") == 0 ||
           strcmp(directory, "Themes") == 0;
}

bool FilesApp::scanPage() {
    if(!storage_ || !scanner_) return false;
    ++scan_generation_;
    if(!scanner_->request(*storage_, current_directory_, offset_,
                          scan_generation_)) {
        showMessage("Folder unavailable");
        return false;
    }
    count_ = 0;
    has_more_ = false;
    if(back_) lv_obj_clear_flag(back_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_, "Scanning folder...");
    return true;
}

bool FilesApp::deletePending() {
    if(!storage_ || pending_delete_ < 0 || pending_delete_ >= count_) return false;
    const FileListItem & item = items_[pending_delete_];
    if(!canDeleteFile(current_directory_, item)) return false;
    char path[128];
    snprintf(path, sizeof(path), "/FireflyOS/%s/%s",
             current_directory_, item.name);
    const bool removed = storage_->removeManaged(path);
    closeConfirmation();
    if(removed) requestRefresh();
    else lv_label_set_text(status_, "Delete failed; item kept");
    return removed;
}

void FilesApp::showRoots() {
    if(!list_) return;
    lv_obj_clean(list_);
    current_directory_[0] = '\0';
    offset_ = 0;
    if(back_) lv_obj_add_flag(back_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_, sd_available_ ? "7 managed folders" : "Managed folders");
    for(uint8_t i = 0; i < kManagedDirectoryCount; ++i) {
        root_contexts_[i].app = this;
        root_contexts_[i].index = i;
        makeRow(list_, kManagedDirectories[i], "Open folder", rootRowEvent,
                &root_contexts_[i], LV_EVENT_CLICKED);
    }
}

void FilesApp::renderPage() {
    if(!list_) return;
    lv_obj_clean(list_);
    char text[48];
    snprintf(text, sizeof(text), "%s  %u-%u", current_directory_,
             static_cast<unsigned>(offset_ + (count_ ? 1 : 0)),
             static_cast<unsigned>(offset_ + count_));
    lv_label_set_text(status_, text);
    if(count_ == 0) {
        makeRow(list_, "No files", "This folder is empty", nullptr, nullptr,
                LV_EVENT_CLICKED);
    }
    for(uint8_t i = 0; i < count_; ++i) {
        row_contexts_[i].app = this;
        row_contexts_[i].index = i;
        char detail[32];
        snprintf(detail, sizeof(detail), items_[i].directory ? "Folder" : "%lu bytes",
                 static_cast<unsigned long>(items_[i].size));
        makeRow(list_, items_[i].name, detail, fileRowEvent,
                &row_contexts_[i], LV_EVENT_LONG_PRESSED);
    }
    if(has_more_) {
        makeRow(list_, "Next 32", "Load the next page", nextPageEvent, this,
                LV_EVENT_CLICKED);
    }
}

void FilesApp::showMessage(const char * message) {
    if(!list_) return;
    lv_obj_clean(list_);
    lv_label_set_text(status_, message ? message : "Unavailable");
    makeRow(list_, message, "Core apps remain available", nullptr, nullptr,
            LV_EVENT_CLICKED);
}

void FilesApp::closeConfirmation() {
    if(confirmation_) lv_obj_add_flag(confirmation_, LV_OBJ_FLAG_HIDDEN);
    pending_delete_ = -1;
}

void FilesApp::rootRowEvent(lv_event_t * event) {
    RowContext * context = static_cast<RowContext *>(lv_event_get_user_data(event));
    if(!context || !context->app || context->index >= kManagedDirectoryCount) return;
    FilesApp * app = context->app;
    strlcpy(app->current_directory_, kManagedDirectories[context->index],
            sizeof(app->current_directory_));
    app->offset_ = 0;
    app->requestRefresh();
}

void FilesApp::fileRowEvent(lv_event_t * event) {
    RowContext * context = static_cast<RowContext *>(lv_event_get_user_data(event));
    if(!context || !context->app || context->index >= context->app->count_) return;
    FilesApp * app = context->app;
    if(!canDeleteFile(app->current_directory_, app->items_[context->index])) return;
    app->pending_delete_ = context->index;
    if(!app->confirmation_) {
        app->confirmation_ = lv_obj_create(app->root_);
        lv_obj_set_size(app->confirmation_, 330, 190);
        lv_obj_align(app->confirmation_, LV_ALIGN_CENTER, 0, 10);
        UiComponents::styleSettingsCard(app->confirmation_, lv_color_hex(0x132229),
                                        24, lv_color_hex(0xFF626A), LV_OPA_COVER);
        lv_obj_t * label = lv_label_create(app->confirmation_);
        lv_label_set_text(label, "Delete this file?");
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 18);
        lv_obj_t * cancel = lv_btn_create(app->confirmation_);
        lv_obj_set_size(cancel, 130, 48);
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 8, -12);
        lv_obj_add_event_cb(cancel, cancelDeleteEvent, LV_EVENT_CLICKED, app);
        lv_obj_t * cancel_text = lv_label_create(cancel);
        lv_label_set_text(cancel_text, "Cancel");
        lv_obj_center(cancel_text);
        lv_obj_t * confirm = lv_btn_create(app->confirmation_);
        lv_obj_set_size(confirm, 130, 48);
        lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, -8, -12);
        lv_obj_add_event_cb(confirm, confirmDeleteEvent, LV_EVENT_CLICKED, app);
        lv_obj_t * confirm_text = lv_label_create(confirm);
        lv_label_set_text(confirm_text, "Delete");
        lv_obj_center(confirm_text);
    }
    lv_obj_clear_flag(app->confirmation_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(app->confirmation_);
}

void FilesApp::nextPageEvent(lv_event_t * event) {
    FilesApp * app = static_cast<FilesApp *>(lv_event_get_user_data(event));
    if(!app || !app->has_more_) return;
    app->offset_ += app->count_;
    app->requestRefresh();
}

void FilesApp::backEvent(lv_event_t * event) {
    FilesApp * app = static_cast<FilesApp *>(lv_event_get_user_data(event));
    if(!app) return;
    if(app->scanner_) app->scanner_->cancel();
    app->showRoots();
}

void FilesApp::confirmDeleteEvent(lv_event_t * event) {
    FilesApp * app = static_cast<FilesApp *>(lv_event_get_user_data(event));
    if(app) app->deletePending();
}

void FilesApp::cancelDeleteEvent(lv_event_t * event) {
    FilesApp * app = static_cast<FilesApp *>(lv_event_get_user_data(event));
    if(app) app->closeConfirmation();
}

}  // namespace firefly
