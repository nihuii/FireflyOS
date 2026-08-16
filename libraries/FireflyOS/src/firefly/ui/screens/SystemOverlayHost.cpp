#include "SystemOverlayHost.h"

#include <stdio.h>

namespace firefly {

bool PairingOverlay::create(lv_obj_t * parent) {
    if(!parent || root_) return false;
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, 410, 502);
    lv_obj_align(root_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x020607), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    status_label_ = lv_label_create(root_);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0x62E8CA), 0);
    lv_label_set_text(status_label_, "BLE REQUEST");
    lv_obj_align(status_label_, LV_ALIGN_TOP_RIGHT, -30, 22);

    title_label_ = lv_label_create(root_);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(0xEFFFFB), 0);
    lv_label_set_text(title_label_, "Pair new phone");
    lv_obj_align(title_label_, LV_ALIGN_TOP_LEFT, 28, 56);

    device_label_ = lv_label_create(root_);
    lv_obj_set_width(device_label_, 354);
    lv_obj_set_style_bg_color(device_label_, lv_color_hex(0x0D171D), 0);
    lv_obj_set_style_bg_opa(device_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(device_label_, lv_color_hex(0x25454B), 0);
    lv_obj_set_style_border_width(device_label_, 1, 0);
    lv_obj_set_style_radius(device_label_, 17, 0);
    lv_obj_set_style_pad_all(device_label_, 14, 0);
    lv_obj_set_style_text_color(device_label_, lv_color_hex(0xEFFFFB), 0);
    lv_obj_align(device_label_, LV_ALIGN_TOP_MID, 0, 105);

    code_label_ = lv_label_create(root_);
    lv_obj_set_width(code_label_, 354);
    lv_obj_set_style_text_font(code_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(code_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(code_label_, lv_color_hex(0xDCFFF7), 0);
    lv_obj_set_style_bg_color(code_label_, lv_color_hex(0x10272A), 0);
    lv_obj_set_style_bg_opa(code_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(code_label_, lv_color_hex(0x3A756F), 0);
    lv_obj_set_style_border_width(code_label_, 1, 0);
    lv_obj_set_style_radius(code_label_, 22, 0);
    lv_obj_set_style_pad_top(code_label_, 14, 0);
    lv_obj_set_style_pad_bottom(code_label_, 14, 0);
    lv_label_set_text(code_label_, "000 000");
    lv_obj_align(code_label_, LV_ALIGN_TOP_MID, 0, 181);

    detail_label_ = lv_label_create(root_);
    lv_obj_set_width(detail_label_, 340);
    lv_label_set_long_mode(detail_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(detail_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(detail_label_, lv_color_hex(0x8BA6AA), 0);
    lv_obj_align(detail_label_, LV_ALIGN_TOP_MID, 0, 290);

    secondary_button_ = lv_btn_create(root_);
    lv_obj_set_size(secondary_button_, 170, 52);
    lv_obj_align(secondary_button_, LV_ALIGN_BOTTOM_LEFT, 28, -28);
    lv_obj_set_style_radius(secondary_button_, 17, 0);
    lv_obj_set_style_bg_color(secondary_button_, lv_color_hex(0x101B20), 0);
    lv_obj_set_style_border_color(secondary_button_, lv_color_hex(0x2E4A50), 0);
    lv_obj_set_style_border_width(secondary_button_, 1, 0);
    secondary_label_ = lv_label_create(secondary_button_);
    lv_obj_set_style_text_color(secondary_label_, lv_color_hex(0xC2D3D5), 0);
    lv_obj_center(secondary_label_);
    lv_obj_add_event_cb(secondary_button_, secondaryClicked,
                        LV_EVENT_CLICKED, this);

    primary_button_ = lv_btn_create(root_);
    lv_obj_set_size(primary_button_, 170, 52);
    lv_obj_align(primary_button_, LV_ALIGN_BOTTOM_RIGHT, -28, -28);
    lv_obj_set_style_radius(primary_button_, 17, 0);
    lv_obj_set_style_bg_color(primary_button_, lv_color_hex(0x153C39), 0);
    lv_obj_set_style_border_color(primary_button_, lv_color_hex(0x3A756F), 0);
    lv_obj_set_style_border_width(primary_button_, 1, 0);
    primary_label_ = lv_label_create(primary_button_);
    lv_obj_set_style_text_color(primary_label_, lv_color_hex(0xDCFFF7), 0);
    lv_obj_center(primary_label_);
    lv_obj_add_event_cb(primary_button_, primaryClicked,
                        LV_EVENT_CLICKED, this);
    hide();
    return true;
}

void PairingOverlay::showRequest(const char * phone_name, uint32_t passkey) {
    if(!root_) return;
    view_ = View::Request;
    result_success_ = false;
    char device[48];
    snprintf(device, sizeof(device), "Phone  %s", phone_name ? phone_name : "Unknown");
    lv_label_set_text(device_label_, device);
    char code[16];
    snprintf(code, sizeof(code), "%03lu %03lu",
             static_cast<unsigned long>(passkey / 1000U),
             static_cast<unsigned long>(passkey % 1000U));
    lv_label_set_text(code_label_, code);
    lv_obj_clear_flag(code_label_, LV_OBJ_FLAG_HIDDEN);
    setText("Pair new phone", "BLE REQUEST",
            "Confirm the same code on your phone. Never share this code.",
            "Allow", "Deny");
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void PairingOverlay::showSecuring(const char * phone_name) {
    if(!root_) return;
    view_ = View::Securing;
    char device[48];
    snprintf(device, sizeof(device), "Phone  %s", phone_name ? phone_name : "Unknown");
    lv_label_set_text(device_label_, device);
    lv_obj_add_flag(code_label_, LV_OBJ_FLAG_HIDDEN);
    setText("Securing link", "VERIFYING",
            "Keep both devices nearby while the encrypted bond is created.",
            "Please wait", nullptr);
    lv_obj_add_state(primary_button_, LV_STATE_DISABLED);
}

void PairingOverlay::showResult(bool success, const char * phone_name) {
    if(!root_) return;
    view_ = View::Result;
    result_success_ = success;
    char device[48];
    snprintf(device, sizeof(device), "Phone  %s", phone_name ? phone_name : "Unknown");
    lv_label_set_text(device_label_, device);
    lv_obj_add_flag(code_label_, LV_OBJ_FLAG_HIDDEN);
    setText(success ? "Phone linked" : "Pairing failed",
            success ? "SECURE" : "NOT LINKED",
            success
                ? "Sync is optional. FireflyOS keeps working when the phone is away."
                : "No phone data was saved. Check both devices and try again.",
            success ? "Done" : "Try again", nullptr);
}

void PairingOverlay::showUnpairConfirmation(const char * phone_name) {
    if(!root_) return;
    view_ = View::Unpair;
    result_success_ = false;
    char device[48];
    snprintf(device, sizeof(device), "Phone  %s", phone_name ? phone_name : "Unknown");
    lv_label_set_text(device_label_, device);
    lv_obj_add_flag(code_label_, LV_OBJ_FLAG_HIDDEN);
    setText("Unlink phone?", "CONFIRM",
            "Clears bond, token, phone name and notifications. Alarms, media and activity stay.",
            "Unlink", "Keep linked");
    lv_obj_set_style_bg_color(primary_button_, lv_color_hex(0x361A1E), 0);
    lv_obj_set_style_border_color(primary_button_, lv_color_hex(0x713B40), 0);
}

void PairingOverlay::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void PairingOverlay::primaryClicked(lv_event_t * event) {
    PairingOverlay * self = static_cast<PairingOverlay *>(lv_event_get_user_data(event));
    if(self) self->emitPrimary();
}

void PairingOverlay::secondaryClicked(lv_event_t * event) {
    PairingOverlay * self = static_cast<PairingOverlay *>(lv_event_get_user_data(event));
    if(self) self->emitSecondary();
}

void PairingOverlay::setText(const char * title,
                             const char * status,
                             const char * detail,
                             const char * primary,
                             const char * secondary) {
    lv_label_set_text(title_label_, title);
    lv_label_set_text(status_label_, status);
    lv_label_set_text(detail_label_, detail);
    lv_label_set_text(primary_label_, primary ? primary : "");
    lv_obj_clear_state(primary_button_, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(primary_button_, lv_color_hex(0x153C39), 0);
    lv_obj_set_style_border_color(primary_button_, lv_color_hex(0x3A756F), 0);
    if(secondary) {
        lv_label_set_text(secondary_label_, secondary);
        lv_obj_clear_flag(secondary_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(primary_button_, 170);
        lv_obj_align(primary_button_, LV_ALIGN_BOTTOM_RIGHT, -28, -28);
    } else {
        lv_obj_add_flag(secondary_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(primary_button_, 354);
        lv_obj_align(primary_button_, LV_ALIGN_BOTTOM_MID, 0, -28);
    }
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void PairingOverlay::emitPrimary() {
    if(!callback_ || view_ == View::Securing) return;
    if(view_ == View::Request) callback_(PairingDecision::Allow);
    else if(view_ == View::Unpair) callback_(PairingDecision::ConfirmUnpair);
    else callback_(PairingDecision::DismissResult);
}

void PairingOverlay::emitSecondary() {
    if(!callback_) return;
    if(view_ == View::Request) callback_(PairingDecision::Deny);
    else if(view_ == View::Unpair) callback_(PairingDecision::CancelUnpair);
}

bool WifiProvisionOverlay::create(lv_obj_t * parent) {
    if(!parent || root_) return false;
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, 410, 502);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x020607), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    status_label_ = lv_label_create(root_);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0x62E8CA), 0);
    lv_label_set_text(status_label_, "SECURE BLE");
    lv_obj_align(status_label_, LV_ALIGN_TOP_RIGHT, -28, 24);

    title_label_ = lv_label_create(root_);
    lv_obj_set_width(title_label_, 354);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(0xEFFFFB), 0);
    lv_obj_align(title_label_, LV_ALIGN_TOP_MID, 0, 62);

    network_label_ = lv_label_create(root_);
    lv_obj_set_width(network_label_, 354);
    lv_label_set_long_mode(network_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_bg_color(network_label_, lv_color_hex(0x0D171D), 0);
    lv_obj_set_style_bg_opa(network_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(network_label_, 1, 0);
    lv_obj_set_style_border_color(network_label_, lv_color_hex(0x25454B), 0);
    lv_obj_set_style_radius(network_label_, 18, 0);
    lv_obj_set_style_pad_all(network_label_, 18, 0);
    lv_obj_set_style_text_color(network_label_, lv_color_hex(0xEFFFFB), 0);
    lv_obj_align(network_label_, LV_ALIGN_TOP_MID, 0, 132);

    detail_label_ = lv_label_create(root_);
    lv_obj_set_width(detail_label_, 340);
    lv_label_set_long_mode(detail_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(detail_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(detail_label_, lv_color_hex(0x8BA6AA), 0);
    lv_obj_align(detail_label_, LV_ALIGN_TOP_MID, 0, 234);

    secondary_button_ = lv_btn_create(root_);
    lv_obj_set_size(secondary_button_, 170, 52);
    lv_obj_align(secondary_button_, LV_ALIGN_BOTTOM_LEFT, 28, -28);
    lv_obj_set_style_radius(secondary_button_, 17, 0);
    lv_obj_set_style_bg_color(secondary_button_, lv_color_hex(0x101B20), 0);
    secondary_label_ = lv_label_create(secondary_button_);
    lv_obj_set_style_text_color(secondary_label_, lv_color_hex(0xC2D3D5), 0);
    lv_obj_center(secondary_label_);
    lv_obj_add_event_cb(secondary_button_, secondaryClicked,
                        LV_EVENT_CLICKED, this);

    primary_button_ = lv_btn_create(root_);
    lv_obj_set_size(primary_button_, 170, 52);
    lv_obj_align(primary_button_, LV_ALIGN_BOTTOM_RIGHT, -28, -28);
    lv_obj_set_style_radius(primary_button_, 17, 0);
    lv_obj_set_style_bg_color(primary_button_, lv_color_hex(0x153C39), 0);
    primary_label_ = lv_label_create(primary_button_);
    lv_obj_set_style_text_color(primary_label_, lv_color_hex(0xDCFFF7), 0);
    lv_obj_center(primary_label_);
    lv_obj_add_event_cb(primary_button_, primaryClicked,
                        LV_EVENT_CLICKED, this);
    hide();
    return true;
}

void WifiProvisionOverlay::showRequest(const char * ssid, bool forget) {
    if(!root_) return;
    view_ = View::Request;
    lv_label_set_text(status_label_, forget ? "FORGET NETWORK" : "WI-FI REQUEST");
    lv_label_set_text(title_label_, forget ? "Forget this network?" : "Connect to Wi-Fi?");
    lv_label_set_text(network_label_, ssid ? ssid : "Unknown network");
    lv_label_set_text(detail_label_, forget
        ? "This clears the saved credential. The phone must send it again to reconnect."
        : "Confirm only the network name. FireflyOS never shows or logs the password.");
    setButtons(forget ? "Forget" : "Connect", "Cancel");
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void WifiProvisionOverlay::showProgress(const char * ssid) {
    if(!root_) return;
    view_ = View::Progress;
    lv_label_set_text(status_label_, "CONNECTING");
    lv_label_set_text(title_label_, "Connecting to Wi-Fi");
    lv_label_set_text(network_label_, ssid ? ssid : "Network");
    lv_label_set_text(detail_label_, "Waiting up to 15 seconds. There is no automatic retry loop.");
    setButtons("Please wait", nullptr);
    lv_obj_add_state(primary_button_, LV_STATE_DISABLED);
}

void WifiProvisionOverlay::showResult(const char * title,
                                      const char * detail,
                                      bool success) {
    if(!root_) return;
    view_ = View::Result;
    lv_label_set_text(status_label_, success ? "DONE" : "NOT CONNECTED");
    lv_label_set_text(title_label_, title ? title : "Wi-Fi result");
    lv_label_set_text(detail_label_, detail ? detail : "No detail available.");
    setButtons("Done", nullptr);
}

void WifiProvisionOverlay::setButtons(const char * primary,
                                      const char * secondary) {
    lv_label_set_text(primary_label_, primary ? primary : "");
    lv_obj_clear_state(primary_button_, LV_STATE_DISABLED);
    if(secondary) {
        lv_label_set_text(secondary_label_, secondary);
        lv_obj_clear_flag(secondary_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(primary_button_, 170);
        lv_obj_align(primary_button_, LV_ALIGN_BOTTOM_RIGHT, -28, -28);
    } else {
        lv_obj_add_flag(secondary_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(primary_button_, 354);
        lv_obj_align(primary_button_, LV_ALIGN_BOTTOM_MID, 0, -28);
    }
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void WifiProvisionOverlay::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void WifiProvisionOverlay::primaryClicked(lv_event_t * event) {
    auto * self = static_cast<WifiProvisionOverlay *>(lv_event_get_user_data(event));
    if(!self || !self->callback_ || self->view_ == View::Progress) return;
    self->callback_(self->view_ == View::Request
        ? WifiProvisionDecision::Confirm : WifiProvisionDecision::Dismiss);
}

void WifiProvisionOverlay::secondaryClicked(lv_event_t * event) {
    auto * self = static_cast<WifiProvisionOverlay *>(lv_event_get_user_data(event));
    if(self && self->callback_ && self->view_ == View::Request) {
        self->callback_(WifiProvisionDecision::Deny);
    }
}

bool SystemOverlayHost::attach(lv_obj_t * host) {
    host_ = host;
    clear();
    return host_ != nullptr;
}

bool SystemOverlayHost::show(uint8_t priority, lv_obj_t * overlay) {
    if(!host_ || !overlay || priority < 1 || priority > 5) return false;
    if(slots_[priority] && slots_[priority] != overlay) {
        lv_obj_add_flag(slots_[priority], LV_OBJ_FLAG_HIDDEN);
    }
    slots_[priority] = overlay;
    if(!acceptsPriority(priority_, priority)) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    activateHighest();
    return true;
}

void SystemOverlayHost::close(lv_obj_t * overlay) {
    if(!overlay) return;
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    for(uint8_t i = 1; i <= 5; ++i) {
        if(slots_[i] == overlay) slots_[i] = nullptr;
    }
    activateHighest();
}

void SystemOverlayHost::clear() {
    if(current_) lv_obj_add_flag(current_, LV_OBJ_FLAG_HIDDEN);
    for(uint8_t i = 1; i <= 5; ++i) slots_[i] = nullptr;
    current_ = nullptr;
    priority_ = 0;
}

void SystemOverlayHost::activateHighest() {
    lv_obj_t * next = nullptr;
    uint8_t next_priority = 0;
    for(int8_t i = 5; i >= 1; --i) {
        if(slots_[i]) {
            next = slots_[i];
            next_priority = static_cast<uint8_t>(i);
            break;
        }
    }
    if(current_ && current_ != next) lv_obj_add_flag(current_, LV_OBJ_FLAG_HIDDEN);
    current_ = next;
    priority_ = next_priority;
    if(current_) {
        lv_obj_clear_flag(current_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(current_);
        lv_obj_move_foreground(host_);
    }
}

}  // namespace firefly
