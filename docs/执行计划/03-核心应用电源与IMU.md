# FireflyOS 核心应用、电源与 IMU Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将时间、闹钟和设置迁入系统服务与应用框架，建立真实电源状态机、统一按键语义，并接入 QMI8658 抬腕、计步和活动应用。

**Architecture:** 时间、闹钟、电源和运动均以独立服务维护状态，通过 EventBus 更新 UI；RTC、PMU 与 IMU 只经 HAL 访问。低功耗与 IMU 采样先在单服务模式验证，再决定是否启用后台核。

**Tech Stack:** Arduino time API、PCF85063、AXP2101、QMI8658/SensorLib、FreeRTOS、LVGL 8.3.11、NVS Preferences。

---

## 1. 文件结构锁定

```text
libraries/FireflyOS/src/firefly/services/TimeService.h
libraries/FireflyOS/src/firefly/services/TimeService.cpp
libraries/FireflyOS/src/firefly/services/AlarmService.h
libraries/FireflyOS/src/firefly/services/AlarmService.cpp
libraries/FireflyOS/src/firefly/services/PowerService.h
libraries/FireflyOS/src/firefly/services/PowerService.cpp
libraries/FireflyOS/src/firefly/services/InputService.h
libraries/FireflyOS/src/firefly/services/InputService.cpp
libraries/FireflyOS/src/firefly/services/MotionService.h
libraries/FireflyOS/src/firefly/services/MotionService.cpp
libraries/FireflyOS/src/firefly/hal/Qmi8658Device.h
libraries/FireflyOS/src/firefly/hal/Qmi8658Device.cpp
libraries/FireflyOS/src/firefly/apps/clock/ClockApp.h
libraries/FireflyOS/src/firefly/apps/clock/ClockApp.cpp
libraries/FireflyOS/src/firefly/apps/settings/SettingsApp.h
libraries/FireflyOS/src/firefly/apps/settings/SettingsApp.cpp
libraries/FireflyOS/src/firefly/apps/calendar/CalendarApp.h
libraries/FireflyOS/src/firefly/apps/calendar/CalendarApp.cpp
libraries/FireflyOS/src/firefly/apps/tools/ToolsApp.h
libraries/FireflyOS/src/firefly/apps/tools/ToolsApp.cpp
libraries/FireflyOS/src/firefly/apps/activity/ActivityApp.h
libraries/FireflyOS/src/firefly/apps/activity/ActivityApp.cpp
docs/UI预览/02-核心应用/
docs/模块说明/03-时间闹钟电源与活动.md
```

**Modify progressively:**

```text
Firefly/FireflyAlarm.h:1-31
Firefly/FireflyAlarm.cpp:1-179
Firefly/FireflyInteraction.cpp:129-163
Firefly/FireflyInteraction.cpp:219-332
Firefly/FireflyInteraction.cpp:379-589
Firefly/FireflyInteraction.cpp:704-814
Firefly/Firefly.ino:1-275
Firefly/Firefly.ino:611-1000
```

## 2. Task 1：迁移 TimeService 与 AlarmService

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/TimeService.*`
- Create: `libraries/FireflyOS/src/firefly/services/AlarmService.*`
- Modify: `Firefly/FireflyAlarm.*`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 写跨日和重复闹钟测试**

  ```cpp
  static void test_alarm_next_trigger() {
      firefly::AlarmService service;
      firefly::Alarm alarm{};
      alarm.configured = true;
      alarm.enabled = true;
      alarm.hour = 7;
      alarm.minute = 30;
      alarm.days_mask = 0x7F;
      service.set(0, alarm);
      const int64_t now = 1767221940;  // 2026-01-01 07:19:00 +08
      const auto next = service.nextTrigger(now);
      expect_true(next.valid, "next alarm exists");
      expect_true(next.slot == 0, "next alarm slot");
      expect_true(next.epoch_seconds > now, "next alarm is future");
  }
  ```

- [ ] **Step 2: 编译并确认失败**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target FireflyCoreTests
  ```

  Expected: FAIL，AlarmService 尚不存在。

- [ ] **Step 3: 定义服务接口**

  ```cpp
  namespace firefly {
  struct Alarm {
      bool configured = false;
      bool enabled = false;
      uint8_t hour = 7;
      uint8_t minute = 30;
      uint8_t days_mask = 0x7F;
      uint8_t ringtone = 0;
      char name[24] = "闹钟";
  };

  struct AlarmTrigger {
      bool valid = false;
      uint8_t slot = 0;
      int64_t epoch_seconds = 0;
  };

  class AlarmService {
  public:
      static constexpr uint8_t kSlots = 2;
      bool set(uint8_t slot, const Alarm & alarm);
      const Alarm & get(uint8_t slot) const;
      AlarmTrigger nextTrigger(int64_t now) const;
      bool shouldTrigger(int64_t now, uint8_t & slot);
  private:
      Alarm alarms_[kSlots]{};
      int64_t last_trigger_minute_[kSlots]{-1, -1};
  };
  }
  ```

- [ ] **Step 4: 复用现有可靠算法**

  将 `firefly_alarm_matches_weekday()` 和 `firefly_alarm_find_next()` 的纯逻辑迁入服务；保持双槽位、星期掩码和 8 天搜索边界。旧 API 暂时调用新服务，待 UI 迁移完成后删除。

- [ ] **Step 5: 实现 TimeService**

  TimeService 接收 `ClockDevice&`，提供 `begin()`、`now()`、`setLocalTime()`、`reloadRtc()` 和每秒 `tick()`；RTC 无效时返回 `valid=false`，不生成假日期。

- [ ] **Step 6: 运行验证并提交**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/services Firefly/FireflyAlarm.* tests/FireflyCoreTests
  git commit -m "refactor: move time and alarms into services"
  ```

## 3. Task 2：迁移设置与时钟应用

**Files:**
- Create: `docs/UI预览/02-核心应用/时钟与设置.html`
- Create: `libraries/FireflyOS/src/firefly/apps/clock/ClockApp.*`
- Create: `libraries/FireflyOS/src/firefly/apps/settings/SettingsApp.*`
- Modify: `Firefly/Firefly.ino:1-275`
- Modify: `Firefly/Firefly.ino:611-1000`

- [ ] **Step 1: 先绘制并审批预览**

  预览必须包含：时钟首页、双闹钟列表、闹钟编辑、计时器、秒表、时间日期设置和电源设置。每页使用 410 × 502 画框并标出安全区。用户批准后继续。

- [ ] **Step 2: 建立 ClockApp 页面工厂**

  ```cpp
  class ClockApp {
  public:
      bool create(lv_obj_t * parent,
                  UiComponents & components,
                  TimeService & time,
                  AlarmService & alarms);
      void destroy();
      void refresh(const SystemState & state);
  private:
      lv_obj_t * root_ = nullptr;
      lv_obj_t * time_label_ = nullptr;
      lv_obj_t * alarm_list_ = nullptr;
  };
  ```

- [ ] **Step 3: 迁移闹钟编辑行为**

  保留当前滚轮、铃声、重复日期和 23 字符名称限制；布局只按批准预览调整。保存动作先调用 `AlarmService::set()`，成功后由存储服务持久化，UI 不直接写 Preferences。

- [ ] **Step 4: 增加计时器与秒表模型**

  计时器使用目标 epoch，不依赖页面定时器累计；秒表使用 `esp_timer_get_time()` 的微秒单调时钟，页面隐藏后仍能正确恢复显示。首版只保留一个计时器和一个秒表会话。

- [ ] **Step 5: 迁移 SettingsApp**

  设置项只发送服务命令；亮度调用 PowerService，时间调用 TimeService，音量调用 AudioService 的预留接口。保留现有闹钟编辑圆角安全区常量，不整体重写滚轮布局。

- [ ] **Step 6: 回归并提交**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add docs/UI预览/02-核心应用 libraries/FireflyOS/src/firefly/apps Firefly
  git commit -m "refactor: migrate clock and settings applications"
  ```

## 4. Task 3：实现日历与轻量工具

**Files:**
- Create: `libraries/FireflyOS/src/firefly/apps/calendar/CalendarApp.*`
- Create: `libraries/FireflyOS/src/firefly/apps/tools/ToolsApp.*`
- Modify: `docs/UI预览/02-核心应用/时钟与设置.html`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 审批日历、计算器和手电筒预览**

  日历展示月视图、今日、最多 8 条日程摘要和无手机同步状态；工具页包含计算器和屏幕手电筒。预览批准后继续。

- [ ] **Step 2: 写日历边界测试**

  测试闰年 2028-02、周日起始映射、跨年翻月和最多 8 条摘要截断；日期计算使用 `tm`/epoch，不在 UI 中手算月份长度。

- [ ] **Step 3: 实现 CalendarApp**

  CalendarApp 离线生成本地月视图；手机同步摘要只作为可选数据源，断连后保留最近摘要并标记更新时间。首版不在手表编辑复杂日程。

- [ ] **Step 4: 写计算器测试并实现**

  支持非负/负数、加减乘除、小数和清除；表达式最长 24 字符，除零显示“无法除以零”，结果限制 12 个可见字符，不支持函数图形和脚本执行。

- [ ] **Step 5: 实现屏幕手电筒保护**

  白屏手电筒最长 60 秒；电量低于 15% 或温度超出 PowerService 安全范围时拒绝启动；PWR/BOOT/触摸关闭并恢复原亮度。

- [ ] **Step 6: 验证并提交**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/apps/calendar libraries/FireflyOS/src/firefly/apps/tools docs/UI预览/02-核心应用 tests/FireflyCoreTests
  git commit -m "feat: add calendar calculator and flashlight tools"
  ```

## 5. Task 4：实现 PowerService 状态机

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/PowerService.*`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `Firefly/FireflyInteraction.cpp:219-256`
- Modify: `Firefly/FireflyInteraction.cpp:520-621`

- [ ] **Step 1: 写状态转换测试**

  ```cpp
  static void test_power_state_machine() {
      firefly::PowerService power;
      power.configure({30000, 5000, 3000});
      power.onActivity(1000);
      expect_true(power.evaluate(30999) == firefly::PowerMode::Active,
                  "active before timeout");
      expect_true(power.evaluate(31001) == firefly::PowerMode::IdleDim,
                  "dim after idle timeout");
      expect_true(power.evaluate(36001) == firefly::PowerMode::Glance,
                  "glance after dim");
      expect_true(power.evaluate(39001) == firefly::PowerMode::ScreenOff,
                  "screen off after glance");
  }
  ```

- [ ] **Step 2: 定义状态与配置**

  ```cpp
  enum class PowerMode : uint8_t {
      Active, IdleDim, Glance, ScreenOff, LightSleep, Charging,
      Saver, LowBattery, CriticalBattery
  };

  struct PowerTiming {
      uint32_t idle_to_dim_ms;
      uint32_t dim_to_glance_ms;
      uint32_t glance_to_off_ms;
  };
  ```

- [ ] **Step 3: 实现纯状态计算**

  `evaluate(now_ms)` 不直接关屏，只返回目标模式；实际亮度、页面显示和睡眠调用由 UI 核处理。电池阈值固定为：省电 25%、低电量 15%、极低 5%。充电优先于普通模式，但低温/高温保护仍可覆盖充电页面。

- [ ] **Step 4: 替换旧自动息屏判断**

  用 PowerService 状态变化事件替换 `should_auto_enter_sleep_now()` 和 `should_blackout_sleep_now()` 的分散逻辑；保持当前 2 秒一瞥行为，直到低功耗链路验证完成。

- [ ] **Step 5: 运行验证并提交**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/services/PowerService.* Firefly tests/FireflyCoreTests
  git commit -m "feat: add deterministic power state machine"
  ```

## 6. Task 5：统一 BOOT 与 PWR 输入语义

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/InputService.*`
- Modify: `Firefly/FireflyInteraction.cpp:38-54`
- Modify: `Firefly/FireflyInteraction.cpp:86-107`

- [ ] **Step 1: 写短按、长按和去抖测试**

  用时间序列验证：按下 40ms 后释放产生 ShortPress；按住 1200ms 产生 LongPress；10ms 抖动不产生事件；长按只触发一次。

- [ ] **Step 2: 实现按键状态机**

  ```cpp
  enum class ButtonAction : uint8_t { None, ShortPress, LongPress };

  class DebouncedButton {
  public:
      ButtonAction update(bool pressed, uint32_t now_ms);
  private:
      bool stable_pressed_ = false;
      bool sampled_pressed_ = false;
      bool long_sent_ = false;
      uint32_t changed_at_ = 0;
      uint32_t pressed_at_ = 0;
  };
  ```

  去抖 30ms，长按阈值 1000ms，短按在释放时产生。

- [ ] **Step 3: 探测 PWR 事件来源**

  优先通过 AXP2101 IRQ/状态读取 PWR 短按和长按；若硬件无法软件区分，则 CapabilityRegistry 将 `PowerButton` 标记不可用，保留 BOOT 唤醒/返回，UI 不显示不可用的电源菜单提示。

- [ ] **Step 4: 映射全局行为**

  - BOOT 短按：应用返回；桌面锁屏；锁屏进入一瞥。
  - PWR 短按：屏幕开关。
  - PWR 长按：电源菜单。
  - 闹钟显示时：任一受支持短按关闭闹钟。

- [ ] **Step 5: 验证并提交**

  真机各执行 30 次短按、10 次长按和 20 次快速抖动模拟。Expected: 无双触发或漏触发。

  ```powershell
  git add libraries/FireflyOS/src/firefly/services/InputService.* Firefly tests/FireflyCoreTests
  git commit -m "feat: unify boot and power button semantics"
  ```

## 7. Task 6：进入真实 LightSleep

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/PowerService.*`
- Create: `docs/模块说明/04-低功耗唤醒矩阵.md`
- Modify: `Firefly/Firefly.ino:1176-1184`

- [ ] **Step 1: 记录唤醒矩阵**

  文档逐项实测 BOOT、PWR、RTC 闹钟、IMU 中断和充电插入能否从屏幕关闭、LightSleep 唤醒。每项记录 GPIO/PMU 来源、是否稳定、100 次成功率。

- [ ] **Step 2: 获得并发与睡眠方案确认**

  向用户说明哪些任务在睡眠前暂停、哪些外设断电、唤醒后初始化顺序及 UI 核恢复流程。未确认时保持当前仅亮度为 0 的黑屏模式。

- [ ] **Step 3: 实现睡眠前后钩子**

  ```cpp
  struct SleepHooks {
      bool (*prepare)();
      void (*restore)();
  };
  ```

  `prepare()` 顺序：关闭面板动画、停止音频/SD 会话、关闭 Wi-Fi、保存必要状态、关屏；`restore()` 顺序：恢复 I2C 设备、显示驱动、触摸和当前 Shell 路由。

- [ ] **Step 4: 只启用成功率 100% 的唤醒源**

  首版至少保留 BOOT/PWR 与 RTC 闹钟；IMU 抬腕在 Task 7 完成后加入。任一来源低于 100/100 时不进入发布配置。

- [ ] **Step 5: 测量功耗并提交**

  记录 Active、IdleDim、ScreenOff、LightSleep 四种平均电流，各测 5 分钟。LightSleep 恢复连续 100 次无失败。

  ```powershell
  git add libraries/FireflyOS/src/firefly/services/PowerService.* Firefly docs/模块说明/04-低功耗唤醒矩阵.md
  git commit -m "feat: add verified light sleep lifecycle"
  ```

## 8. Task 7：接入 QMI8658 HAL

**Files:**
- Create: `libraries/FireflyOS/src/firefly/hal/Qmi8658Device.*`
- Create: `libraries/FireflyOS/src/firefly/services/MotionService.*`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 复用官方示例确认引脚与地址**

  以 `examples/04_LVGL_QMI8658_ui/04_LVGL_QMI8658_ui.ino` 为唯一板级初始化参考；先写串口探测草图确认 WHO_AM_I、加速度和陀螺仪读数稳定。

- [ ] **Step 2: 定义运动样本与 HAL**

  ```cpp
  struct MotionSample {
      float ax, ay, az;
      float gx, gy, gz;
      uint32_t timestamp_ms;
      bool valid;
  };

  class MotionDevice {
  public:
      virtual ~MotionDevice() = default;
      virtual bool begin() = 0;
      virtual bool setLowPower(bool enabled) = 0;
      virtual MotionSample read() = 0;
  };
  ```

- [ ] **Step 3: 实现 MotionService 固定内存模型**

  服务保存最近 32 个样本的环形缓冲、当天步数、活动分钟数和抬腕冷却时间；不保存全天原始数据。

- [ ] **Step 4: 运行 10 分钟静置测试**

  Expected: 无 I2C 错误洪泛；无效样本不增加步数；静置误计步小于 5 步。

- [ ] **Step 5: Commit**

  ```powershell
  git add libraries/FireflyOS/src/firefly/hal/Qmi8658Device.* libraries/FireflyOS/src/firefly/services/MotionService.*
  git commit -m "feat: add QMI8658 motion service"
  ```

## 9. Task 8：实现抬腕、计步与活动应用

**Files:**
- Create: `docs/UI预览/02-核心应用/活动.html`
- Create: `libraries/FireflyOS/src/firefly/apps/activity/ActivityApp.*`
- Modify: `libraries/FireflyOS/src/firefly/services/MotionService.*`

- [ ] **Step 1: 先绘制活动应用预览并审批**

  展示今日步数、活动分钟、简单目标环和传感器不可用状态；不出现心率、血氧、睡眠分数或卡路里医疗结论。

- [ ] **Step 2: 写计步契约测试**

  使用固定 100Hz 加速度样本文件：静置序列产生 0 步；20 个规则步态峰产生 18–22 步；连续高频抖动触发去抖限制。

- [ ] **Step 3: 实现轻量计步器**

  对加速度模长执行一阶低通，使用上下阈值和 250–1200ms 步间隔判定；阈值与滤波系数存入校准配置，不使用动态内存。

- [ ] **Step 4: 实现抬腕判定**

  要求姿态变化、屏幕朝向和最小角速度同时满足，并设置 3 秒冷却。屏幕已亮、正在充电或活动页面高频采样时不重复触发。

- [ ] **Step 5: 接入低功耗唤醒**

  先在屏幕关闭但 CPU 未睡时验证 100 次抬腕成功率与误唤醒；达到成功率 95% 且 30 分钟静置误唤醒不超过 1 次后，再把 IMU 中断加入 LightSleep 唤醒矩阵。

- [ ] **Step 6: 运行一天数据测试并提交**

  与参考计步设备进行至少 3000 步对比；记录误差，不宣称医疗精度。当天聚合数据每 15 分钟或进入睡眠前保存一次。

  ```powershell
  git add docs/UI预览/02-核心应用/活动.html libraries/FireflyOS/src/firefly/apps/activity libraries/FireflyOS/src/firefly/services/MotionService.*
  git commit -m "feat: add wrist raise steps and activity app"
  ```

## 10. Task 9：本阶段验收

**Files:**
- Create: `docs/模块说明/03-时间闹钟电源与活动.md`
- Modify: `docs/项目介绍.md`
- Modify: `Firefly/README.md`

- [ ] **Step 1: 完整验证**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  ```

- [ ] **Step 2: 真机验收**

  - RTC 无效时提示正确，手动设置后可恢复。
  - 双闹钟跨日、重复和关闭行为正确。
  - 计时器页面退出后仍准确触发。
  - PWR/BOOT 行为符合总纲。
  - LightSleep 100 次唤醒成功。
  - 活动应用不显示不存在的健康传感器数据。
  - 400mAh 条件下记录阶段续航，不低于迁移前基线。

- [ ] **Step 3: 更新文档并提交**

  ```powershell
  git add docs/模块说明/03-时间闹钟电源与活动.md docs/项目介绍.md Firefly/README.md
  git commit -m "docs: document core apps power and motion"
  ```
