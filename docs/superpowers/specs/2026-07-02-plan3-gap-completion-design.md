# FireflyOS 计划 3 缺口增量补全设计

## 1. 目标与完成边界

本设计在 `codex/core-apps-power-imu` 现有实现上增量补全计划 3，不重写 UI Shell，不进入计划 4，也不提前合并到 `main`。

软件完成标准：

- Clock 页面提供真实可操作的单计时器和单秒表会话，离开页面后继续计时。
- Settings 页面产生的亮度、音量和时间设置统一通过固定容量命令队列，由 UI 主循环执行服务调用与持久化。
- PowerService 的完整状态结果进入运行时，ScreenOff、充电、低电量和温度保护不再只停留在单元接口。
- LightSleep 具备真实的准备、进入、唤醒和恢复代码路径，但发布入口继续受 BOOT、PWR、RTC 三项 100/100 真机门控制。
- QMI8658 在活动、亮屏、ScreenOff 和睡眠准备之间执行明确的功耗策略；未获得抬腕真机门数据前，不启用 IMU 中断作为 LightSleep 唤醒源。
- 新增页面只使用当前固件已嵌入的 Montserrat/LVGL Symbol 可显示字符，避免无中文字库时出现方框。
- 自动验证入口全部通过，预览与文档和实际代码一致。

以下结果必须由开发板实测产生，软件实现不能替代：BOOT/PWR/RTC 各 100 次唤醒、抬腕 100 次、30 分钟静置误唤醒、至少 3000 步参考对比、四种电源模式电流和 400mAh 续航。

## 2. 方案选择

采用增量补全：保留已验证的 TimeService、AlarmService、PowerService、MotionService 和旧闹钟编辑布局，只补充缺失的会话控制器、命令消费链和低功耗协调器。

不采用完整重写，因为现有闹钟、壁纸、触摸和覆盖层已经在真机运行，重写会扩大圆角布局和资源回归范围；也不采用只修改静态标签的表面修补，因为那无法满足服务化和离页继续运行要求。

## 3. 时钟会话设计

`CountdownTimer` 和 `StopwatchSession` 保留为不依赖 LVGL 的纯模型，ClockApp 增加固定页面状态与按钮回调：

- 计时器提供预设时长选择、开始/暂停、复位；只保存一个目标单调时刻。
- 秒表提供开始/暂停、复位；使用 `esp_timer_get_time()`，不保存圈速列表。
- ClockApp 的 `tick(now_ms, now_us)` 由 UI 主循环调用，负责刷新标签并在计时器首次到期时投递一次系统事件。
- 页面隐藏不停止模型；重新打开时直接按目标时间和累计微秒刷新。
- 所有按钮至少 48px，有效内容保持在 410 × 502 圆角安全区内。

计时器到期通过 EventBus/主循环显示系统覆盖层，不在模型或后台任务内调用 LVGL。

## 4. Settings 命令链设计

继续复用现有设置页对象和闹钟编辑圆角常量，但回调不直接修改硬件或写 Preferences：

- `SettingsCommandType` 覆盖亮度、音量、设置本地时间、重载 RTC、自动息屏和闹钟保存请求。
- LVGL 回调只采集值并投递命令。
- `firefly_process_settings_commands()` 在 UI 主循环消费命令，调用 TimeService、PowerService、AlarmService 和板级适配器。
- Preferences 写入集中在命令消费者的存储辅助函数，SettingsApp 不包含 Preferences 依赖。
- 队列满时保留旧值，并在串口诊断计数中记录失败，不静默写入部分状态。

AudioService 尚未进入本阶段，因此音量只保存系统逻辑值，不虚构音频硬件输出。

## 5. 电源与 LightSleep 设计

运行时每次评估都调用 `PowerService::evaluate()`：

- ThermalProtection 优先，禁止手电筒并限制高亮操作。
- Charging 保持充电覆盖层逻辑，不自动进入低功耗。
- Saver/LowBattery/CriticalBattery 通过状态快照和控制中心提示；CriticalBattery 降低亮度上限，但不在未确认时自动关机。
- Active、IdleDim、Glance、ScreenOff 继续驱动现有页面和亮度流程。

新增 `LightSleepCoordinator`，只由 UI 主循环调用：

1. 保存活动和设置聚合状态。
2. 关闭覆盖层动画和手电筒，暂停不需要的后台工作。
3. 将 QMI8658 切到适合当前已验证唤醒矩阵的模式。
4. 配置已验证的 ESP32/PMU/RTC 唤醒源并关闭显示。
5. 调用 LightSleep。
6. 唤醒后按 I2C、PMU/RTC/IMU、显示、触摸、Shell 路由顺序恢复。

发布代码只有在 `PowerService::canEnterLightSleep()` 返回真时才能进入上述流程。验证数据默认仍为 0/100，因此当前固件继续采用 ScreenOff，不会因补全代码而冒险失去唤醒能力。

## 6. QMI8658 功耗与唤醒设计

- Active/Activity 页面：加速度计和陀螺仪正常采样。
- 普通亮屏且非活动页：维持计步所需采样，但由 MotionService 节流无效读取。
- ScreenOff 且 CPU 运行：保持抬腕判定所需陀螺仪，不切换到会关闭陀螺仪的低功耗配置。
- 真正进入 LightSleep 前：只有 IMU 中断通过成功率门后才配置中断唤醒；在此之前 IMU 不是 LightSleep 唤醒源。
- 唤醒恢复失败时将 Motion capability 标为不可用，Activity 页面显示降级状态，其他应用继续运行。

增加串口诊断输出，用于真机确认地址、初始化结果、有效样本数、无效样本数、步数和抬腕事件，不输出高频原始样本洪流。

## 7. 字体与显示设计

当前未提交授权中文字体，`LV_FONT_DEFAULT` 是 Montserrat 14。为保证当前固件可读，本次将 Calendar、Tools、电源菜单和新增状态文案改为英文 ASCII；正式中文字体仍按已有资源流程在后续替换，不把大型字体临时塞入固件。

预览继续保留中文说明文字，但 410 × 502 表盘内部文案与固件实际英文一致。

## 8. 错误处理与降级

- RTC 无效：显示明确状态，允许手动设置或重载，不生成假日期。
- 命令队列满：拒绝新命令并记录诊断计数。
- LightSleep 门未通过或准备钩子失败：停留在 ScreenOff，并恢复已暂停资源。
- QMI8658 初始化或恢复失败：禁用 Motion capability，不阻塞 UI、时间或电源功能。
- Preferences 写入失败：运行时状态继续有效，下一保存周期重试。
- 计时器到期事件队列满：保持到期待发布状态，下一 UI tick 重试，避免丢失提醒。

## 9. 测试与验收设计

所有行为修改遵循红—绿—重构：

- 核心草图测试计时器暂停/恢复/单次到期、秒表离页、Settings FIFO 与溢出、完整电源优先级、LightSleep 门和恢复失败、Motion 模式策略。
- Python 仓库契约检查 ClockApp 存在交互回调、Settings 运行时消费者、主工程不绕过 LightSleep 门、后台代码不调用 LVGL、中文固件字符串不落入无字体页面。
- 每批运行 FireflyCoreTests 编译和完整 Firefly 固件编译。
- 最终运行 `tools/verify_all.ps1`，记录 Flash/RAM。
- 真机验收文档提供逐项步骤和记录字段，未执行项保持“待真机”，不填造成功率或电流。

## 10. 非目标

- 不实现多计时器、圈速历史、复杂日程编辑、手机应用、网络同步或音频播放。
- 不加入未经授权的中文字体或正式角色美术。
- 不在没有 100/100 必需唤醒证据时启用发布版 LightSleep。
- 不合并 `codex/core-apps-power-imu` 到 `main`，除非用户后续明确要求。
