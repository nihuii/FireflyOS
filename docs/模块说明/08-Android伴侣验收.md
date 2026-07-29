# FireflyOS BLE 与 Android 伴侣验收

## 1. 文档范围与结论

本文记录计划 5 Task 1～9 的软件实现范围、Task 10 Gate D 的自动验证证据，以及必须在 Android 手机和 FireflyOS 开发板上完成的真机验收矩阵。

截至 2026-07-29：

- Task 1～9 的协议、固件服务、Android 工程和伴侣功能已经形成软件实现，并通过本页列出的自动测试与编译检查。
- Android Debug APK 和三项 Arduino 草图均已成功编译；这些结果只证明源码可以通过相应工具链，不代表 BLE 射频、触控、音频、显示、电源或长时间运行已经在真机上通过。
- Gate D 真机矩阵尚未执行，因此计划 5 的 Gate D 状态为 **PENDING**，不能标记为真机验收通过。
- Android 解除绑定入口、`UnpairRequest`/`UnpairConfirm`、待解绑 token 跨进程持久化、ACK 写入失败/断线恢复及双方 ACK 后提交路径已完成代码审计和自动测试；BLE bond 的实际清除与重新配对仍必须在真机复核。

本文中的状态含义：

- `PASS-AUTO`：有本次自动测试或编译输出作为证据。
- `PENDING`：尚无满足验收要求的真机记录。
- `FAIL`：已执行但未达到预期；必须记录日志、环境和复现步骤。

## 2. Task 1～9 软件实现范围

| Task | 已形成的软件能力 | 自动证据边界 |
| --- | --- | --- |
| 1 | 冻结 BLE 协议 v1：固定 GATT UUID、11 字节帧头、CRC16、序号、ACK、动态 MTU 分片、消息类型、Error 错误码与黄金帧。 | Python 黄金帧、CRC、重放、分片、未知类型 Error 和 1025 字节拒绝契约已覆盖。 |
| 2 | 固件侧无堆 `FrameCodec`，逻辑 payload 上限 1024 字节，ATT 块上限 180 字节。 | Python 契约和 `FireflyCoreTests` 编译通过；未代替开发板运行断言。 |
| 3 | BLE Peripheral HAL 与 `ConnectivityService`，包含广播/重连策略、MTU 23 回退、最多 147 片的动态分片、单帧 ACK 重试缓存、串行接收、ACK 超时与有限重试、断连状态传播。 | 固件服务契约和三项草图编译通过；50 次重连及内存趋势待真机。 |
| 4 | Secure Connections、配对确认、128-bit app token、敏感消息 HMAC-SHA256 截断 8 字节、重放拒绝、Unauthorized 响应、连续失败断开和双方确认后解绑。 | 安全与事务契约已覆盖，包括配对记录待确认/已确认阶段、启动回滚、待解绑 token 跨进程恢复及 ACK 写入失败时保留恢复能力；错误 HMAC、旧序号、未配对修改及 bond 实际清除仍待真机。 |
| 5 | Android 伴侣正式 UI 预览、离线态和权限边界已获用户批准。 | 预览与审批记录契约通过；410×502 手表端视觉及 Android 实机易用性仍待人工验收。 |
| 6 | Android Views 工程、BLE/日历/普通通知最小权限声明，以及独立的系统通知监听授权入口。 | Android 工程契约、单元测试和 Debug APK 编译通过；各 Android 版本权限弹窗待真机。 |
| 7 | Kotlin `FrameCodec`、15 秒定向扫描、MTU 185 请求与超时回退、MTU 23 下 1024 字节载荷的 147 片发送、`BluetoothGatt` 客户端、串行 GATT 操作、CCCD 启用、连接状态流和可靠发送队列。 | Kotlin 单元测试和 Python 源码契约通过；通用 BLE 工具互操作、协商 MTU 和 20/50 次循环待真机。 |
| 8 | 通知监听、UTF-8 有界摘要、相同 key 更新、删除同步、20 条手表缓存，以及需要 ACK 的有界串行重发。 | 通知编码、认证、队列和固件缓存契约通过；授权、撤销、重连与通知风暴待真机。 |
| 9 | 四类设置独立 revision/changed_at 冲突处理、原子设置快照、已缓存/内置主题校验与主循环动态换肤、天气 3/24 小时策略、未来 7 天最多 8 条日程摘要、媒体控制、显式错误状态与双向找设备。 | 88 项 Android 单元测试及相关 Python/固件契约覆盖纯逻辑；系统日历、媒体会话、动态换肤视觉、前后台找设备和低电量策略待真机。 |

软件实现采用以下固定边界：

- 协议逻辑 payload 最大 1024 字节，ATT 单块最大 180 字节；180 字节块最多 7 片，MTU 23 回退时最多 147 片。固件只缓存一个待 ACK 逻辑帧，Android GATT 队列固定最多 147 个操作。
- 固件 BLE 接收队列和业务分发队列各固定 4 项；设置固定 4 类，每类值最多 256 字节。
- 手表通知摘要固定最多 20 条；日程摘要固定最多 8 条。
- ACK 超时为 2 秒，最多重试 3 次；连续 5 次认证失败触发断开。
- 所有输入在复制、持久化或分发前检查长度、版本、CRC、序号及认证边界。

## 3. Android 构建兼容偏差与国内网络配置

计划原文指定 AGP `9.2.0`，但当前 Android Studio 所支持的最高版本为 AGP `9.1.0`。工程因此固定为：

| 项目 | 当前配置 | 说明 |
| --- | --- | --- |
| Android Gradle Plugin | `9.1.0` | 为匹配当前 Android Studio 的兼容上限，从计划原 `9.2.0` 下调。 |
| Gradle Wrapper | `9.3.1` | 与当前 AGP 工程组合已完成单元测试和 Debug 构建。 |
| Kotlin | AGP 9 内建 Kotlin | 不再声明独立 Kotlin Android 插件版本，避免与 AGP 内建 Kotlin 冲突。 |
| SDK | compile/target 36，min 26 | Android 12 及以上走 Nearby Devices 权限；旧版本保留兼容权限分支。 |

中国大陆无 VPN 环境下，仓库优先级固定为：

1. Gradle Wrapper：腾讯云 `https://mirrors.cloud.tencent.com/gradle/gradle-9.3.1-all.zip`。
2. 插件与依赖：阿里云 `google`、`public`、`gradle-plugin` 镜像。
3. 官方 `google()`、`mavenCentral()`、`gradlePluginPortal()` 仅作为后备源。

该偏差是工具链兼容修正，不改变 BLE 协议 v1、应用 ID、SDK 级别或业务消息格式。升级 Android Studio 后若重新评估 AGP 9.2，必须先完整重跑 Android 单元测试和 APK 编译。

## 4. 2026-07-29 自动验证证据

### 4.1 FireflyOS 全量入口

执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
```

结果：

| 检查 | 结果 | 状态 |
| --- | --- | --- |
| Python 契约测试 | `Ran 124 tests`，`OK` | PASS-AUTO |
| FireflyCoreTests 编译 | Flash 919,861 B / 4,718,592 B；全局 RAM 39,728 B / 327,680 B | PASS-AUTO |
| AudioProbe 编译 | Flash 957,125 B / 4,718,592 B；全局 RAM 40,440 B / 327,680 B | PASS-AUTO |
| Firefly 完整固件编译 | Flash 3,068,497 B / 4,718,592 B；全局 RAM 105,560 B / 327,680 B | PASS-AUTO |
| Markdown 占位标记检查 | `no placeholders in docs markdown` | PASS-AUTO |

早期沙箱内执行 Python 全集时，2 项字体收集测试曾因工作区临时目录 ACL 返回 `PermissionError`；本轮重新验证后 124 项全部通过。该记录属于执行环境差异，不应改写为断言失败，也不应隐藏首次受限结果。

`FireflyCoreTests` 和 `AudioProbe` 的结果均为“草图编译成功”，未将草图烧录到开发板运行；完整 Firefly 结果也只是固件编译成功。

### 4.2 Android

执行：

```powershell
cd AndroidCompanion
$env:JAVA_HOME='C:\APP\Android\Android Studio\jbr'
.\gradlew.bat testDebugUnitTest assembleDebug
```

结果：

- 17 个测试套件，共 88 项测试，0 failure、0 error、0 skipped。
- `assembleDebug` 输出 `BUILD SUCCESSFUL`，生成 `app/build/outputs/apk/debug/app-debug.apk`。
- 本轮 Debug APK SHA-256 为 `5E4C92E7AB28D1BA95FF6D4D432F88F1122D59906D73992B94B734DCE4ABFAF4`。
- 编译器报告 Android 旧版 `BluetoothGatt` write API 的弃用警告；当前低版本兼容分支仍可编译，后续迁移时必须保留 API 等级分支测试。

以上为 JVM 单元测试与 Debug 构建证据，不包含 Android 仪器测试、蓝牙射频互操作或真机权限弹窗验收。

## 5. 架构与 UI 安全边界

- FireflyOS 当前固定使用 LVGL 8.3.11，不得按 LVGL 9 API 假设修改。
- LVGL 只能由 Arduino `loop()` 所在 UI 主循环访问。BLE 回调、FreeRTOS 后台任务和 GATT 解析只写入固定容量队列或投递系统事件，不得调用 `lv_obj_*`、`lv_label_*`、`lv_img_*` 等 UI API。
- 手表正式 UI 以 410×502 圆角屏安全区为边界，重要信息、返回操作和主要按钮不得进入圆角裁切区。
- 所有可触摸操作目标至少 48 px；Android 端关键按钮至少 48 dp。
- 手机断连只改变伴侣连接状态；本地时间、闹钟、活动、音乐、录音和缓存读取必须继续独立运行。
- 网络、日历、通知和媒体会话均属于可选手机能力；权限拒绝或服务不可用时应显示明确错误，不能让手表本地核心功能失效。

## 6. Gate D 真机验收矩阵

执行前准备：

1. 固定 FireflyOS 固件哈希、Android APK 哈希、开发板型号、手机型号、Android 版本和测试日期。
2. 清除上一次 BLE 日志计数，但不要删除用户本地闹钟、媒体或活动数据。
3. 串口持续记录连接状态、错误码、认证失败计数、队列满计数、free heap 和 minimum free heap。
4. 每项保留操作视频或关键截图，并在“实际记录”栏填写日志文件名；没有证据时状态保持 `PENDING`。

| ID | 场景与执行步骤 | 通过标准 | 状态 | 实际记录 |
| --- | --- | --- | --- | --- |
| D-01 | 使用 nRF Connect、LightBlue 或同类通用 BLE 工具发现固定 Service UUID；写入协议文档的空 Hello 黄金帧并读取 ACK；再写中文通知黄金帧。 | UUID、字节序、CRC、消息类型和 ACK 与协议文档逐字节一致，设备不重启。 | PENDING | 未执行 |
| D-02 | 已绑定手机执行 20 次“断开后立即恢复”循环，每次确认订阅恢复、Hello/ACK 和一个设置读取；随后执行 50 次连接/断开压力循环。 | 20/20 功能恢复，50/50 无卡死、无必须重启、无残留错误状态。 | PENDING | 未执行 |
| D-03 | 用测试客户端分别发送错误 HMAC、被篡改 payload、已成功处理的旧序号，以及连续 5 次认证失败。 | 错误消息不进入业务层；旧序号不重复执行；达到阈值后断开；重新合法配对可恢复。 | PENDING | 未执行 |
| D-04 | 清除手机绑定或使用从未配对的第二台手机，发送亮度、音量、主题和闹钟 `SettingsSet`。 | 所有修改均被拒绝，NVS 与实时设置保持原值，并返回可诊断的未授权错误。 | PENDING | 未执行 |
| D-05 | 播放本地音乐、启动录音、设置近期闹钟并积累活动数据；随后关闭手机蓝牙至少 10 分钟并等待闹钟触发。 | 本地时间继续走时，闹钟正常触发，活动继续累计，音乐/录音不因手机断连被强制停止。 | PENDING | 未执行 |
| D-06 | 在 Android 12、13 及可用的更高版本上分别测试 BLE 扫描权限、通知监听权限、Android 13+ 通知权限和日历权限的授予、拒绝、再次进入；执行双端解除绑定并重新配对。 | 权限请求只在用户主动操作时出现；拒绝只禁用对应能力并解释原因；扫描 15 秒停止；解除绑定入口、手表确认、`UnpairConfirm`、token/bond 清理和重新配对完整可用。 | PENDING | 未执行；解绑代码审计与自动事务测试已完成，bond 清理和重配待真机 |
| D-07 | 对闹钟、亮度、音量、主题分别制造手机和手表的交错修改：较新 `changed_at`、相同时间不同 revision、uint32 revision 回绕边界；重连后读取双方状态。 | 四类设置独立合并；最近明确操作获胜；无半包应用、无部分 NVS 快照；双方最终一致。 | PENDING | 未执行 |
| D-08 | 发送更新时间分别为当前、超过 3 小时、超过 24 小时的天气摘要；每次断开手机观察手表显示。 | 3 小时内正常；超过 3 小时标记数据较旧并保留；超过 24 小时视为过期；断连提示清晰。 | PENDING | 未执行 |
| D-09 | 手机上先确保无可用媒体会话，再由手表发送播放、暂停、上一首、下一首和音量命令；随后启动受支持播放器重复测试。 | 无会话时返回明确错误且应用不崩溃；有会话时命令只作用于当前会话，音量结果可观察。 | PENDING | 未执行 |
| D-10 | 前台和后台分别触发 FindPhone；测试通知权限已授予和拒绝两种情况，并手动取消。再从 Android 触发 FindWatch，测试正常电量、5% 及以下电量、BOOT/PWR 取消。 | FindPhone 最长 30 秒且可取消；后台按权限选择响铃或高优先通知并释放资源。FindWatch 正常电量声光 30 秒，极低电量仅闪光 5 秒且无音频，按键可立即取消。 | PENDING | 未执行 |
| D-11 | 在 D-02 的 50 次循环中，每次稳定连接后记录 free heap、minimum free heap、队列满计数；另持续运行通知和设置同步 30 分钟。 | 无持续单调内存下降，无队列计数持续增长，无 watchdog、崩溃或 BLE 服务失联。 | PENDING | 未执行 |
| D-12 | 在 410×502 实屏逐页检查配对覆盖层、控制中心、天气、日程、媒体与找设备状态；沿四角和底边反复点击、滑动，测量全部操作区域。 | 信息不被圆角裁切；主要控件完全位于安全区；所有触摸目标至少 48 px；无误触、死区或后台线程访问 LVGL 的异常。 | PENDING | 未执行 |

## 7. 真机记录模板

每轮测试至少记录：

| 字段 | 填写内容 |
| --- | --- |
| 固件标识 | Git commit 或未提交 diff 标识、编译时间 |
| Android 标识 | APK SHA-256、versionName/versionCode |
| 硬件 | 开发板批次、手机型号、Android 版本 |
| 配对状态 | 新配对、已绑定重连或解除绑定后重配 |
| 循环编号 | 例如 `07/50` |
| 资源数据 | free heap、minimum free heap、失败/丢弃计数 |
| 操作与预期 | 对应 D-xx 步骤及预期 |
| 实际结果 | PASS 或 FAIL；不得用“看起来正常”代替数据 |
| 附件 | 串口日志、Android logcat、截图或视频路径 |

只有 D-01～D-12 全部具有可复核记录且通过，才能把 Gate D 从 `PENDING` 改为真机通过。自动编译、模拟调用或源码审计均不能替代该结论。
