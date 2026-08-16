# 电源按键触摸与 IMU

## 来源

- 固定提交：`e4fb0d1ff71bcc0330b507fa90c653be9929e611`
- 主要路径：`services/PowerService.*`、`InputService.*`、`MotionService.*`、`hal/Qmi8658Device.*`、`hal/I2cBusManager.*`、`Firefly/touch.h`
- 取回示例：`git show "e4fb0d1:libraries/FireflyOS/src/firefly/services/PowerService.h"`

## 复用等级

**仅作反例或诊断参考。** 电源状态、唤醒验证门和运动算法可拆开借鉴，但旧集成把触摸、PMU、RTC、QMI8658 放在同一 `Wire` 上，触摸读取却绕过 `I2cBusManager`，存在跨核竞态，禁止整模块照搬。

## 模块定位

该功能域负责 BOOT 按键去抖、屏幕状态、电池策略、浅睡门控、QMI8658 采样、计步和抬腕。它同时触及 UI 主循环、后台任务和共享 I²C，是重启时必须最晚、最小批次重新引入的硬件域之一。

## 职责与边界

- `DebouncedButton`：30ms 去抖，1,000ms 长按阈值，只输出语义动作。
- `PowerService`：根据活动时间、电量、充电和温度计算 `PowerMode`，记录唤醒验证，门控 Wi-Fi 和浅睡。
- `MotionService`：固定 32 项采样环、计步、活跃分钟、抬腕事件和诊断计数。
- `Qmi8658Device`：通过 `I2cBusManager` 获取总线，切换正常/低功耗模式。
- `touch.h`：直接创建 `TouchLib touch(Wire, ...)` 并调用 `touch.read()`；这是当前竞态根源。

## 数据流与线程边界

```text
UI 主循环                         后台任务
my_touchpad_read                  MotionService.poll / PMU / RTC
    └─ touch_touched                  └─ I2cBusManager.lock
         └─ touch.read(Wire)               └─ Wire transaction
              ▲
              └──── 同一 SDA15/SCL14，但没有共同锁 ────┘
```

重启实现必须选择一种所有权：所有 I²C 设备统一使用同一总线管理器，或由单一硬件任务串行执行事务并把触摸点/传感器快照送给 UI。不能让 UI 核和后台核分别直接调用 `Wire`。

## 关键接口

| 类型 | 固定边界或门槛 |
|---|---|
| `PowerMode` | Active、IdleDim、Glance、ScreenOff、LightSleep、Charging、Saver、Low/Critical、Thermal |
| `PowerService` | 25% 省电、15% 低电、5% 临界；安全温度 0～45°C |
| `WakeVerification` | 每种唤醒源 attempts/successes |
| `MotionService` | `kSampleCapacity = 32` |
| `WristRaiseDetector` | `kCooldownMs = 3000` |
| `I2cBusManager` | 静态 FreeRTOS mutex，显式超时 |

## 精选代码

来源：`libraries/FireflyOS/src/firefly/services/PowerService.cpp`，符号 `allowsWifiSession`。

```cpp
bool PowerService::allowsWifiSession(bool high_power) const {
    PowerRecursiveLock lock(mutex_);
    const bool percent_known = battery_.valid && battery_.percent >= 0 &&
        battery_.percent <= 100;
    if(!percent_known) return !high_power;
    if(battery_.percent <= kCriticalBatteryPercent) return false;
    if(battery_.charging || battery_.vbus_present) return true;
    return !high_power || battery_.percent > kLowBatteryPercent;
}
```

未知电量只允许低功耗会话，临界电量全部拒绝，充电状态允许高功耗；这是可复用的失败保守策略。

来源：`libraries/FireflyOS/src/firefly/services/PowerService.cpp`，符号 `canEnterLightSleep`。

```cpp
bool PowerService::canEnterLightSleep() const {
    PowerRecursiveLock lock(mutex_);
    if(wifi_session_active_) return false;
    static const WakeSource required[] = {
        WakeSource::Boot,
        WakeSource::PowerButton,
        WakeSource::RtcAlarm
    };
    for(const WakeSource source : required) {
        const WakeVerification & verification =
            wake_verification_[static_cast<uint8_t>(source)];
        if(verification.attempts == 0 ||
           verification.successes != verification.attempts) return false;
    }
    return true;
}
```

浅睡由真机唤醒矩阵结果门控是正确方向。重启初期应保持浅睡关闭，只验证屏幕状态，不把编译通过当作唤醒验证。

来源：`libraries/FireflyOS/src/firefly/services/MotionService.h`，符号 `MotionService`。

```cpp
class MotionService {
public:
    static constexpr uint8_t kSampleCapacity = 32;
    bool poll(const MotionContext & context);
    MotionSummary summary() const;
    MotionDiagnostics diagnostics() const;
    bool consumeWristRaise();

private:
    MotionSample samples_[kSampleCapacity]{};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
};
```

来源：`Firefly/touch.h` 与 `Firefly/FireflyDisplay.cpp`，符号 `touch_touched` / `my_touchpad_read`。下面是禁止照搬的共享总线反例：

```cpp
TouchLib touch(Wire, TOUCH_SDA, TOUCH_SCL, TOUCH_MODULE_ADDR);

bool touch_touched() {
    if(touch.read()) {
        TP_Point t = touch.getPoint(0);
        // 更新触摸坐标
        return true;
    }
    return false;
}

void my_touchpad_read(lv_indev_drv_t *, lv_indev_data_t * data) {
    if(touch_touched()) {
        data->state = LV_INDEV_STATE_PR;
    }
}
```

对照 `Qmi8658Device::read()`：QMI8658 会先 `bus_.lock(50)`，完成读取后 `bus_.unlock()`；触摸侧没有同一锁。因此单独给 QMI 加锁并不能保证总线安全。

## 源码与测试映射

- 电源：`services/PowerService.h/.cpp`、`core/SystemLifecycle.*`。
- 按键：`services/InputService.h/.cpp`。
- 运动：`services/MotionService.h/.cpp`、`hal/Qmi8658Device.h/.cpp`。
- 总线：`hal/I2cBusManager.h/.cpp`、`hal/LockedRegisterDevice.*`。
- 触摸：`Firefly/touch.h`、`Firefly/FireflyDisplay.cpp`。
- 集成：`Firefly/Firefly.ino`、`Firefly/FireflyInteraction.cpp`。
- 测试：`tests/FireflyCoreTests/FireflyCoreTests.ino`、`tests/python/test_repository_contracts.py`。

## 验证边界

自动测试覆盖阈值、状态转换、固定采样容量和浅睡门逻辑；不能证明触摸/IMU 并发安全、PMU 电量准确、计步效果、抬腕误触率、功耗或唤醒成功率。全部正式真机项目仍为 `PENDING`。

## 已知问题

- 触摸与后台 PMU/RTC/IMU 对同一 `Wire` 的所有权不一致，可能导致触摸完全失效或总线事务异常。
- 旧后台任务约 10ms 轮询运动和电源，会放大竞态并增加功耗。
- 锁屏短按 BOOT 进入一瞥后约两秒黑屏符合 `Glance → ScreenOff` 定时，不代表必然崩溃。
- 上传进行到中途的串口异常发生在 bootloader/stub 阶段，应先排查 USB、驱动、端口占用和波特率。
- `PowerService` 内部互斥保护状态，不代表 PMU 驱动自身线程安全。

## 基于 main 的复用步骤

1. 先修复并完成 UI Shell 真机 Gate B，不启用后台 IMU/PMU 轮询。
2. 引入按键去抖和纯屏幕状态转换，暂不进入真正浅睡。
3. 建立唯一 `I2cBusManager` 或硬件任务，让触摸、PMU、RTC、IMU 全部通过同一所有者。
4. 单独接入触摸并连续滑动验证，再单独接入 PMU，确认两者并存稳定。
5. 最后加入低频 IMU 采样、计步和抬腕，每一步完成真机压力与功耗记录。
6. 唤醒矩阵未达到规定次数全部成功前，保持 `canEnterLightSleep()` 门关闭。

