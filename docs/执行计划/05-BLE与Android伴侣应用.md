# FireflyOS BLE 与 Android 伴侣应用 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立版本化、安全且可断线恢复的 BLE 协议，并交付 Android 伴侣应用的配对、设置同步、通知转发、天气摘要、媒体控制和找设备能力。

**Architecture:** 手表是 BLE Peripheral/GATT Server，Android 是 Central/GATT Client，符合 Android 官方 BLE 角色模型。BLE 只传输小型控制与摘要数据；协议使用固定帧头、分片、序号、CRC、确认和配对令牌，断连不影响手表本地功能。

**Tech Stack:** Arduino-ESP32 BLE、mbedTLS HMAC、Android 12+ Nearby Devices 权限、Kotlin、BluetoothGatt、NotificationListenerService、AGP 9.2/Kotlin 2.3。

---

## 1. 依据与文件结构

Android 依据：

- [Bluetooth Low Energy overview](https://developer.android.com/develop/connectivity/bluetooth/ble/ble-overview)
- [Bluetooth permissions](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions)
- [NotificationListenerService](https://developer.android.com/reference/android/service/notification/NotificationListenerService)

**Create:**

```text
docs/模块说明/07-BLE协议.md
docs/UI预览/04-Android伴侣/
libraries/FireflyOS/src/firefly/protocol/ProtocolTypes.h
libraries/FireflyOS/src/firefly/protocol/FrameCodec.h
libraries/FireflyOS/src/firefly/protocol/FrameCodec.cpp
libraries/FireflyOS/src/firefly/services/ConnectivityService.h
libraries/FireflyOS/src/firefly/services/ConnectivityService.cpp
libraries/FireflyOS/src/firefly/services/NotificationService.h
libraries/FireflyOS/src/firefly/services/NotificationService.cpp
libraries/FireflyOS/src/firefly/hal/BlePeripheralDevice.h
libraries/FireflyOS/src/firefly/hal/BlePeripheralDevice.cpp
tests/python/test_protocol_frames.py
AndroidCompanion/settings.gradle.kts
AndroidCompanion/build.gradle.kts
AndroidCompanion/gradlew.bat
AndroidCompanion/gradle/wrapper/gradle-wrapper.properties
AndroidCompanion/app/build.gradle.kts
AndroidCompanion/app/src/main/AndroidManifest.xml
AndroidCompanion/app/src/main/java/com/fireflyos/companion/MainActivity.kt
AndroidCompanion/app/src/main/res/layout/activity_main.xml
AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/FrameCodec.kt
AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/FireflyGattClient.kt
AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/ConnectionRepository.kt
AndroidCompanion/app/src/main/java/com/fireflyos/companion/notifications/PhoneNotificationListener.kt
AndroidCompanion/app/src/main/java/com/fireflyos/companion/data/DeviceState.kt
AndroidCompanion/app/src/main/java/com/fireflyos/companion/data/SettingsSync.kt
AndroidCompanion/app/src/test/java/com/fireflyos/companion/ble/FrameCodecTest.kt
```

## 2. Task 1：冻结 BLE 协议 v1

**Files:**
- Create: `docs/模块说明/07-BLE协议.md`
- Create: `libraries/FireflyOS/src/firefly/protocol/ProtocolTypes.h`
- Create: `tests/python/test_protocol_frames.py`

- [ ] **Step 1: 固定 GATT UUID**

  ```text
  FireflyOS Service:  7b7f0001-4f53-4653-8000-ff1e00000001
  Command RX:         7b7f0002-4f53-4653-8000-ff1e00000001
  Event TX Notify:    7b7f0003-4f53-4653-8000-ff1e00000001
  Bulk Control:       7b7f0004-4f53-4653-8000-ff1e00000001
  ```

- [ ] **Step 2: 固定帧格式**

  ```text
  Offset  Size  Field
  0       2     Magic = 0x46 0x46 ('FF')
  2       1     Protocol version = 1
  3       1     Message type
  4       1     Flags: ACK_REQUIRED/IS_ACK/FRAGMENT/LAST_FRAGMENT
  5       2     Sequence, little-endian
  7       2     Payload length, little-endian
  9       2     CRC16-CCITT of bytes 0..8 and payload
  11      N     Payload, maximum logical frame 1024 bytes
  ```

  ATT 分片块固定最大 180 字节；若协商 MTU 较小，使用 `min(180, mtu - 3)`。

- [ ] **Step 3: 定义消息类型**

  ```cpp
  enum class MessageType : uint8_t {
      Hello = 0x01,
      PairRequest = 0x02,
      PairConfirm = 0x03,
      Ack = 0x04,
      DeviceState = 0x10,
      SettingsGet = 0x11,
      SettingsSet = 0x12,
      NotificationPush = 0x20,
      NotificationDismiss = 0x21,
      WeatherUpdate = 0x30,
      CalendarUpdate = 0x31,
      MediaCommand = 0x40,
      FindPhone = 0x41,
      FindWatch = 0x42,
      WifiProvision = 0x50,
      OtaControl = 0x60,
      Error = 0x7F
  };
  ```

- [ ] **Step 4: 写 Python 黄金帧测试**

  测试必须固定：空 Hello 帧、带中文 UTF-8 通知帧、错误 CRC、重复序号、三分片重组和 1025 字节拒绝。黄金帧十六进制同时写入协议文档供 C++/Kotlin 对照。

- [ ] **Step 5: 运行测试并确认失败**

  ```powershell
  python -m unittest tests.python.test_protocol_frames -v
  ```

  Expected: FAIL，因为 FrameCodec 尚未实现。

- [ ] **Step 6: Commit**

  ```powershell
  git add docs/模块说明/07-BLE协议.md libraries/FireflyOS/src/firefly/protocol/ProtocolTypes.h tests/python/test_protocol_frames.py
  git commit -m "docs: freeze FireflyOS BLE protocol v1"
  ```

## 3. Task 2：实现手表端 FrameCodec

**Files:**
- Create: `libraries/FireflyOS/src/firefly/protocol/FrameCodec.*`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 写 C++ 黄金帧测试**

  读取与 Python 相同的 Hello/通知黄金字节，验证编码结果逐字节一致；CRC 错误返回 `DecodeError::CrcMismatch`。

- [ ] **Step 2: 定义固定缓冲接口**

  ```cpp
  struct Frame {
      MessageType type = MessageType::Error;
      uint8_t flags = 0;
      uint16_t sequence = 0;
      uint16_t payload_length = 0;
      uint8_t payload[1024]{};
  };

  enum class DecodeError : uint8_t {
      None, TooShort, BadMagic, BadVersion, TooLarge, CrcMismatch
  };

  class FrameCodec {
  public:
      static size_t encode(const Frame & frame, uint8_t * output, size_t capacity);
      static DecodeError decode(const uint8_t * input, size_t length, Frame & frame);
      static uint16_t crc16(const uint8_t * data, size_t length,
                            uint16_t seed = 0xFFFF);
  };
  ```

- [ ] **Step 3: 实现无堆编码解码**

  禁止 `String`、`std::vector` 和动态分配；所有长度在复制前验证。对未知消息类型返回 `Error` 响应，不重启连接。

- [ ] **Step 4: 运行双端测试并提交**

  ```powershell
  python -m unittest tests.python.test_protocol_frames -v
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/protocol tests/FireflyCoreTests
  git commit -m "feat: add bounded BLE frame codec"
  ```

## 4. Task 3：实现 BLE Peripheral 与连接服务

**Files:**
- Create: `libraries/FireflyOS/src/firefly/hal/BlePeripheralDevice.*`
- Create: `libraries/FireflyOS/src/firefly/services/ConnectivityService.*`

- [ ] **Step 1: 定义 HAL 回调**

  ```cpp
  class BlePeripheralDevice {
  public:
      using ReceiveCallback = void (*)(const uint8_t *, size_t);
      bool begin(const char * device_name, ReceiveCallback callback);
      void advertise(uint16_t interval_ms);
      void stopAdvertising();
      bool notify(const uint8_t * data, size_t length);
      bool connected() const;
      uint16_t negotiatedMtu() const;
      void disconnect();
  };
  ```

- [ ] **Step 2: 控制广播与连接参数**

  未配对时广播 60 秒后降为低频；已绑定设备优先快速重连 20 秒，再降频。连接期间空闲 30 秒使用低功耗间隔；同步会话临时提高吞吐，结束后恢复。

- [ ] **Step 3: 实现命令分发**

  ConnectivityService 在后台解析 Frame，验证版本、序号、CRC 和认证，再投递 `SystemEvent`；不得直接改 UI。每个 ACK 超时 2 秒，最多重试 3 次。

- [ ] **Step 4: 断连语义**

  断连只将 `phone_connected=false`，天气保留缓存并标记旧，通知历史保留，本地闹钟/活动/媒体不停止。

- [ ] **Step 5: 真机互操作测试并提交**

  使用通用 BLE 调试 App 写入 Hello 黄金帧并读取 ACK；反复连接/断开 50 次。Expected: 无内存下降和卡死。

  ```powershell
  git add libraries/FireflyOS/src/firefly/hal/BlePeripheralDevice.* libraries/FireflyOS/src/firefly/services/ConnectivityService.*
  git commit -m "feat: add FireflyOS BLE peripheral service"
  ```

## 5. Task 4：实现配对与应用层认证

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/ConnectivityService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/StorageService.*`
- Modify: `libraries/FireflyOS/src/firefly/ui/screens/SystemOverlayHost.*`

- [ ] **Step 1: 配对流程预览审批**

  先展示手表六位验证码、手机设备名称、允许/拒绝按钮、绑定成功和失败状态；获得批准后实现。

- [ ] **Step 2: 使用 BLE Secure Connections bonding**

  手表显示随机六位码并要求用户确认；连接完成后生成 128-bit 随机 app token，保存在 `ff_pair`。不得通过日志输出 token、PIN 或 Wi-Fi 密码。

- [ ] **Step 3: 消息认证**

  已配对后的敏感消息在 payload 尾部携带 HMAC-SHA256 截断 8 字节，密钥为 app token；验证失败返回 Unauthorized，连续 5 次失败后断开连接。

- [ ] **Step 4: 解除绑定**

  手表设置和 Android 均可发起；手表端最终确认后清除 bonding、app token、通知缓存和手机设备名，但不删除本地闹钟、媒体与活动数据。

- [ ] **Step 5: 安全测试并提交**

  测试正确 token、错误 token、重放旧序号、连续失败和解除绑定后重连。Expected: 未认证客户端不能设置时间、Wi-Fi 或 OTA。

  ```powershell
  git add libraries/FireflyOS/src/firefly/services libraries/FireflyOS/src/firefly/ui/screens
  git commit -m "feat: secure FireflyOS companion pairing"
  ```

## 6. Task 5：先审批 Android 应用 UI

**Files:**
- Create: `docs/UI预览/04-Android伴侣/index.html`
- Create: `docs/UI预览/04-Android伴侣/审批记录.md`

- [ ] **Step 1: 绘制页面**

  包含：设备扫描/配对、设备主页、通知授权、设置同步、主题管理、天气城市、媒体控制、找设备、固件更新和解除绑定。

- [ ] **Step 2: 明确离线状态**

  手机未连接时可浏览最近设备信息，但所有远程操作禁用并说明原因；不得暗示手表核心功能失效。

- [ ] **Step 3: 用户批准 Gate**

  将用户确认写入审批记录。没有批准不实现 Android 界面。

- [ ] **Step 4: Commit**

  ```powershell
  git add docs/UI预览/04-Android伴侣
  git commit -m "docs: add approved Android companion previews"
  ```

## 7. Task 6：创建 Android 工程和权限边界

**Files:**
- Create: `AndroidCompanion/settings.gradle.kts`
- Create: `AndroidCompanion/build.gradle.kts`
- Create: `AndroidCompanion/app/build.gradle.kts`
- Create: `AndroidCompanion/app/src/main/AndroidManifest.xml`
- Create: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/MainActivity.kt`

- [ ] **Step 1: 创建工程**

  使用 Android Studio 的 Empty Views Activity 模板创建 Gradle Wrapper 和基础资源；固定 AGP `9.2.0`、Kotlin `2.3.21`、`compileSdk=36`、`targetSdk=36`、`minSdk=26`，不使用 `+` 动态依赖。主界面使用 Android Views/XML，避免为首版引入第二套声明式 UI 状态框架。

- [ ] **Step 2: 声明最小权限**

  ```xml
  <uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />
  <uses-permission android:name="android.permission.BLUETOOTH_SCAN"
      android:usesPermissionFlags="neverForLocation" />
  <uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
  <uses-permission android:name="android.permission.BLUETOOTH"
      android:maxSdkVersion="30" />
  <uses-permission android:name="android.permission.BLUETOOTH_ADMIN"
      android:maxSdkVersion="30" />
  <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"
      android:maxSdkVersion="30" />
  <uses-permission android:name="android.permission.READ_CALENDAR" />
  ```

  不申请后台位置；扫描只用于用户明确的配对流程。`READ_CALENDAR` 仅在用户主动启用“同步日程摘要”时请求，拒绝后其他伴侣功能保持可用。

- [ ] **Step 3: 运行空工程测试**

  ```powershell
  cd AndroidCompanion
  .\gradlew.bat testDebugUnitTest assembleDebug
  ```

  Expected: BUILD SUCCESSFUL。

- [ ] **Step 4: Commit**

  ```powershell
  git add AndroidCompanion
  git commit -m "feat: scaffold FireflyOS Android companion"
  ```

## 8. Task 7：实现 Android FrameCodec 与 GATT Client

**Files:**
- Create: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/FrameCodec.kt`
- Create: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/FireflyGattClient.kt`
- Create: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/ConnectionRepository.kt`
- Create: `AndroidCompanion/app/src/test/java/com/fireflyos/companion/ble/FrameCodecTest.kt`

- [ ] **Step 1: 写 Kotlin 黄金帧测试**

  测试字节必须与 `test_protocol_frames.py` 相同；验证 UTF-8 通知、CRC 错误和三片重组。

- [ ] **Step 2: 实现 Kotlin FrameCodec**

  使用 `ByteBuffer.order(ByteOrder.LITTLE_ENDIAN)`；逻辑帧最大 1024 字节；所有解码错误返回 sealed result，不抛出到 UI 主线程。

- [ ] **Step 3: 实现 15 秒限时扫描**

  扫描只匹配 FireflyOS Service UUID；发现目标后立即停止，不循环扫描。连接使用 `connectGatt(context, false, callback)`。

- [ ] **Step 4: 串行化 GATT 操作**

  Android 每次只允许一个 characteristic write/descriptor write 在途；收到 callback 后才发下一项。连接状态通过 `StateFlow<DeviceState>` 暴露给 UI。

- [ ] **Step 5: 运行单元测试并提交**

  ```powershell
  cd AndroidCompanion
  .\gradlew.bat testDebugUnitTest
  git add app/src
  git commit -m "feat: add Android BLE protocol client"
  ```

## 9. Task 8：实现通知转发

**Files:**
- Create: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/notifications/PhoneNotificationListener.kt`
- Create: `libraries/FireflyOS/src/firefly/services/NotificationService.*`
- Modify: `AndroidCompanion/app/src/main/AndroidManifest.xml`

- [ ] **Step 1: 声明通知监听服务**

  ```xml
  <service
      android:name=".notifications.PhoneNotificationListener"
      android:exported="true"
      android:label="FireflyOS 通知同步"
      android:permission="android.permission.BIND_NOTIFICATION_LISTENER_SERVICE">
      <intent-filter>
          <action android:name="android.service.notification.NotificationListenerService" />
      </intent-filter>
  </service>
  ```

- [ ] **Step 2: 限制数据**

  转发包名、应用名、标题最多 128 UTF-8 字节、正文最多 256 字节、发布时间和稳定 notification key；不传图片、RemoteViews、操作 token 或完整历史。

- [ ] **Step 3: 手表端去重与容量**

  NotificationService 复用计划 1 `NotificationModel.h` 中的固定模型，最多保存 20 条摘要；相同 key 更新原条目；超过容量删除最旧非置顶通知。锁屏隐藏正文设置由手表本地决定。

  ```cpp
  static_assert(sizeof(NotificationSummary) <= 480,
                "notification summaries must remain bounded");
  ```

- [ ] **Step 4: 删除同步**

  Android 通知移除时发送 `NotificationDismiss`；手表本地清除不反向关闭手机通知，避免权限和误操作复杂度。

- [ ] **Step 5: 验证并提交**

  测试中文长文本、同 key 更新、20+ 条、断连期间通知和重连。Expected: 重连只同步当前有效摘要，不洪泛旧通知。

  ```powershell
  git add AndroidCompanion/app/src libraries/FireflyOS/src/firefly/services/NotificationService.*
  git commit -m "feat: sync bounded Android notifications"
  ```

## 10. Task 9：实现设置、天气/日程摘要、媒体与找设备

**Files:**
- Create: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/data/SettingsSync.kt`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/MainActivity.kt`
- Modify: `libraries/FireflyOS/src/firefly/services/ConnectivityService.*`

- [ ] **Step 1: 设置同步冲突规则**

  每份设置带 `revision:uint32` 和 `changed_at:int64`；最近明确操作获胜。闹钟、亮度、音量和主题每类独立 revision，避免一个设置覆盖全部。

- [ ] **Step 2: 天气摘要**

  手机发送城市、温度、天气码、最高/最低温和更新时间；手表缓存 24 小时，超过 3 小时标记“数据较旧”，断连仍显示缓存。

- [ ] **Step 3: 可选日程摘要**

  用户授予日历权限后，Android 只同步未来 7 天内最多 8 条事件的标题、开始/结束时间和全天标记；不传参与者、地点详情或备注。拒绝权限时发送禁用状态，手表日历仍可离线浏览日期。

- [ ] **Step 4: 媒体控制**

  手表发送播放/暂停、上一首、下一首和音量命令；Android 只在可用媒体会话存在时执行，否则返回明确错误。

- [ ] **Step 5: 找设备**

  找手机要求 Android 前台/允许通知提示；找手表触发 30 秒声光提示，可在手表取消。极低电量时只闪屏 5 秒，不持续播放。

- [ ] **Step 6: 运行 Android 与真机集成测试**

  ```powershell
  cd AndroidCompanion
  .\gradlew.bat testDebugUnitTest assembleDebug
  ```

  真机执行 20 次断连重连、设置冲突、天气过期和媒体不可用场景。

- [ ] **Step 7: Commit**

  ```powershell
  git add AndroidCompanion libraries/FireflyOS/src/firefly/services/ConnectivityService.*
  git commit -m "feat: add companion settings weather and controls"
  ```

## 11. Task 10：Gate D 验收

**Files:**
- Create: `docs/模块说明/08-Android伴侣验收.md`
- Modify: `docs/项目介绍.md`
- Modify: `Firefly/README.md`

- [ ] **Step 1: 完整验证**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  cd AndroidCompanion
  .\gradlew.bat testDebugUnitTest assembleDebug
  ```

- [ ] **Step 2: 安全与恢复验收**

  - 未配对设备不能修改设置。
  - 错误 HMAC 和重放序号被拒绝。
  - 手机断连不影响本地时间、闹钟、活动、音乐和录音。
  - 50 次连接循环无持续内存下降。
  - Android 扫描 15 秒自动停止。
  - 通知权限、BLE 权限和解除绑定路径可理解。

- [ ] **Step 3: 更新文档并提交**

  ```powershell
  git add docs/模块说明/08-Android伴侣验收.md docs/项目介绍.md Firefly/README.md
  git commit -m "docs: document BLE and Android companion"
  ```
