# 电源、按键、触摸与 IMU

## 目标

当前 main 已有 BOOT/触摸交互、电池/充电主程序行为、I²C 管理器、基础设备接口和 `Qmi8658ControlAdapter`，但完整电源与运动服务只存在于本地历史对象。重新开发时先统一总线所有权，再把 PMU、RTC 和 IMU 的现有基础逐项收敛到服务边界。

## 推荐边界

- BOOT 先去抖并转换为短按、长按等语义动作。
- 触摸、RTC、PMU、IMU 必须使用同一个 `I2cBusManager`，或全部由单一硬件任务串行访问。
- 后台只发布触摸点、传感器和电池快照，不调用 LVGL。
- 电量未知时拒绝高功率会话；临界电量保持失败保守。
- 浅睡默认关闭，只有各唤醒源达到规定真机成功次数后才开放。
- IMU 采样、计步和抬腕使用固定环形容量与冷却时间。

## 最小实现

1. 先稳定 BOOT、屏幕状态和 UI 手势，不启动后台传感器轮询。
2. 让触摸进入统一 I²C 所有权并做连续滑动测试。
3. 把现有 PMU/电池读取纳入统一边界，再验证触摸与电量读取并存。
4. 以现有 QMI8658 控制适配为起点加入低频采样、计步和抬腕，每步记录功耗与误触。
5. 最后建立唤醒矩阵；验证完成前保持真实浅睡关闭。

## 主要风险

当前 `touch.h` 直接使用 `Wire`，而受锁设备走 `I2cBusManager`，跨核并发可能表现为触摸失效、卡屏、闪屏或重启。锁屏短按进入一瞥、约两秒后黑屏可能是既定状态转换；上传中途串口异常发生在应用启动前时，应单独检查 USB 和烧录链路。

## 参考路径

- 当前：`Firefly/touch.h`
- 当前：`Firefly/FireflyDisplay.cpp`
- 当前：`libraries/FireflyOS/src/firefly/hal/I2cBusManager.*`
- 当前：`libraries/FireflyOS/src/firefly/hal/LockedRegisterDevice.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/PowerService.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/MotionService.*`
