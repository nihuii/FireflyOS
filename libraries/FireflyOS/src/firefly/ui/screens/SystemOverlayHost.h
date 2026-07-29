#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace firefly {

enum class PairingDecision : uint8_t {
    Allow,
    Deny,
    DismissResult,
    ConfirmUnpair,
    CancelUnpair
};

class PairingOverlay {
public:
    using DecisionCallback = void (*)(PairingDecision);

    bool create(lv_obj_t * parent);
    void setDecisionCallback(DecisionCallback callback) { callback_ = callback; }
    void showRequest(const char * phone_name, uint32_t passkey);
    void showSecuring(const char * phone_name);
    void showResult(bool success, const char * phone_name);
    void showUnpairConfirmation(const char * phone_name);
    void hide();
    lv_obj_t * root() const { return root_; }

private:
    enum class View : uint8_t { Request, Securing, Result, Unpair };
    static void primaryClicked(lv_event_t * event);
    static void secondaryClicked(lv_event_t * event);
    void setText(const char * title,
                 const char * status,
                 const char * detail,
                 const char * primary,
                 const char * secondary);
    void emitPrimary();
    void emitSecondary();

    DecisionCallback callback_ = nullptr;
    View view_ = View::Request;
    bool result_success_ = false;
    lv_obj_t * root_ = nullptr;
    lv_obj_t * status_label_ = nullptr;
    lv_obj_t * title_label_ = nullptr;
    lv_obj_t * device_label_ = nullptr;
    lv_obj_t * code_label_ = nullptr;
    lv_obj_t * detail_label_ = nullptr;
    lv_obj_t * primary_button_ = nullptr;
    lv_obj_t * primary_label_ = nullptr;
    lv_obj_t * secondary_button_ = nullptr;
    lv_obj_t * secondary_label_ = nullptr;
};

class SystemOverlayHost {
public:
    static constexpr uint8_t kPairingPriority = 3;
    static constexpr uint8_t kAlarmPriority = 4;
    static bool acceptsPriority(uint8_t current, uint8_t incoming) {
        return incoming >= 1 && incoming <= 5 && incoming >= current;
    }
    bool attach(lv_obj_t * host);
    bool show(uint8_t priority, lv_obj_t * overlay);
    void close(lv_obj_t * overlay);
    void clear();

    uint8_t priority() const { return priority_; }
    lv_obj_t * current() const { return current_; }

private:
    void activateHighest();
    lv_obj_t * host_ = nullptr;
    lv_obj_t * current_ = nullptr;
    lv_obj_t * slots_[6]{};
    uint8_t priority_ = 0;
};

}  // namespace firefly
