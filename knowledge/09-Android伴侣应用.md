# Android 伴侣应用

## 来源

- 固定提交：`e4fb0d1ff71bcc0330b507fa90c653be9929e611`
- 主要路径：`AndroidCompanion/app/src/main/java/com/fireflyos/companion/`
- 取回示例：`git show "e4fb0d1:AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/ConnectionRepository.kt"`

## 复用等级

**需要重构后复用。** GATT 主线程串行化、固定队列、可靠发送、私有持久化和权限空状态值得保留；`MainActivity` 承担过多 UI 与业务装配，重启 Android 端时应拆成更小的页面控制器。

## 模块定位

Android 应用负责扫描和连接手表、系统配对、业务帧认证、通知/日历/媒体同步、Wi-Fi 配网和 Bulk 上传。它不能绕过 Android 权限和特殊访问页面，也不能把业务同步放在 GATT 回调线程中无界执行。

## 职责与边界

- `FireflyGattClient`：扫描、连接、请求 MTU、服务发现、CCCD、串行 GATT 操作和超时恢复。
- `ConnectionRepository`：持有连接状态、配对协调器、认证发送器、可靠发送器和固定业务帧队列。
- `PairingProtocolCoordinator`：显式配对、修复配对、退役 token 和确认 ACK 后清理。
- `FrameAuthenticator` / `InboundFrameGate`：敏感帧 HMAC、重放检查和认证后 ACK。
- `PrivateSettingsSnapshotPersistence`：应用私有目录中的原子设置快照。
- `PhoneNotificationListener`：Android 特殊通知访问服务，只生成有界摘要。
- `AndroidCalendarDataSource`：白名单 projection，最多同步未来窗口。
- `AndroidMediaSessionGateway`：只有获得通知监听访问后才能控制 MediaSession。

## 数据流与线程边界

```text
BluetoothGatt callback
      │ Handler(主 Looper)
      ▼
FireflyGattClient 固定操作队列
      │ frame
      ▼
ConnectionRepository
      ├─ 严格 ACK → ReliableFrameSender
      ├─ InboundFrameGate → PairingProtocolCoordinator
      └─ 业务帧固定队列 → CompanionController / UI
```

成功连接不等于安全业务会话就绪。存在 token 时，Repository 先发送需要 ACK 的 Hello；只有在加密链路上收到对应严格 ACK 后，才重放设置并启用通知桥。

## 关键接口

| 项目 | 固定边界 |
|---|---:|
| 扫描超时 | 15 秒 |
| MTU/GATT 操作超时 | 2 秒 |
| GATT 批队列 | 最大 147 项，与最大分片数一致 |
| Repository 业务帧缓存 | 8 项 |
| 可靠发送 service tick | 250ms |
| 主线程同步等待上限 | 2 秒 |
| 手机名 | UTF-8 最多 32 B |

## 精选代码

来源：`AndroidCompanion/.../ble/FrameCodec.kt`，C++ 对应容量。

```kotlin
object FrameCodec {
    const val MAX_PAYLOAD = 1024
    const val HEADER_SIZE = 11
    const val MAX_FRAGMENTS = 147

    fun encode(frame: Frame): ByteArray? {
        if (frame.payload.size > MAX_PAYLOAD) return null
        val output = ByteArray(HEADER_SIZE + frame.payload.size)
        val buffer = ByteBuffer.wrap(output).order(ByteOrder.LITTLE_ENDIAN)
        // 写 magic、version、type、flags、sequence、length、CRC
        return output
    }
}
```

来源：`AndroidCompanion/.../ble/FrameAuthenticator.kt`，认证容量。

```kotlin
object FrameAuthenticator {
    const val APP_TOKEN_BYTES = 16
    const val AUTH_TAG_BYTES = 8

    fun authenticate(frame: Frame, token: ByteArray?): Frame? {
        if (!isSensitive(frame.type)) return frame
        if (token == null || token.size != APP_TOKEN_BYTES ||
            frame.payload.size > FrameCodec.MAX_PAYLOAD - AUTH_TAG_BYTES
        ) return null
        // HMAC-SHA256 后截断为 8 字节
        return frame
    }
}
```

来源：`AndroidCompanion/.../ble/FireflyGattClient.kt`，固定 GATT 队列与超时。

```kotlin
const val SCAN_TIMEOUT_MS = 15_000L
const val MAX_GATT_OPERATIONS = FrameCodec.MAX_FRAGMENTS
private const val MTU_CALLBACK_TIMEOUT_MS = 2_000L
private const val GATT_OPERATION_TIMEOUT_MS = 2_000L

private val operationQueue =
    FixedGattBatchQueue<GattOperation>(MAX_GATT_OPERATIONS)
```

来源：`AndroidCompanion/.../ble/PairingProtocolCoordinator.kt`，解除配对提交条件。

```kotlin
fun onDisconnected(): Boolean {
    val unpairCommitted =
        unpairCompleted && unpairAcknowledgementWritten
    val tokenCleared = !unpairCommitted || tokenStore.clearToken()
    // 只有确认帧 ACK 已实际写出后才清理稳定 token
    return repairPairingRequested && unpairCompleted &&
        (!unpairCommitted || tokenCleared)
}
```

该流程防止 Android 在确认响应尚未发送到手表时提前删除 token，造成两端永久失配。

## 源码与测试映射

- BLE：`ble/FireflyGattClient.kt`、`ConnectionRepository.kt`、`FrameCodec.kt`、`FrameAuthenticator.kt`、`ReliableFrameSender.kt`、`InboundFrameGate.kt`、`PairingProtocolCoordinator.kt`。
- 状态与设置：`data/DeviceState.kt`、`PrivateSettingsSnapshotPersistence.kt`、`SettingsSync.kt`。
- 系统适配：`notifications/PhoneNotificationListener.kt`、`sync/AndroidCalendarDataSource.kt`、`media/AndroidMediaSessionGateway.kt`、`find/AndroidFindPhoneController.kt`。
- 业务控制：`sync/CompanionController.kt`、`CompanionPayloadCodec.kt`。
- UI：`MainActivity.kt`、`ui/CompanionUiPolicy.kt`。
- 测试：`AndroidCompanion/app/src/test/java/com/fireflyos/companion/**`，共 21 个测试文件；另有 `tests/python/test_android_*` 合约。

## 验证边界

已有单元测试覆盖 codec、可靠发送、队列、配对、设置、通知、日历、媒体和 UI 策略；Debug APK 构建不等于在真实手机完成权限、连接和后台限制验证。本次知识库整理中 Android 复测曾按用户要求中止，因此不生成新的通过结论。正式 Android/BLE 项仍为 `PENDING`。

## 已知问题

- `MainActivity.kt` 体积较大，权限引导、配网、传输、设置和诊断 UI 需要拆分。
- Android BLE 回调顺序随厂商和系统版本变化，所有操作都必须保留超时和失败清理。
- 通知监听和媒体控制依赖“特殊访问”，不能只检查普通运行时权限。
- `Network` 绑定只应用于临时 Bulk SoftAP 请求，不能把整个进程永久绑定到无互联网网络。
- 私有设置持久化成功应先于内存状态切换，避免进程重启后回退。

## 基于 main 的复用步骤

1. 新建最小 Android 外壳，只实现权限状态和单次扫描，不接业务同步。
2. 加入 GATT 主线程操作队列、MTU 超时和 CCCD 状态机。
3. 接入最小配对协议和安全 Hello，完成多品牌手机真机连接矩阵。
4. 增加私有设置快照，再逐项加入通知、日历、媒体适配。
5. Wi-Fi 配网和 Bulk 上传最后引入，并分别验证 Android 10+ 网络请求和取消清理。

