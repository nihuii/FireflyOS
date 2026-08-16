# 计划 6 Task 1—5 缺口修补设计（A+）

## 1. 目标与边界

本轮在 `codex/wifi-weather-ota` 工作树中补齐计划 6 Task 1—5 已审查确认的代码缺口。采用 A+：保留现有 `WifiService`、`WeatherService`、`TimeService`、`BulkTransferService` 和 Android 伴侣应用边界，以协议 v2 和局部状态机修补完成闭环，同时吸收统一期限、显式命令结果和幂等清理三项协调层原则。

本轮不实现 Task 6 及以后功能，不提交、合并或推送，不删除或覆盖 `image/图片生成提示词`。自动测试、编译和 APK 生成只作为自动验证证据，功耗、隐藏网络、拔卡及真实 Wi-Fi/HTTP 传输继续保留为真机 `PENDING`。

## 2. 共同行为约束

1. 所有长会话同时维护绝对期限和空闲期限。空闲活动只能延长空闲期限，不能延长 15 分钟绝对期限。
2. 每条 BLE 控制命令都产生显式结果；结果不得依赖服务状态是否发生变化。
3. 取消、超时、断连、低电量、SD 拔出和传输错误共用幂等资源清理入口。
4. 所有跨核共享状态由静态互斥量或临界区保护；LVGL 仍只由 UI 主循环访问。
5. 新增数据结构和缓冲区均为固定容量，不为协议解析、天气快照或传输状态引入无界容器。

## 3. Task 1：Wi-Fi 会话与电源门禁

`PowerService::allowsWifiSession()` 先处理有效电池的临界电量：电量小于等于 5% 时，无论是否充电或检测到 VBUS，均拒绝新 Wi-Fi 会话；随后才处理充电豁免和 15% 高功耗门禁。

`WifiService` 对 Station 与 SoftAP 使用同一个 `session_started_ms_` 绝对起点。`tick()` 在处理具体模式前检查 Transfer/OTA 是否达到 15 分钟上限；达到后统一关闭射频、清空 purpose 并记录可查询的停止原因。普通 NTP/Weather 继续使用 60 秒空闲关闭，Station 连接仍为 15 秒超时。

`BulkTransferService` 同时检查自己的 `session_started_ms_` 和 Wi-Fi purpose 是否仍有效。任一绝对期限到达时进入 `Cancelled/Timeout`，删除临时文件并停止 HTTP、SoftAP/Station 和 SD 会话。

## 4. Task 2：无 RTC 依赖的安全 BLE 配网

### 4.1 配网 payload v2

认证后的 `WifiProvision` 业务 payload 使用以下固定格式：

```text
byte 0       schema = 2
byte 1       ttl_seconds，范围 1..60
byte 2..9    8-byte cryptographic nonce
byte 10      ssid_length，范围 1..32
bytes ...    SSID UTF-8 bytes
next byte    password_length，范围 0..64
bytes ...    password bytes
```

整个业务帧继续由已 bonding 会话的 HMAC 认证保护。手表以收到帧时的 `millis()` 建立最多 60 秒确认期限；nonce 重放缓存也使用单调毫秒期限和 wrap-safe 比较，因此首次开机不依赖 RTC。Android 默认只发送 v2。

现有 v1 绝对 Unix 到期格式只在设备 RTC 有效时保留兼容；RTC 无效时明确拒绝 v1，不降低旧格式安全性。

### 4.2 敏感状态清理

`WifiService` 提供无 UI 副作用的 `clearSensitiveState()`，停止射频、清除 NVS 凭据、内存密码、待确认凭据和 nonce 缓存。忘记网络流程调用该入口并保留自己的结果状态；后续 Task 9 恢复出厂协调器也可直接调用。当前不提前增加 Task 9 的恢复出厂页面或全系统擦除逻辑。

## 5. Task 3：NTP、TimeService 与天气超时

`TimeService` 为 epoch、基准 tick、有效标志和延迟网络时间增加静态递归互斥量。所有公开读写方法在同一锁策略下访问状态；写 RTC 仍经既有 `ClockDevice`/I2C 同步边界。

网络时间流程增加单一 `stopNetworkTimeRequest()` 清理函数。以下路径全部调用它：SNTP 成功、Wi-Fi purpose 失活、闹钟响铃导致延期、请求失败或会话超时。清理函数先 `esp_sntp_stop()`，再清配置标志，允许重复调用。

天气 HTTPS 请求使用单一 15 秒截止预算。HTTP 建连后，读取阶段只使用剩余预算；任何阶段到达截止点立即结束请求并释放 Weather Wi-Fi purpose。8KB 响应上限保持不变。

## 6. Task 4：正式天气 A8 图标

用户已批准 `docs/UI预览/05-天气与更新/天气图标母图-v1.png`。母图实际为 1254×1254 的 4×3 非方形网格，本轮保留原文件不变；按每行、每列的比例边界确定性裁切，提取前景包围盒后等比例居中缩放到 48×48，并转换为 LVGL 8.3.11 可用的单色 A8 固定资源。

12 个资源依次表示晴、夜间晴、少云、阴、阵雨、大雨、雷暴、雪、雾、风、小雨和雨夹雪。生成的资源独立存放在 WeatherApp 资源文件中，使用 `LV_IMG_DECLARE`/`lv_img_set_src` 接入；天气码映射返回资源描述符，不再返回 `SUN`、`RAIN` 等占位文字。图标对象不接收点击事件，不改变既有 410×502 安全区和刷新按钮 48px 以上触摸目标。

## 7. Task 5：传输协商、取消与清理

### 7.1 BulkTransfer v2

Start 请求格式：

```text
byte 0       schema = 2
byte 1       opcode = 1 (Start)
byte 2..3    request_id uint16 little-endian
byte 4       flags，bit0 = prefer shared LAN
byte 5..12   declared_size uint64 little-endian
byte 13..44  SHA-256 binary
byte 45      managed_path_length
bytes ...    managed UTF-8 path
```

Cancel 请求格式为 `[schema=2, opcode=4, request_id:u16]`。路径仍限定在 `/FireflyOS/Themes`、`Pictures`、`Music`、`Updates`；单文件范围为 1 字节到 64MB。

手表在生成 token 前检查音频/OTA互斥、电量、SD、规范化路径、声明大小和剩余空间，并将协商的路径、大小及 SHA-256 固定到会话快照。HTTP headers 必须与协商元数据完全一致，避免一次 token 上传另一文件。

Ready/Status 响应携带相同 `request_id`。服务维护固定宽度的结果 generation；即使状态仍为 Error 或 Busy，每个请求也会产生新的可发送结果，Android 不再因“状态未变化”无限等待。

### 7.2 Android 取消闭环

Android 在文件准备后发送包含完整元数据的 Start。界面增加至少 48px 的取消按钮；按钮在哈希、等待 endpoint、SoftAP 连接和上传阶段均可用。取消动作停止当前协程、释放 Android Network，并在认证 BLE 仍连接时发送 Cancel。权限拒绝、SoftAP 获取失败和本地文件失效也走同一取消入口。

### 7.3 错误保真和孤儿文件

`writeChunk()` 已产生 LowPower、SdUnavailable 或 WriteFailed 时，HTTP 层读取当前 sink 失败原因并保留它，不再无条件覆盖成 WriteFailed。

`BulkTransferStorage` 增加 `cleanupOrphanParts()`。启动挂载 SD 后，只扫描四个受管根目录并删除名称以 `.part` 结尾的普通临时文件；不递归越出受管目录，不删除正式文件。清理失败记录降级状态，但不阻止系统其他模块启动。

## 8. TDD 与验证

按以下顺序执行红—绿—重构：

1. C++：临界电量充电场景、SoftAP 15 分钟硬截止、配网 v2/重放/v1 RTC 边界。
2. C++：TimeService 跨核锁契约、SNTP 清理契约、天气总超时预算。
3. Kotlin：配网 v2 编码、Bulk v2 Start/Cancel/响应 request ID。
4. C++：协商前空间检查、元数据绑定、重复 Busy 显式结果、错误保真、孤儿 `.part` 清理。
5. Kotlin：用户取消、权限拒绝和上传取消状态机；Uploader 1KB、1MB、32MB 流式输入测试。
6. Python/资源契约：12 个 48×48 A8 图标、WeatherApp 使用 `lv_img`、禁止占位字符串。

最终运行仓库 Python 测试、Android `testDebugUnitTest` 与 `assembleDebug`、FireflyCoreTests、AudioProbe 和 Firefly 主固件编译。所有真机项目保持 `PENDING`，直到按既定分支顺序在设备上执行。

## 9. 完成判据

- 无 RTC 的首次启动可通过认证 BLE 完成 60 秒内的配网确认，同时 nonce 重放仍被拒绝。
- 任意 Transfer/OTA Wi-Fi 模式都不能超过 15 分钟绝对期限；5% 及以下拒绝全部新 Wi-Fi。
- NTP 不在会话释放后继续后台运行，TimeService 不存在无保护的跨核共享字段。
- HTTPS 从开始到结束共享一个 15 秒预算。
- WeatherApp 使用已批准母图转换出的 12 个正式 48×48 A8 图标。
- 传输在 BLE 批准前完成路径、大小和空间检查；用户取消、超时、断连、低电量和拔卡均删除 `.part` 并释放网络资源。
- 每个 BulkTransfer 命令均有可关联的显式结果，失败原因保持真实。
- 自动测试和三套固件/探针编译均有新鲜输出，但不被表述为真机通过。
