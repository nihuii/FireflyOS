#pragma once

#include <stdint.h>
#include <lvgl.h>

#include "../../core/SystemState.h"
#include "../../ui/UiComponents.h"

namespace firefly {

class CalculatorEngine {
public:
    static constexpr uint8_t kMaxExpressionLength = 24;
    static constexpr uint8_t kMaxDisplayChars = 12;

    CalculatorEngine();

    bool setExpression(const char * expression);
    bool inputKey(const char * key);
    bool evaluate();
    void clear();
    const char * expression() const { return expression_; }
    const char * display() const { return display_; }

private:
    bool setError(const char * text);
    void formatResult(double value);

    char expression_[kMaxExpressionLength + 1]{};
    char display_[32]{};
};

enum class ToolsCommandType : uint8_t {
    None,
    SetBrightness
};

struct ToolsCommand {
    ToolsCommand() = default;
    ToolsCommand(ToolsCommandType command_type, uint8_t command_value)
        : type(command_type), value(command_value) {}

    ToolsCommandType type = ToolsCommandType::None;
    uint8_t value = 0;
};

class ToolsCommandQueue {
public:
    static constexpr uint8_t kCapacity = 4;

    bool post(const ToolsCommand & command);
    bool take(ToolsCommand & command);
    uint8_t size() const { return count_; }

private:
    ToolsCommand commands_[kCapacity]{};
    uint8_t head_ = 0;
    uint8_t tail_ = 0;
    uint8_t count_ = 0;
};

struct FlashlightPowerState {
    FlashlightPowerState(uint8_t percent = 0,
                         int8_t temperature = 25,
                         bool is_valid = false)
        : battery_percent(percent),
          temperature_c(temperature),
          valid(is_valid) {}

    uint8_t battery_percent;
    int8_t temperature_c;
    bool valid;
};

class FlashlightSession {
public:
    static constexpr uint32_t kMaxDurationMs = 60000UL;
    static constexpr uint8_t kMinBatteryPercent = 15;
    static constexpr int8_t kMinSafeTemperatureC = 0;
    static constexpr int8_t kMaxSafeTemperatureC = 45;

    bool canStart(const FlashlightPowerState & state) const;
    bool start(const FlashlightPowerState & state,
               uint32_t now_ms,
               uint8_t original_brightness);
    bool active(uint32_t now_ms) const;
    bool shouldStop(uint32_t now_ms) const;
    void stop();
    void closeFromUser();
    bool running() const { return active_; }
    uint8_t originalBrightness() const { return original_brightness_; }
    const char * lastDenial() const { return last_denial_; }

private:
    bool active_ = false;
    uint32_t started_at_ms_ = 0;
    uint8_t original_brightness_ = 0;
    const char * last_denial_ = "";
};

class FlashlightController {
public:
    bool start(const FlashlightPowerState & state,
               uint32_t now_ms,
               uint8_t original_brightness);
    bool tick(uint32_t now_ms);
    bool stop();
    bool takeCommand(ToolsCommand & command) { return queue_.take(command); }
    const FlashlightSession & session() const { return session_; }

private:
    FlashlightSession session_{};
    ToolsCommandQueue queue_{};
};

class ToolsApp {
public:
    bool create(lv_obj_t * parent, UiComponents & components);
    void destroy();
    void show();
    void hide();
    void refresh(const SystemState & state,
                 uint8_t current_brightness,
                 uint32_t now_ms);
    void tick(uint32_t now_ms);
    bool takeCommand(ToolsCommand & command);
    bool closeFlashlightFromInput();
    lv_obj_t * root() const { return root_; }
    CalculatorEngine & calculator() { return calculator_; }
    const FlashlightSession & flashlight() const { return flashlight_controller_.session(); }

private:
    static void calculatorKeyEvent(lv_event_t * event);
    static void flashlightStartEvent(lv_event_t * event);
    static void flashlightCloseEvent(lv_event_t * event);
    void handleCalculatorKey(lv_obj_t * button);
    void startFlashlight();
    void setFlashlightOverlayVisible(bool visible);
    void refreshStatusLabels(const SystemState & state);

    lv_obj_t * root_ = nullptr;
    lv_obj_t * calculator_display_label_ = nullptr;
    lv_obj_t * battery_label_ = nullptr;
    lv_obj_t * flashlight_label_ = nullptr;
    lv_obj_t * flashlight_overlay_ = nullptr;
    CalculatorEngine calculator_{};
    FlashlightController flashlight_controller_{};
    SystemState current_state_{};
    uint8_t current_brightness_ = 128;
};

}  // namespace firefly
