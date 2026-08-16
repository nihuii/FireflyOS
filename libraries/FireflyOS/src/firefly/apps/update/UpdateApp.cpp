#include "UpdateApp.h"

#include <stdio.h>

#include "../../ui/UiTheme.h"

namespace firefly {

bool UpdateApp::create(lv_obj_t * parent, UiComponents & components) {
    LV_UNUSED(components);
    if(!parent) return false;
    const UiTokens tokens = UiTheme::fireflyDefault();
    root_ = UiComponents::createPage(parent, tokens);
    lv_obj_set_size(root_, 410, 502);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * page_title = lv_label_create(root_);
    lv_label_set_text(page_title, "System Update");
    lv_obj_set_style_text_font(page_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(page_title,
                                lv_color_hex(tokens.text_primary), 0);
    lv_obj_set_pos(page_title, 28, 42);

    icon_label_ = lv_label_create(root_);
    lv_obj_set_size(icon_label_, 56, 56);
    lv_obj_set_style_radius(icon_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(icon_label_, 2, 0);
    lv_obj_set_style_border_color(icon_label_,
                                  lv_color_hex(tokens.firefly_primary), 0);
    lv_obj_set_style_text_color(icon_label_,
                                lv_color_hex(tokens.firefly_primary), 0);
    lv_obj_set_style_text_align(icon_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(icon_label_, 18, 0);
    lv_label_set_text(icon_label_, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_pos(icon_label_, 177, 92);

    state_label_ = lv_label_create(root_);
    lv_obj_set_width(state_label_, 350);
    lv_obj_set_style_text_align(state_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(state_label_,
                                lv_color_hex(tokens.text_primary), 0);
    lv_label_set_text(state_label_, "No update selected");
    lv_obj_set_pos(state_label_, 30, 164);

    detail_label_ = lv_label_create(root_);
    lv_obj_set_width(detail_label_, 350);
    lv_label_set_long_mode(detail_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(detail_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(detail_label_,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(detail_label_, "Check the managed update source.");
    lv_obj_set_pos(detail_label_, 30, 204);

    progress_bar_ = lv_bar_create(root_);
    lv_obj_set_size(progress_bar_, 350, 10);
    lv_obj_set_pos(progress_bar_, 30, 258);
    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar_,
                              lv_color_hex(tokens.bg_surface), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar_,
                              lv_color_hex(tokens.firefly_primary),
                              LV_PART_INDICATOR);

    percent_label_ = lv_label_create(root_);
    lv_obj_set_width(percent_label_, 350);
    lv_obj_set_style_text_align(percent_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(percent_label_,
                                lv_color_hex(tokens.firefly_primary), 0);
    lv_label_set_text(percent_label_, "0%");
    lv_obj_set_pos(percent_label_, 30, 278);

    static const char * const stages[] = {
        "DOWNLOAD", "VERIFY", "WRITE", "CHECK"
    };
    for(uint8_t index = 0; index < 4; ++index) {
        lv_obj_t * stage = lv_label_create(root_);
        lv_obj_set_width(stage, 82);
        lv_obj_set_style_text_align(stage, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(stage,
                                    lv_color_hex(tokens.text_secondary), 0);
        lv_label_set_text(stage, stages[index]);
        lv_obj_set_pos(stage, 31 + index * 87, 314);
    }

    primary_button_ = lv_btn_create(root_);
    lv_obj_set_size(primary_button_, 350, 52);
    lv_obj_set_pos(primary_button_, 30, 370);
    lv_obj_set_style_radius(primary_button_, 18, 0);
    lv_obj_set_style_bg_color(primary_button_,
                              lv_color_hex(tokens.firefly_primary), 0);
    lv_obj_add_event_cb(primary_button_, primaryEvent,
                        LV_EVENT_CLICKED, this);
    primary_label_ = lv_label_create(primary_button_);
    lv_obj_set_style_text_color(primary_label_,
                                lv_color_hex(tokens.bg_base), 0);
    lv_label_set_text(primary_label_, "Install Update");
    lv_obj_center(primary_label_);

    secondary_button_ = lv_btn_create(root_);
    lv_obj_set_size(secondary_button_, 350, 48);
    lv_obj_set_pos(secondary_button_, 30, 430);
    lv_obj_set_style_radius(secondary_button_, 18, 0);
    lv_obj_set_style_bg_opa(secondary_button_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(secondary_button_, 1, 0);
    lv_obj_set_style_border_color(secondary_button_,
                                  lv_color_hex(tokens.text_secondary), 0);
    lv_obj_add_event_cb(secondary_button_, secondaryEvent,
                        LV_EVENT_CLICKED, this);
    secondary_label_ = lv_label_create(secondary_button_);
    lv_obj_set_style_text_color(secondary_label_,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(secondary_label_, "Diagnostics");
    lv_obj_center(secondary_label_);
    return true;
}

void UpdateApp::destroy() {
    hide();
    if(root_) lv_obj_del(root_);
    root_ = nullptr;
    icon_label_ = nullptr;
    state_label_ = nullptr;
    detail_label_ = nullptr;
    progress_bar_ = nullptr;
    percent_label_ = nullptr;
    primary_button_ = nullptr;
    primary_label_ = nullptr;
    secondary_button_ = nullptr;
    secondary_label_ = nullptr;
}

void UpdateApp::show() {
    visible_ = true;
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void UpdateApp::hide() {
    visible_ = false;
    if(icon_label_) {
        lv_anim_del(icon_label_, nullptr);
        lv_obj_set_style_transform_angle(icon_label_, 0, 0);
    }
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

const char * UpdateApp::failureText(UpdateFailure failure) {
    switch(failure) {
        case UpdateFailure::LowPower: return "Charge the watch or reach 40% battery.";
        case UpdateFailure::AlarmActive: return "Dismiss the active alarm first.";
        case UpdateFailure::AudioBusy: return "Stop music or recording first.";
        case UpdateFailure::TransferBusy: return "Finish the current file transfer first.";
        case UpdateFailure::OtaBusy: return "Another update session is already active.";
        case UpdateFailure::WrongProduct: return "This package is for another product.";
        case UpdateFailure::BuildNotNewer: return "The package is not newer than this build.";
        case UpdateFailure::MinBuildMismatch: return "This build cannot upgrade directly.";
        case UpdateFailure::PackageTooLarge: return "The package exceeds the OTA slot.";
        case UpdateFailure::NoHttpsEndpoint: return "No trusted update endpoint is configured.";
        case UpdateFailure::SignatureInvalid: return "Package signature is not trusted.";
        case UpdateFailure::HashMismatch: return "Package integrity check failed.";
        case UpdateFailure::Cancelled: return "Update cancelled without changing the boot slot.";
        case UpdateFailure::BootValidationFailed: return "Startup checks requested rollback.";
        case UpdateFailure::Timeout: return "The update source timed out.";
        default: return "Update could not continue. Open diagnostics for details.";
    }
}

void UpdateApp::refresh(const UpdateSnapshot & snapshot) {
    if(!root_) return;
    const char * title = "No update selected";
    const char * detail = "Check the managed update source.";
    const char * icon = LV_SYMBOL_DOWNLOAD;
    bool animate = false;
    bool primary = false;
    bool secondary = true;
    secondary_is_cancel_ = false;

    switch(snapshot.state) {
        case UpdateState::Idle:
            break;
        case UpdateState::Available:
            title = "Update Available";
            detail = "Signed package is ready. Installation will use the spare slot.";
            primary = true;
            secondary = true;
            break;
        case UpdateState::Blocked:
            title = "Update Blocked";
            detail = failureText(snapshot.failure);
            icon = LV_SYMBOL_WARNING;
            break;
        case UpdateState::Downloading:
            title = "Installing Update";
            detail = "Streaming a verified package to the spare system slot.";
            animate = true;
            secondary_is_cancel_ = snapshot.cancel_allowed;
            secondary = snapshot.cancel_allowed;
            break;
        case UpdateState::Verifying:
            title = "Verifying Package";
            detail = "Checking exact length and SHA-256 integrity.";
            icon = LV_SYMBOL_REFRESH;
            animate = true;
            secondary_is_cancel_ = snapshot.cancel_allowed;
            secondary = snapshot.cancel_allowed;
            break;
        case UpdateState::Writing:
            title = "Writing System";
            detail = "SAM guard active. Do not power off; this step cannot be cancelled.";
            icon = LV_SYMBOL_SAVE;
            secondary = false;
            break;
        case UpdateState::RebootPending:
            title = "Ready to Restart";
            detail = "The spare slot is selected. Restart to run startup checks.";
            icon = LV_SYMBOL_POWER;
            secondary = false;
            break;
        case UpdateState::BootChecking:
            title = "Checking New System";
            detail = "RTC, PMU, display, touch, NVS and main UI are being checked.";
            icon = LV_SYMBOL_REFRESH;
            animate = true;
            secondary = false;
            break;
        case UpdateState::Completed:
            title = "Update Complete";
            detail = "The new system passed all startup checks.";
            icon = LV_SYMBOL_OK;
            secondary = false;
            break;
        case UpdateState::Failed:
            title = "Update Failed";
            detail = failureText(snapshot.failure);
            icon = LV_SYMBOL_WARNING;
            break;
        case UpdateState::RollbackRequested:
            title = "Restoring Previous System";
            detail = "Startup validation failed. Rollback has been requested.";
            icon = LV_SYMBOL_REFRESH;
            animate = true;
            secondary = false;
            break;
        case UpdateState::RolledBack:
            title = "Previous System Restored";
            detail = "The earlier working build remains active.";
            icon = LV_SYMBOL_OK;
            break;
    }

    lv_label_set_text(icon_label_, icon);
    lv_label_set_text(state_label_, title);
    lv_label_set_text(detail_label_, detail);
    lv_bar_set_value(progress_bar_, snapshot.progress_percent, LV_ANIM_OFF);
    char percent[8]{};
    snprintf(percent, sizeof(percent), "%u%%", snapshot.progress_percent);
    lv_label_set_text(percent_label_, percent);
    lv_label_set_text(primary_label_, "Install Update");
    lv_label_set_text(secondary_label_,
                      secondary_is_cancel_ ? "Cancel" : "Diagnostics");
    setButtonVisible(primary_button_, primary);
    setButtonVisible(secondary_button_, secondary);
    setAnimation(visible_ && animate);
}

void UpdateApp::setAnimation(bool active) {
    if(!icon_label_) return;
    lv_anim_del(icon_label_, nullptr);
    lv_obj_set_style_transform_angle(icon_label_, 0, 0);
    if(!active) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, icon_label_);
    lv_anim_set_exec_cb(&animation, animateIcon);
    lv_anim_set_values(&animation, 0, 3600);
    lv_anim_set_time(&animation, 1400);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

void UpdateApp::setButtonVisible(lv_obj_t * button, bool visible) {
    if(!button) return;
    if(visible) lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
}

void UpdateApp::animateIcon(void * object, int32_t angle) {
    lv_obj_set_style_transform_angle(static_cast<lv_obj_t *>(object), angle, 0);
}

void UpdateApp::primaryEvent(lv_event_t * event) {
    auto * self = static_cast<UpdateApp *>(lv_event_get_user_data(event));
    if(self && self->visible_ && self->start_callback_) {
        self->start_callback_(self->start_context_);
    }
}

void UpdateApp::secondaryEvent(lv_event_t * event) {
    auto * self = static_cast<UpdateApp *>(lv_event_get_user_data(event));
    if(!self || !self->visible_) return;
    if(self->secondary_is_cancel_) {
        if(self->cancel_callback_) self->cancel_callback_(self->cancel_context_);
    } else if(self->diagnostics_callback_) {
        self->diagnostics_callback_(self->diagnostics_context_);
    }
}

}  // namespace firefly
