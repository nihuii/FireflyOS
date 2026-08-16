# BLE 协议、配对与通知

## 来源

- 固定提交：`e4fb0d1ff71bcc0330b507fa90c653be9929e611`
- 主要路径：`protocol/*`、`hal/BlePeripheralDevice.*`、`services/ConnectivityService.*`、`CompanionSyncService.*`、`NotificationService.*`
- 取回示例：`git show "e4fb0d1:libraries/FireflyOS/src/firefly/protocol/ProtocolTypes.h"`

## 复用等级

**需要重构后复用。** 固定帧格式、分片上限、严格 ACK、显式配对和敏感帧认证值得保留；协议面已经覆盖大量业务，重启时应先冻结最小 Hello/Pair/Ack，再逐类增加消息。

## 模块定位

手表作为 BLE GATT 外设，暴露命令写入、事件通知和 Bulk 控制特征。`ConnectivityService` 负责连接、分片重组、重放门、认证、ACK 重试和配对状态；业务帧通过固定队列交给 `CompanionSyncService` 或 `NotificationService`。

## 职责与边界

- `FrameCodec`：小端 11 字节头、CRC16、最大 1,024 字节载荷。
- `ConnectivityService`：接收队列 4、分发队列 4、ACK 超时 2 秒、最多重试 3 次。
- `MessageAuthenticator`：16 字节应用 token，HMAC-SHA256 截断为 8 字节认证标签。
- `PairingStore`：配对记录先处于 provisional，确认 ACK 完成后才成为稳定会话。
- `CompanionSyncService`：设置、天气、日历、查找设备、媒体命令等有界业务模型。
- `NotificationService`：最多保留固定数量摘要，不能把手机完整通知内容无限复制到手表。

BLE 链路加密和应用层认证承担不同职责：bond/MITM 保护无线连接，应用 token 和序列号保护业务帧。不能因为已加密就跳过敏感帧认证。

## 数据流与线程边界

```text
BLE 回调 ── enqueueReceived(最多 180 B) ── 接收队列 4
                                                │ service()
                                                ▼
FrameCodec → 分片重组 → 认证 → 重放检查 → ACK
                                                │
                                                ▼
                                      分发队列 4 / EventBus
                                                │ UI 主循环
                                                ▼
                                  通知、设置、天气等业务快照
```

敏感帧必须先认证，再决定重复帧 ACK；否则攻击者可以通过伪造重复 sequence 获得成功响应。只有帧成功发布到业务队列后才能推进最新入站 sequence。

## 关键接口

| 项目 | 固定边界 |
|---|---:|
| 帧头 | 11 B，magic `0x46 0x46`，version 1 |
| 最大 payload | 1,024 B |
| 最大 ATT chunk | 180 B |
| 默认 ATT chunk | 20 B |
| 接收/分发队列 | 各 4 项 |
| ACK | 2,000ms，最多 3 次重试 |
| 应用 token / auth tag | 16 B / 8 B |
| 连续认证失败 | 5 次后关闭敏感会话 |

## 精选代码

来源：`libraries/FireflyOS/src/firefly/protocol/ProtocolTypes.h`，协议容量。

```cpp
static constexpr size_t kHeaderSize = 11;
static constexpr size_t kMaxPayload = 1024;
static constexpr size_t kMaxEncodedFrame = kHeaderSize + kMaxPayload;
static constexpr size_t kMaxAttChunk = 180;
static constexpr size_t kFragmentMetadataSize = 2;
static constexpr size_t kMaxFragmentData =
    kMaxAttChunk - kHeaderSize - kFragmentMetadataSize;
```

来源：`libraries/FireflyOS/src/firefly/protocol/FrameCodec.h`，符号 `Frame`。

```cpp
struct Frame {
    MessageType type = MessageType::Error;
    uint8_t flags = 0;
    uint16_t sequence = 0;
    uint16_t payload_length = 0;
    uint8_t payload[kMaxPayload]{};
};
```

来源：`libraries/FireflyOS/src/firefly/services/ConnectivityService.h`，有界队列与重试策略。

```cpp
static constexpr uint32_t kAckTimeoutMs = 2000;
static constexpr uint8_t kMaxRetries = 3;
static constexpr uint8_t kReceiveQueueCapacity = 4;
static constexpr uint8_t kDispatchQueueCapacity = 4;
static constexpr uint8_t kMaxAuthenticationFailures = 5;
```

来源：`libraries/FireflyOS/src/firefly/services/CompanionSyncService.h`，设置快照。

```cpp
struct CompanionSettingsSnapshot {
    static constexpr uint16_t kSchemaVersion = 1;
    static constexpr uint8_t kCapacity = 4;

    uint16_t schema_version = kSchemaVersion;
    VersionedCompanionSetting settings[kCapacity]{};
    uint8_t valid[kCapacity]{};
};
```

设置同步采用完整快照和两阶段提交：先解析到 staged 快照，持久化成功后才替换运行时快照。报警设置的 value 只表示一个显式变更槽位，合并时不能清空其他闹钟。

## 源码与测试映射

- 协议：`protocol/ProtocolTypes.h`、`FrameCodec.h/.cpp`。
- BLE HAL：`hal/BlePeripheralDevice.h/.cpp`。
- 连接与配对：`services/ConnectivityService.h/.cpp`。
- 业务同步：`services/CompanionSyncService.h/.cpp`、`NotificationService.h/.cpp`。
- 存储：`services/StorageService.*` 中的 `PairingStore` 和设置快照。
- 测试：`test_protocol_frames.py`、`test_connectivity_service_contract.py`、`test_notification_sync_contract.py`、`test_pairing_security_contract.py`、核心固件测试。
- 文档：`docs/模块说明/07-BLE协议.md`。

## 验证边界

Python 和 Android 单元测试覆盖黄金帧、CRC、分片、ACK、认证、配对和重放契约；不能证明天线、实际 MTU、系统配对弹窗、长连接功耗、断线重连和跨手机兼容。正式 BLE 真机项目仍为 `PENDING`。

## 已知问题

- `ConnectivityService` 同时负责链路、协议、认证、配对和 ACK，状态较多；重启时宜拆成 codec、session、pairing 三个独立单元。
- 1,024 字节业务帧不适合传大文件；必须走临时 Bulk 通道。
- ACK 必须是 `MessageType::Ack + IsAck + 空 payload`，宽松接受会引入协议歧义。
- 测试夹具中的私钥、应用 token 和生产 OTA 信任锚不是同一种密钥，不能混用。
- 通知隐私默认应隐藏内容，不能因为手机已授权通知访问就默认同步全文。

## 基于 main 的复用步骤

1. 冻结 Hello、PairRequest、PairConfirm、Ack 和 Error 五类最小消息。
2. 用同一黄金帧在 C++/Kotlin 两端验证小端、CRC 和 payload 上限。
3. 真机验证 SC、MITM、bond、明确用户确认和取消路径。
4. 加入敏感帧认证、严格 ACK 和重放门，执行断线/重复/坏标签测试。
5. 每次只增加一种业务消息；通知、设置、媒体、天气分别关闭真机门槛后再继续。

