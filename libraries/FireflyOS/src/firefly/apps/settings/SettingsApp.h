#pragma once

#include <stdint.h>
#include <lvgl.h>

#include "../../core/SystemState.h"
#include "../../services/TimeService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

enum class SettingsCommandType : uint8_t {
    None,
    SetBrightness,
    SetVolume,
    SetLocalTime
};

struct SettingsCommand {
    SettingsCommand() = default;
    SettingsCommand(SettingsCommandType command_type, int32_t command_value)
        : type(command_type), value(command_value) {}

    SettingsCommandType type = SettingsCommandType::None;
    int32_t value = 0;
};

class SettingsCommandQueue {
public:
    static constexpr uint8_t kCapacity = 8;

    bool post(const SettingsCommand & command);
    bool take(SettingsCommand & command);
    uint8_t size() const { return count_; }

private:
    SettingsCommand commands_[kCapacity]{};
    uint8_t head_ = 0;
    uint8_t tail_ = 0;
    uint8_t count_ = 0;
};

class SettingsApp {
public:
    bool create(lv_obj_t * parent, UiComponents & components, TimeService & time);
    bool bindLegacyPanel(lv_obj_t * panel);
    void show();
    void hide();
    void refresh(const SystemState & state);
    bool postCommand(const SettingsCommand & command);
    bool takeCommand(SettingsCommand & command);
    lv_obj_t * root() const { return root_; }

private:
    lv_obj_t * root_ = nullptr;
    lv_obj_t * status_label_ = nullptr;
    TimeService * time_ = nullptr;
    SettingsCommandQueue queue_{};
};

}  // namespace firefly
