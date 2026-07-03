#include "ToolsApp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ui/UiTheme.h"

namespace firefly {
namespace {

bool allowedCalculatorChar(char c) {
    return (c >= '0' && c <= '9') ||
           c == '+' || c == '-' || c == '*' || c == '/' ||
           c == '.' || c == ' ';
}

void trimTrailingZeroes(char * text) {
    if(!text) return;
    char * dot = strchr(text, '.');
    if(!dot) return;
    char * end = text + strlen(text);
    while(end > dot + 1 && *(end - 1) == '0') {
        --end;
        *end = '\0';
    }
    if(end > dot && *(end - 1) == '.') {
        *(end - 1) = '\0';
    }
}

}  // namespace

CalculatorEngine::CalculatorEngine() {
    clear();
}

bool CalculatorEngine::setExpression(const char * expression) {
    if(!expression) return false;
    const size_t length = strlen(expression);
    if(length == 0 || length > kMaxExpressionLength) {
        return false;
    }
    for(size_t i = 0; i < length; ++i) {
        if(!allowedCalculatorChar(expression[i])) {
            return false;
        }
    }
    strlcpy(expression_, expression, sizeof(expression_));
    strlcpy(display_, expression_, sizeof(display_));
    return true;
}

bool CalculatorEngine::inputKey(const char * key) {
    if(!key) return false;
    if(strcmp(key, "C") == 0) {
        clear();
        return true;
    }
    if(strcmp(key, "=") == 0) {
        if(expression_[0] == '\0') return false;
        const bool evaluated = evaluate();
        if(evaluated) {
            strlcpy(expression_, display_, sizeof(expression_));
        }
        return evaluated;
    }
    if(key[0] == '\0' || key[1] != '\0' || !allowedCalculatorChar(key[0])) {
        return false;
    }

    const size_t length = strlen(expression_);
    if(length >= kMaxExpressionLength) return false;
    if(length == 0 && key[0] != '-' && (key[0] < '0' || key[0] > '9')) {
        return false;
    }
    expression_[length] = key[0];
    expression_[length + 1] = '\0';
    strlcpy(display_, expression_, sizeof(display_));
    return true;
}

bool CalculatorEngine::evaluate() {
    char * first_end = nullptr;
    const double lhs = strtod(expression_, &first_end);
    if(first_end == expression_) {
        return setError("Error");
    }

    while(*first_end == ' ') ++first_end;
    const char op = *first_end;
    if(op == '\0') {
        formatResult(lhs);
        return true;
    }
    if(op != '+' && op != '-' && op != '*' && op != '/') {
        return setError("Error");
    }

    const char * rhs_start = first_end + 1;
    while(*rhs_start == ' ') ++rhs_start;
    char * second_end = nullptr;
    const double rhs = strtod(rhs_start, &second_end);
    if(second_end == rhs_start) {
        return setError("Error");
    }
    while(*second_end == ' ') ++second_end;
    if(*second_end != '\0') {
        return setError("Error");
    }

    double result = 0.0;
    switch(op) {
        case '+': result = lhs + rhs; break;
        case '-': result = lhs - rhs; break;
        case '*': result = lhs * rhs; break;
        case '/':
            if(fabs(rhs) < 0.000001) {
                return setError("Divide by zero");
            }
            result = lhs / rhs;
            break;
        default:
            return setError("Error");
    }

    formatResult(result);
    return true;
}

void CalculatorEngine::clear() {
    expression_[0] = '\0';
    strlcpy(display_, "0", sizeof(display_));
}

bool CalculatorEngine::setError(const char * text) {
    strlcpy(display_, text ? text : "Error", sizeof(display_));
    return false;
}

void CalculatorEngine::formatResult(double value) {
    if(isnan(value) || isinf(value)) {
        setError("Error");
        return;
    }
    snprintf(display_, sizeof(display_), "%.6f", value);
    trimTrailingZeroes(display_);
    if(strlen(display_) > kMaxDisplayChars) {
        snprintf(display_, sizeof(display_), "%.6g", value);
    }
    if(strlen(display_) > kMaxDisplayChars) {
        display_[kMaxDisplayChars] = '\0';
    }
}

bool ToolsCommandQueue::post(const ToolsCommand & command) {
    if(command.type == ToolsCommandType::None || count_ >= kCapacity) {
        return false;
    }
    commands_[tail_] = command;
    tail_ = static_cast<uint8_t>((tail_ + 1U) % kCapacity);
    ++count_;
    return true;
}

bool ToolsCommandQueue::take(ToolsCommand & command) {
    if(count_ == 0) {
        command = {};
        return false;
    }
    command = commands_[head_];
    head_ = static_cast<uint8_t>((head_ + 1U) % kCapacity);
    --count_;
    return true;
}

bool FlashlightSession::canStart(const FlashlightPowerState & state) const {
    if(!state.valid) return false;
    if(state.battery_percent < kMinBatteryPercent) return false;
    return state.temperature_c >= kMinSafeTemperatureC &&
           state.temperature_c <= kMaxSafeTemperatureC;
}

bool FlashlightSession::start(const FlashlightPowerState & state,
                              uint32_t now_ms,
                              uint8_t original_brightness) {
    if(!state.valid) {
        last_denial_ = "power-state-invalid";
        return false;
    }
    if(state.battery_percent < kMinBatteryPercent) {
        last_denial_ = "battery-low";
        return false;
    }
    if(state.temperature_c < kMinSafeTemperatureC ||
       state.temperature_c > kMaxSafeTemperatureC) {
        last_denial_ = "temperature-unsafe";
        return false;
    }
    active_ = true;
    started_at_ms_ = now_ms;
    original_brightness_ = original_brightness;
    last_denial_ = "";
    return true;
}

bool FlashlightSession::active(uint32_t now_ms) const {
    return active_ && !shouldStop(now_ms);
}

bool FlashlightSession::shouldStop(uint32_t now_ms) const {
    return active_ && (now_ms - started_at_ms_ >= kMaxDurationMs);
}

void FlashlightSession::stop() {
    active_ = false;
}

void FlashlightSession::closeFromUser() {
    stop();
}

bool FlashlightController::start(const FlashlightPowerState & state,
                                 uint32_t now_ms,
                                 uint8_t original_brightness) {
    if(session_.running()) return false;
    if(!session_.start(state, now_ms, original_brightness)) return false;
    if(!queue_.post({ToolsCommandType::SetBrightness, 255})) {
        session_.stop();
        return false;
    }
    return true;
}

bool FlashlightController::tick(uint32_t now_ms) {
    if(!session_.shouldStop(now_ms)) return false;
    return stop();
}

bool FlashlightController::stop() {
    if(!session_.running()) return false;
    const uint8_t original_brightness = session_.originalBrightness();
    session_.stop();
    return queue_.post({ToolsCommandType::SetBrightness, original_brightness});
}

bool ToolsApp::create(lv_obj_t * parent, UiComponents & components) {
    LV_UNUSED(components);
    if(!parent) return false;

    const UiTokens tokens = UiTheme::fireflyDefault();
    root_ = UiComponents::createPage(parent, tokens);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(root_);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(tokens.text_primary), 0);
    lv_label_set_text(title, "Calculator");
    lv_obj_set_pos(title, 28, 48);

    lv_obj_t * subtitle = lv_label_create(root_);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(subtitle, "Fixed buffer | basic math");
    lv_obj_set_pos(subtitle, 30, 80);

    lv_obj_t * calculator_card = UiComponents::createCard(root_, tokens);
    lv_obj_set_size(calculator_card, 354, 296);
    lv_obj_set_pos(calculator_card, 28, 108);
    lv_obj_clear_flag(calculator_card, LV_OBJ_FLAG_SCROLLABLE);
    calculator_display_label_ = lv_label_create(calculator_card);
    lv_obj_set_width(calculator_display_label_, 322);
    lv_obj_set_style_text_align(calculator_display_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(calculator_display_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(calculator_display_label_,
                                lv_color_hex(tokens.text_primary), 0);
    lv_label_set_text(calculator_display_label_, calculator_.display());
    lv_obj_set_pos(calculator_display_label_, 8, 10);

    static const char * keys[] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "C", "0", ".", "+",
        "="
    };
    for(uint8_t i = 0; i < 17; ++i) {
        lv_obj_t * key = lv_btn_create(calculator_card);
        const bool equals = i == 16;
        lv_obj_set_size(key, equals ? 332 : 80, 46);
        lv_obj_set_pos(key,
                       8 + (equals ? 0 : (i % 4U) * 84),
                       50 + (equals ? 4 : (i / 4U)) * 49);
        lv_obj_set_ext_click_area(key, 1);
        lv_obj_set_style_radius(key, 12, 0);
        lv_obj_set_style_bg_color(
            key,
            lv_color_hex(equals ? tokens.firefly_primary : tokens.bg_surface),
            0);
        lv_obj_set_style_bg_opa(key, equals ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_obj_add_event_cb(key, calculatorKeyEvent, LV_EVENT_CLICKED, this);
        lv_obj_t * label = lv_label_create(key);
        lv_label_set_text(label, keys[i]);
        if(equals) {
            lv_obj_set_style_text_color(label, lv_color_hex(tokens.bg_base), 0);
        }
        lv_obj_center(label);
    }

    lv_obj_t * flashlight_button = lv_btn_create(root_);
    lv_obj_set_size(flashlight_button, 354, 56);
    lv_obj_set_pos(flashlight_button, 28, 414);
    lv_obj_set_style_radius(flashlight_button, 20, 0);
    lv_obj_set_style_bg_color(flashlight_button,
                              lv_color_hex(tokens.bg_surface), 0);
    lv_obj_set_style_bg_opa(flashlight_button, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(flashlight_button, flashlightStartEvent,
                        LV_EVENT_CLICKED, this);

    flashlight_label_ = lv_label_create(flashlight_button);
    lv_obj_set_width(flashlight_label_, 310);
    lv_obj_set_style_text_align(flashlight_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(flashlight_label_,
                                lv_color_hex(tokens.firefly_primary), 0);
    lv_label_set_text(flashlight_label_, "Screen flashlight | Check power");
    lv_obj_center(flashlight_label_);

    battery_label_ = lv_label_create(root_);
    lv_obj_set_width(battery_label_, 180);
    lv_obj_set_style_text_align(battery_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(battery_label_,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(battery_label_, "Power unknown");
    lv_obj_set_pos(battery_label_, 196, 54);

    flashlight_overlay_ = lv_obj_create(root_);
    lv_obj_set_size(flashlight_overlay_, 410, 502);
    lv_obj_set_pos(flashlight_overlay_, 0, 0);
    lv_obj_set_style_bg_color(flashlight_overlay_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(flashlight_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(flashlight_overlay_, 0, 0);
    lv_obj_set_style_radius(flashlight_overlay_, 0, 0);
    lv_obj_clear_flag(flashlight_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(flashlight_overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(flashlight_overlay_, flashlightCloseEvent,
                        LV_EVENT_CLICKED, this);

    lv_obj_t * close_hint = lv_label_create(flashlight_overlay_);
    lv_obj_set_width(close_hint, 350);
    lv_obj_set_style_text_align(close_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(close_hint, lv_color_hex(0x506066), 0);
    lv_label_set_text(close_hint, "Tap / BOOT / PWR to close\n60s max");
    lv_obj_align(close_hint, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_add_flag(flashlight_overlay_, LV_OBJ_FLAG_HIDDEN);

    return true;
}

void ToolsApp::destroy() {
    if(root_) {
        lv_obj_del(root_);
        root_ = nullptr;
    }
    calculator_display_label_ = nullptr;
    battery_label_ = nullptr;
    flashlight_label_ = nullptr;
    flashlight_overlay_ = nullptr;
}

void ToolsApp::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ToolsApp::hide() {
    closeFlashlightFromInput();
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ToolsApp::refresh(const SystemState & state,
                       uint8_t current_brightness,
                       uint32_t now_ms) {
    current_state_ = state;
    current_brightness_ = current_brightness;
    tick(now_ms);
    if(calculator_display_label_) {
        lv_label_set_text(calculator_display_label_, calculator_.display());
    }
    refreshStatusLabels(state);
}

void ToolsApp::tick(uint32_t now_ms) {
    if(flashlight_controller_.tick(now_ms)) {
        setFlashlightOverlayVisible(false);
    }
}

bool ToolsApp::takeCommand(ToolsCommand & command) {
    return flashlight_controller_.takeCommand(command);
}

bool ToolsApp::closeFlashlightFromInput() {
    const bool stopped = flashlight_controller_.stop();
    if(stopped) setFlashlightOverlayVisible(false);
    return stopped;
}

void ToolsApp::calculatorKeyEvent(lv_event_t * event) {
    ToolsApp * app = static_cast<ToolsApp *>(lv_event_get_user_data(event));
    if(app) app->handleCalculatorKey(lv_event_get_target(event));
}

void ToolsApp::flashlightStartEvent(lv_event_t * event) {
    ToolsApp * app = static_cast<ToolsApp *>(lv_event_get_user_data(event));
    if(app) app->startFlashlight();
}

void ToolsApp::flashlightCloseEvent(lv_event_t * event) {
    ToolsApp * app = static_cast<ToolsApp *>(lv_event_get_user_data(event));
    if(app) app->closeFlashlightFromInput();
}

void ToolsApp::handleCalculatorKey(lv_obj_t * button) {
    if(!button) return;
    lv_obj_t * label = lv_obj_get_child(button, 0);
    const char * key = label ? lv_label_get_text(label) : nullptr;
    calculator_.inputKey(key);
    if(calculator_display_label_) {
        lv_label_set_text(calculator_display_label_, calculator_.display());
    }
}

void ToolsApp::startFlashlight() {
    const FlashlightPowerState power_state(
        current_state_.battery.percent < 0
            ? 0
            : static_cast<uint8_t>(current_state_.battery.percent),
        static_cast<int8_t>(current_state_.battery.temperature_c),
        current_state_.battery.valid);
    if(flashlight_controller_.start(power_state, lv_tick_get(),
                                    current_brightness_)) {
        setFlashlightOverlayVisible(true);
    }
    refreshStatusLabels(current_state_);
}

void ToolsApp::setFlashlightOverlayVisible(bool visible) {
    if(!flashlight_overlay_) return;
    if(visible) {
        lv_obj_clear_flag(flashlight_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(flashlight_overlay_);
    } else {
        lv_obj_add_flag(flashlight_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ToolsApp::refreshStatusLabels(const SystemState & state) {
    if(!battery_label_ || !flashlight_label_) return;

    if(!state.battery.valid) {
        lv_label_set_text(battery_label_, "Power unknown");
    } else {
        char battery[48];
        snprintf(battery, sizeof(battery), "BAT %d%% | %dC",
                 static_cast<int>(state.battery.percent),
                 static_cast<int>(state.battery.temperature_c));
        lv_label_set_text(battery_label_, battery);
    }

    const FlashlightPowerState power_state(
        state.battery.percent < 0 ? 0 : static_cast<uint8_t>(state.battery.percent),
        static_cast<int8_t>(state.battery.temperature_c),
        state.battery.valid);
    if(flashlight_controller_.session().running()) {
        lv_label_set_text(flashlight_label_, "Flashlight on | Tap white screen");
    } else {
        lv_label_set_text(flashlight_label_,
                          flashlight_controller_.session().canStart(power_state)
                              ? "Screen flashlight | Tap (60s max)"
                              : "Flashlight locked | Check power");
    }
}

}  // namespace firefly
