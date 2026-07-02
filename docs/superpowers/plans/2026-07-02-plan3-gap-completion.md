# FireflyOS Plan 3 Gap Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the remaining Plan 3 software gaps while keeping unverified hardware wake paths disabled.

**Architecture:** Preserve the existing services and legacy alarm editor, then add stateful clock sessions, a UI-main settings command consumer, full power-state integration, and a gated LightSleep attempt path. All LVGL work stays on the UI loop; background work only reads devices and posts fixed-size events.

**Tech Stack:** Arduino ESP32 2.0.17, LVGL 8.3.11, FreeRTOS, SensorLib/QMI8658, PCF85063, AXP2101, Preferences, Arduino CLI.

---

## File structure

- Modify `libraries/FireflyOS/src/firefly/apps/clock/ClockApp.h/.cpp`: clock session models, timer/stopwatch pages, UI callbacks.
- Modify `libraries/FireflyOS/src/firefly/apps/settings/SettingsApp.h/.cpp`: complete fixed-capacity settings commands.
- Modify `libraries/FireflyOS/src/firefly/services/PowerService.h/.cpp`: LightSleep attempt result and one-shot lifecycle.
- Modify `libraries/FireflyOS/src/firefly/services/MotionService.h/.cpp`: explicit runtime motion power policy and diagnostics counters.
- Modify `libraries/FireflyOS/src/firefly/core/SystemEvent.h`: timer-expired event.
- Modify `Firefly/Firefly.ino`: page creation, settings callbacks, timer overlay, sleep hooks.
- Modify `Firefly/FireflyInteraction.cpp`: command consumers, clock session tick, full power evaluation, gated sleep attempt.
- Modify `Firefly/FireflyApp.h` and `Firefly/FireflyState.cpp`: declarations and fixed runtime state.
- Modify `tests/FireflyCoreTests/FireflyCoreTests.ino`: pure model and policy tests.
- Modify `tests/python/test_repository_contracts.py`: integration boundary checks.
- Modify `docs/UI预览/02-核心应用/时钟与设置.html`: final timer/stopwatch and ASCII firmware labels.
- Modify `docs/模块说明/03-时间闹钟电源与活动.md` and `docs/模块说明/04-低功耗唤醒矩阵.md`: accurate software/hardware status.

### Task 1: Complete countdown and stopwatch sessions

**Files:**
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `libraries/FireflyOS/src/firefly/apps/clock/ClockApp.h`
- Modify: `libraries/FireflyOS/src/firefly/apps/clock/ClockApp.cpp`

- [ ] **Step 1: Add failing session tests**

```cpp
static void test_countdown_pause_resume_and_one_shot_expiry() {
    firefly::CountdownTimer timer;
    timer.start(60000, 1000);
    timer.pause(11000);
    expect_true(!timer.running() && timer.remainingMs(50000) == 50000,
                "paused countdown preserves remaining time");
    timer.resume(20000);
    expect_true(timer.remainingMs(69999) == 1,
                "resumed countdown uses a new target");
    expect_true(timer.consumeExpired(70000),
                "countdown publishes expiry once");
    expect_true(!timer.consumeExpired(70001),
                "countdown expiry is one shot");
}

static void test_stopwatch_survives_page_visibility_changes() {
    firefly::StopwatchSession stopwatch;
    stopwatch.start(1000000);
    expect_true(stopwatch.elapsedUs(4000000) == 3000000,
                "stopwatch follows monotonic time without page state");
    stopwatch.pause(5000000);
    stopwatch.start(9000000);
    expect_true(stopwatch.elapsedUs(10000000) == 5000000,
                "stopwatch resumes accumulated time");
}
```

- [ ] **Step 2: Run the core build and confirm RED**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target FireflyCoreTests
```

Expected: compilation fails because `CountdownTimer::pause`, `resume`, and `consumeExpired` do not exist.

- [ ] **Step 3: Implement the minimal session API**

```cpp
class CountdownTimer {
public:
    void start(uint32_t duration_ms, uint32_t now_ms);
    void pause(uint32_t now_ms);
    void resume(uint32_t now_ms);
    void reset(uint32_t duration_ms);
    bool consumeExpired(uint32_t now_ms);
    bool running() const;
    uint32_t remainingMs(uint32_t now_ms) const;
private:
    uint32_t target_ms_ = 0;
    uint32_t remaining_ms_ = 0;
    bool running_ = false;
    bool expiry_consumed_ = false;
};
```

`pause()` stores `remainingMs(now_ms)`, `resume()` creates a new target, and `consumeExpired()` returns true only on the first tick at or after zero.

- [ ] **Step 4: Build GREEN and add ClockApp UI pages**

Create overview, timer, and stopwatch containers inside the existing root. Use 1/5/10 minute preset buttons and Start/Pause/Reset buttons, all at least 48px high. Add:

```cpp
void ClockApp::tick(uint32_t now_ms, int64_t now_us);
bool ClockApp::consumeTimerExpired(uint32_t now_ms);
```

The page callbacks only update these in-memory models and LVGL objects on the UI loop.

- [ ] **Step 5: Rebuild both sketches and commit**

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target FireflyCoreTests
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target Firefly
git add tests/FireflyCoreTests libraries/FireflyOS/src/firefly/apps/clock
git commit -m "feat: complete clock timer and stopwatch sessions"
```

### Task 2: Route Settings through a fixed command consumer

**Files:**
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `libraries/FireflyOS/src/firefly/apps/settings/SettingsApp.h`
- Modify: `libraries/FireflyOS/src/firefly/apps/settings/SettingsApp.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `Firefly/FireflyApp.h`

- [ ] **Step 1: Add failing command payload tests**

```cpp
static void test_settings_commands_preserve_time_and_alarm_payloads() {
    firefly::SettingsCommandQueue queue;
    firefly::SettingsCommand time{};
    time.type = firefly::SettingsCommandType::SetLocalTime;
    time.value = 1783008000LL;
    expect_true(queue.post(time), "settings accepts epoch command");
    firefly::SettingsCommand actual{};
    expect_true(queue.take(actual) && actual.value == 1783008000LL,
                "settings preserves 64 bit epoch");

    firefly::SettingsCommand alarm{};
    alarm.type = firefly::SettingsCommandType::SaveAlarm;
    alarm.slot = 1;
    alarm.alarm.configured = true;
    alarm.alarm.hour = 7;
    alarm.alarm.minute = 30;
    expect_true(queue.post(alarm), "settings accepts alarm command");
    expect_true(queue.take(actual) && actual.slot == 1 &&
                    actual.alarm.minute == 30,
                "settings preserves fixed alarm payload");
}
```

- [ ] **Step 2: Run RED**

Expected: compilation fails because the command still uses `int32_t` and has no alarm payload.

- [ ] **Step 3: Extend the command without dynamic memory**

```cpp
enum class SettingsCommandType : uint8_t {
    None, SetBrightness, SetVolume, SetLocalTime, ReloadRtc,
    SetAutoSleep, SaveAlarm
};

struct SettingsCommand {
    SettingsCommandType type = SettingsCommandType::None;
    int64_t value = 0;
    uint8_t slot = 0;
    Alarm alarm{};
};
```

Retain queue capacity 8 and FIFO overflow rejection.

- [ ] **Step 4: Convert LVGL callbacks to producers**

`slider_volume_cb`, `slider_brightness_cb`, `auto_sleep_cb`, time save/reload, alarm toggle, and alarm editor confirmation post `SettingsCommand`. They do not call Preferences or board methods.

- [ ] **Step 5: Add the UI-main consumer**

```cpp
void firefly_process_settings_commands() {
    firefly::SettingsCommand command{};
    while(settings_app.takeCommand(command)) {
        switch(command.type) {
            case firefly::SettingsCommandType::SetBrightness:
                set_screen_brightness_level(static_cast<uint8_t>(command.value));
                break;
            case firefly::SettingsCommandType::SetVolume:
                volume_level = static_cast<uint8_t>(command.value);
                save_volume_preference();
                break;
            case firefly::SettingsCommandType::SetLocalTime:
                if(time_service.setLocalTime(command.value)) {
                    sync_time_to_system_from_epoch(command.value);
                }
                break;
            case firefly::SettingsCommandType::ReloadRtc: {
                const firefly::TimeSnapshot snapshot = time_service.reloadRtc();
                if(snapshot.valid) sync_time_to_system_from_epoch(snapshot.epoch_seconds);
                break;
            }
            case firefly::SettingsCommandType::SetAutoSleep:
                auto_sleep_ms = static_cast<uint32_t>(command.value);
                break;
            case firefly::SettingsCommandType::SaveAlarm:
                if(command.slot < FIREFLY_ALARM_SLOT_COUNT &&
                   alarm_service.set(command.slot, command.alarm)) {
                    copy_service_alarm_to_legacy(command.slot, command.alarm);
                    save_alarm_preferences();
                    clear_alarm_trigger_history();
                }
                break;
            default:
                break;
        }
    }
}
```

Add `copy_service_alarm_to_legacy(slot, alarm)` beside the existing inverse conversion helper; it copies each scalar and uses `strlcpy` for the fixed 24-byte name.

Call it from Arduino `loop()` before `lv_timer_handler()`. On queue failure, leave the old state intact and increment a fixed diagnostic counter.

- [ ] **Step 6: Add repository contracts and commit**

Add Python assertions that settings callbacks call `settings_app.postCommand`, that `firefly_process_settings_commands()` exists, and that the callbacks contain no `prefs.put` calls.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
git add tests libraries/FireflyOS/src/firefly/apps/settings Firefly
git commit -m "refactor: route settings through ui command queue"
```

### Task 3: Integrate full power modes and gated LightSleep lifecycle

**Files:**
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `libraries/FireflyOS/src/firefly/services/PowerService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/PowerService.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `tests/python/test_repository_contracts.py`

- [ ] **Step 1: Add failing one-shot lifecycle tests**

```cpp
static bool fake_sleep_enter_result = true;
static uint8_t fake_sleep_enter_calls = 0;
static bool fake_sleep_enter() {
    ++fake_sleep_enter_calls;
    return fake_sleep_enter_result;
}

static void test_light_sleep_attempt_restores_on_success_and_failure() {
    firefly::PowerService power;
    power.setSleepHooks({fake_sleep_prepare, fake_sleep_restore});
    expect_true(power.attemptLightSleep(fake_sleep_enter) ==
                    firefly::SleepAttemptResult::GateClosed,
                "sleep attempt rejects unverified wake matrix");
    power.recordWakeVerification(firefly::WakeSource::Boot, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::PowerButton, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::RtcAlarm, 100, 100);
    fake_sleep_enter_result = false;
    expect_true(power.attemptLightSleep(fake_sleep_enter) ==
                    firefly::SleepAttemptResult::EnterFailed,
                "failed platform entry is reported");
    expect_true(sleep_restore_calls == 1,
                "failed entry restores prepared resources");
}
```

- [ ] **Step 2: Run RED**

Expected: compilation fails because `SleepAttemptResult` and `attemptLightSleep` are absent.

- [ ] **Step 3: Implement the gated lifecycle**

```cpp
enum class SleepAttemptResult : uint8_t {
    Entered, GateClosed, PrepareFailed, EnterFailed
};

SleepAttemptResult PowerService::attemptLightSleep(bool (*enter)()) {
    if(!canEnterLightSleep()) return SleepAttemptResult::GateClosed;
    if(!prepareForLightSleep()) return SleepAttemptResult::PrepareFailed;
    const bool entered = enter && enter();
    restoreFromLightSleep();
    return entered ? SleepAttemptResult::Entered
                   : SleepAttemptResult::EnterFailed;
}
```

- [ ] **Step 4: Use complete power evaluation at runtime**

Call `power_service.evaluate(now)` for Charging, Saver, LowBattery, CriticalBattery, and ThermalProtection. Continue to use `evaluateIdle(now)` only as the nested display-idle state for Saver/LowBattery. Apply brightness caps of 160 for Saver, 96 for LowBattery, and 64 for CriticalBattery; never exceed the user setting.

- [ ] **Step 5: Install real hooks without enabling an unverified path**

The UI-main prepare hook persists motion/settings, closes flashlight and overlays, changes MotionService to sleep mode, and turns the display off. The enter callback calls `esp_light_sleep_start()` only after the wake-source configurator reports BOOT, PWR, and RTC ready. With the current 0/100 matrix, `attemptLightSleep()` returns `GateClosed` and ScreenOff remains active.

- [ ] **Step 6: Add contracts, verify, and commit**

Repository tests assert that the main runtime calls `evaluate(`, that any `esp_light_sleep_start` call is reachable only through `attemptLightSleep`, and that background code contains no sleep entry or LVGL calls.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
git add tests libraries/FireflyOS/src/firefly/services/PowerService.* Firefly
git commit -m "feat: complete gated power lifecycle"
```

### Task 4: Add Motion power policy and bounded diagnostics

**Files:**
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `libraries/FireflyOS/src/firefly/services/MotionService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/MotionService.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`

- [ ] **Step 1: Add failing policy tests**

```cpp
static void test_motion_power_policy_keeps_gyro_for_screen_off_wrist_raise() {
    expect_true(firefly::MotionPowerPolicy::modeFor(false, false) ==
                    firefly::MotionPowerMode::Normal,
                "active mode keeps normal sampling");
    expect_true(firefly::MotionPowerPolicy::modeFor(true, false) ==
                    firefly::MotionPowerMode::Normal,
                "screen off cpu mode keeps gyro for wrist raise");
    expect_true(firefly::MotionPowerPolicy::modeFor(true, true) ==
                    firefly::MotionPowerMode::LowPower,
                "light sleep preparation uses low power sensor mode");
}
```

- [ ] **Step 2: Run RED and implement fixed policy types**

```cpp
enum class MotionPowerMode : uint8_t { Normal, LowPower };

class MotionPowerPolicy {
public:
    static MotionPowerMode modeFor(bool screen_off,
                                   bool entering_light_sleep);
};
```

Both LightSleep cases use LowPower until IMU interrupt wake is separately verified; normal ScreenOff keeps the gyroscope enabled.

- [ ] **Step 3: Add bounded counters**

Track `valid_samples`, `invalid_samples`, `steps`, and `wrist_events` as fixed integers. Snapshot them under the existing critical section and print one summary every 10 seconds with Gate A diagnostics; never print each raw sample.

- [ ] **Step 4: Integrate prepare/restore and commit**

Sleep prepare calls `motion_service.setLowPower(true)`. Restore calls `setLowPower(false)` and disables the Motion capability if reconfiguration fails.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
git add tests libraries/FireflyOS/src/firefly/services/MotionService.* Firefly
git commit -m "feat: add motion power policy and diagnostics"
```

### Task 5: Make all Plan 3 firmware labels render with current fonts

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/apps/calendar/CalendarApp.cpp`
- Modify: `libraries/FireflyOS/src/firefly/apps/tools/ToolsApp.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `tests/python/test_repository_contracts.py`
- Modify: `docs/UI预览/02-核心应用/时钟与设置.html`

- [ ] **Step 1: Add a failing repository contract**

Scan the Plan 3 app sources and power menu strings. When `LV_FONT_SIMSUN_16_CJK` is zero and no generated font source exists, assert that firmware labels contain ASCII only.

- [ ] **Step 2: Run RED**

```powershell
python -m unittest tests.python.test_repository_contracts -v
```

Expected: failure listing Calendar, Tools, calculator errors, and power-menu Chinese labels.

- [ ] **Step 3: Replace firmware labels with concise English**

Use `Offline month`, `No synced events`, `Calculator`, `Screen flashlight`, `Power menu`, `Sleep`, `Restart`, `Shutdown`, and `Cancel`. Keep Chinese explanation outside the watch frame in HTML documentation.

- [ ] **Step 4: Run GREEN and render the preview**

```powershell
python -m unittest tests.python.test_repository_contracts -v
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target Firefly
```

Render the HTML to PNG and visually check 410 × 502 safe areas and 48px controls.

- [ ] **Step 5: Commit**

```bash
git add tests/python libraries/FireflyOS/src/firefly/apps Firefly/FireflyInteraction.cpp docs/UI预览/02-核心应用
git commit -m "fix: keep plan 3 ui readable without cjk font"
```

### Task 6: Final integration, documentation, and hardware handoff

**Files:**
- Modify: `docs/模块说明/03-时间闹钟电源与活动.md`
- Modify: `docs/模块说明/04-低功耗唤醒矩阵.md`
- Modify: `docs/项目介绍.md`
- Modify: `Firefly/README.md`

- [ ] **Step 1: Run fresh complete verification**

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
```

Expected: 0 Python failures, both Arduino targets compile, and documentation marker scan passes.

- [ ] **Step 2: Check repository state and resource protection**

Confirm only Plan 3 files changed, `image/图片生成提示词` is untouched, and no merge or push occurred.

- [ ] **Step 3: Update status documents with exact evidence**

Record the final Flash/RAM values and distinguish:

- software path compiled;
- core assertions compiled but not executed on hardware;
- hardware wake, motion accuracy, current, and endurance measurements not executed.

Provide a true-device checklist with attempt count, success count, firmware commit, and measured current fields. Leave measurement fields blank rather than inventing values.

- [ ] **Step 4: Commit documentation**

```bash
git add docs/模块说明/03-时间闹钟电源与活动.md docs/模块说明/04-低功耗唤醒矩阵.md docs/项目介绍.md Firefly/README.md
git commit -m "docs: close plan 3 software gaps"
```

- [ ] **Step 5: Preserve the branch**

Keep `codex/core-apps-power-imu` and its worktree. Do not merge, push, delete, or start the next plan without explicit user direction.
