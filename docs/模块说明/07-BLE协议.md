# FireflyOS BLE 协议 v1

本文冻结手表与 Android 伴侣应用之间的 BLE 线协议。协议使用固定容量、小端整数、显式长度和 CRC；任何实现不得依赖动态内存来接收完整逻辑帧。

## 1. GATT 服务

| 用途 | UUID | 属性 |
|---|---|---|
| FireflyOS Service | `7b7f0001-4f53-4653-8000-ff1e00000001` | Primary service |
| Command RX | `7b7f0002-4f53-4653-8000-ff1e00000001` | Write / Write Without Response |
| Event TX | `7b7f0003-4f53-4653-8000-ff1e00000001` | Read / Notify |
| Bulk Control | `7b7f0004-4f53-4653-8000-ff1e00000001` | Write / Notify |

手表是 Peripheral/GATT Server，手机是 Central/GATT Client。Command RX 承载控制帧，Event TX 承载 ACK、状态和错误，Bulk Control 预留给需要节流的多片会话。

## 2. 帧格式

| Offset | Size | 字段 |
|---:|---:|---|
| 0 | 2 | Magic：`0x46 0x46`（ASCII `FF`） |
| 2 | 1 | 协议版本：`0x01` |
| 3 | 1 | Message type |
| 4 | 1 | Flags |
| 5 | 2 | Sequence，小端 |
| 7 | 2 | Payload length，小端 |
| 9 | 2 | CRC16，小端 |
| 11 | N | Payload，逻辑载荷最大 1024 字节 |

CRC 使用 CRC16-CCITT-FALSE：多项式 `0x1021`、初值 `0xFFFF`、不反射、不异或输出。计算输入依次为字节 `0..8` 和 payload；CRC 字段自身不参与计算。解码器必须先验证物理长度和 1024 字节上限，再复制 payload。

Flags：

- `0x01 ACK_REQUIRED`：接收端必须回复 ACK。
- `0x02 IS_ACK`：该帧是 ACK。
- `0x04 FRAGMENT`：payload 前两字节为 `fragment_index`、`fragment_count`。
- `0x08 LAST_FRAGMENT`：仅允许出现在最后一片。

## 3. ATT 分片与重组

单次 ATT 写入/通知上限为 `min(180, negotiated_mtu - 3)`。当编码帧超过该值时，每片仍是一个完整的 v1 帧，并使用相同 message type 和 sequence；payload 前两字节固定为从 0 开始的片序号和总片数，后接片数据。180 字节块可携带 167 字节片数据，1024 字节逻辑载荷需要 7 片；默认 MTU 23 的 20 字节 ATT 块每片可携带 7 字节片数据，同一载荷需要 147 片。因此 v1 的固定分片上限是 147，而不是只按理想 MTU 设置为 7。

Android 连接后先请求 MTU 185，再发现服务；协商失败或 2 秒内没有回调时按默认 MTU 23 继续。收发两端都必须以本次连接的实际 MTU 动态计算分片，不能把 180 字节当作所有连接都可用的固定写入长度。

接收端只保留一个固定容量重组会话。片必须严格有序，message type、sequence 和总片数必须一致；最后一片必须同时设置 `LAST_FRAGMENT`。任一不一致、缺片、超出 147 片或重组后超过 1024 字节都会清空该会话并返回错误。发送端只保留一个待 ACK 的完整逻辑帧，重试时按当前 MTU 重新编码并逐片发送，不为 147 片预留二维缓存。完整帧重组成功后，sequence 才进入防重放窗口。

Sequence 是 16 位无符号计数器，按模 65536 前进。与最近已接受序号相同或位于其后退半区（差值为 0 或不小于 `0x8000`）的完整帧视为重复/旧帧。分片共用同一 sequence，不因中间片触发重复判断。

## 4. 消息类型

| 值 | 名称 | 方向/用途 |
|---:|---|---|
| `0x01` | Hello | 双向能力与版本握手 |
| `0x02` | PairRequest | 手机发起配对 |
| `0x03` | PairConfirm | 配对确认 |
| `0x04` | Ack | 确认帧 |
| `0x05` | UnpairRequest | 请求在手表端显示解绑确认 |
| `0x06` | UnpairConfirm | 解绑结果 |
| `0x10` | DeviceState | 手表状态摘要 |
| `0x11` | SettingsGet | 手机读取设置 |
| `0x12` | SettingsSet | 手机修改设置 |
| `0x20` | NotificationPush | 通知摘要 |
| `0x21` | NotificationDismiss | 手机通知消失 |
| `0x30` | WeatherUpdate | 天气摘要 |
| `0x31` | CalendarUpdate | 日程摘要 |
| `0x40` | MediaCommand | 媒体控制 |
| `0x41` | FindPhone | 手表找手机 |
| `0x42` | FindWatch | 手机找手表 |
| `0x50` | WifiProvision | Wi-Fi 配置 |
| `0x60` | OtaControl | OTA 控制 |
| `0x7F` | Error | 明确错误响应 |

未知消息类型不会导致断连或重启；接收端返回 `Error`。需要 ACK 的发送项超时为 2 秒，最多重试 3 次；重复 ACK 不重复执行业务命令。

`Error` payload 固定为 3 字节：

```text
schema = 1
failed_message_type
error_code
```

错误码为：`1 InvalidPayload`、`2 NoActiveMediaSession`、`3 MediaAccessRequired`、`4 SecurityDenied`、`5 FindPhoneUnavailable`、`6 PersistenceFailure`、`7 Unauthorized`。认证失败必须返回 `Unauthorized`；Error 本身不要求 ACK，接收端消费后不得再回复 Error，以避免错误帧循环。

## 5. 配对和消息认证

新手机先发送 `PairRequest`，payload 是最多 32 字节的 UTF-8 设备名。手表生成并显示六位码；用户在手表上允许后，双方以 BLE Secure Connections、MITM 和 bonding 建立加密链路。成功后手表生成随机 128-bit app token，先以“待确认”阶段持久化，再通过加密链路的 ACK-required `PairConfirm` payload 发送。Android 只有在私有存储同步写入 token 成功后才回复 ACK；手表收到该 ACK 并把记录提升为“已确认”阶段后才进入已配对运行状态。启动时发现待确认记录必须清除该记录和 bond；发送失败、ACK 超时、连接中断或阶段提升持久化失败也必须回滚。PIN、token 和 Wi-Fi 密码不得进入日志。

敏感消息在原 payload 尾部追加 8 字节 HMAC-SHA256 截断标签。HMAC 输入依次是：

```text
protocol_version (1)
message_type (1)
flags (1)
sequence_le (2)
payload_body_length_le (2)
payload_body (N，不含认证标签)
```

密钥为 16 字节 app token。接收端先执行帧长度、版本与 CRC 检查；对敏感消息先要求链路已加密并以常量时间比较 HMAC，再检查完整逻辑帧的序号新鲜度。该顺序确保伪造的旧序号不会绕过认证失败计数。验证成功后移除 8 字节标签再投递业务事件。敏感类型包括设置、通知、天气、日程、媒体、找设备、Wi-Fi、OTA、设备状态、解绑请求和解绑确认。连续 5 次认证失败立即断连；旧序号即使 HMAC 正确也按重放拒绝。

HMAC 黄金值：token 为 `000102030405060708090a0b0c0d0e0f`，`SettingsSet`、flags `0x01`、sequence `0x1234`、body `102030` 时，截断标签为 `20d18d970ef31336`。

手机或手表均可发起经过认证的 `UnpairRequest`，但清除动作必须由手表 UI 最终确认。手表确认后先发送经过认证、要求 ACK 的 `UnpairConfirm`；Android 在 ACK 前把活动 token 原子迁移到应用私有的持久化“待解绑”槽，而不是提前删除或只保存在内存中。该 ACK 完成 GATT 写入并随后断连时，Android 才清除待解绑 token；ACK 丢失、提前断连或应用进程被终止时仍保留 token，并在下次连接先恢复认证解绑。手表收到 ACK 后才清除 BLE bond、app token、手机名和通知缓存并断开连接；ACK 超时、断连或任一侧持久化失败时保留原绑定。闹钟、媒体和活动数据不属于解绑清理范围。

## 6. 黄金帧

下列十六进制字符串是 Python、C++ 与 Kotlin 实现共同使用的字节级基准。

### 空 Hello

- type：`Hello (0x01)`
- flags：`0`
- sequence：`1`
- payload：空
- 完整帧：`4646010100010000004de0`

### 中文通知

- type：`NotificationPush (0x20)`
- flags：`ACK_REQUIRED`
- sequence：`0x1234`
- payload：UTF-8 文本“来电”，字节 `e69da5e794b5`
- 完整帧：`464601200134120600e525e69da5e794b5`

错误 CRC 必须报告 CRC mismatch；1025 字节 payload 必须在复制前拒绝。黄金行为还包括重复序号拒绝，以及同类型、同序号的三片有序重组。

## 7. 版本和兼容性

v1 接收端只接受版本字节 `1`。版本不匹配返回明确错误，不尝试按 v1 解释 payload。新增可选消息类型可以保持帧格式不变；帧头、现有类型值、CRC 参数或分片语义如需变化，必须提升协议版本。
