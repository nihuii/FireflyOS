# FireflyOS Wi-Fi、天气、OTA 与发布验收 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 增加按需 Wi-Fi、NTP、直接天气、临时大文件传输和可回滚 OTA，并完成全系统性能、续航、可靠性、安全与公开发布验收。

**Architecture:** Wi-Fi 由会话状态机管理，默认关闭；BLE 负责配网和启动会话。天气以手机摘要为首选、Open-Meteo 直连为离线于手机的备用来源。OTA 使用 32MB Flash 的双应用槽、完整性/签名校验和首次启动自检确认。

**Tech Stack:** Arduino WiFi、HTTPClient/WiFiClientSecure、SNTP、Open-Meteo Forecast API、ESP-IDF OTA API、SHA-256、ECDSA P-256、LittleFS、Android BLE/Wi-Fi 协调。

---

## 1. 依据与文件结构

技术依据：

- [ESP32-S3 Wi-Fi power save](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/wifi-driver/wifi-performance-and-power-save.html)
- [ESP32-S3 OTA and rollback](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html)
- [Open-Meteo Forecast API](https://open-meteo.com/en/docs)

**Create:**

```text
libraries/FireflyOS/src/firefly/services/WifiService.h
libraries/FireflyOS/src/firefly/services/WifiService.cpp
libraries/FireflyOS/src/firefly/services/WeatherService.h
libraries/FireflyOS/src/firefly/services/WeatherService.cpp
libraries/FireflyOS/src/firefly/services/BulkTransferService.h
libraries/FireflyOS/src/firefly/services/BulkTransferService.cpp
libraries/FireflyOS/src/firefly/services/UpdateService.h
libraries/FireflyOS/src/firefly/services/UpdateService.cpp
libraries/FireflyOS/src/firefly/services/DiagnosticService.h
libraries/FireflyOS/src/firefly/services/DiagnosticService.cpp
libraries/FireflyOS/src/firefly/apps/weather/WeatherApp.h
libraries/FireflyOS/src/firefly/apps/weather/WeatherApp.cpp
Firefly/partitions.csv
tests/python/test_partition_layout.py
tests/python/test_update_manifest.py
tools/sign_update.py
docs/UI预览/05-天气与更新/
docs/模块说明/09-WiFi天气与传输.md
docs/模块说明/10-OTA发布规范.md
docs/模块说明/11-最终验收报告.md
```

## 2. Task 1：实现按需 WifiService

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/WifiService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/PowerService.*`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 写会话状态机测试**

  ```cpp
  static void test_wifi_session_timeout() {
      firefly::WifiService wifi;
      wifi.configureTimeout(60000);
      expect_true(wifi.request(firefly::WifiPurpose::Weather, 1000),
                  "weather requests Wi-Fi");
      expect_true(wifi.mode() == firefly::WifiMode::Connecting,
                  "Wi-Fi connecting");
      wifi.onConnected(5000);
      expect_true(wifi.mode() == firefly::WifiMode::Connected,
                  "Wi-Fi connected");
      wifi.tick(65001);
      expect_true(wifi.mode() == firefly::WifiMode::Off,
                  "Wi-Fi auto stops");
  }
  ```

- [ ] **Step 2: 定义会话类型**

  ```cpp
  enum class WifiPurpose : uint8_t { Ntp, Weather, Transfer, Ota };
  enum class WifiMode : uint8_t { Off, Connecting, Connected, SoftAp, Error };

  class WifiService {
  public:
      void configureTimeout(uint32_t timeout_ms);
      bool request(WifiPurpose purpose, uint32_t now_ms);
      void release(WifiPurpose purpose, uint32_t now_ms);
      void tick(uint32_t now_ms);
      void onConnected(uint32_t now_ms);
      WifiMode mode() const;
      bool provision(const char * ssid, const char * password);
      void forgetNetwork();
  };
  ```

- [ ] **Step 3: 实现按需开关和功耗策略**

  无活动会话立即 `WiFi.disconnect(true)` 并 `WiFi.mode(WIFI_OFF)`；Station 连接使用 `WIFI_PS_MIN_MODEM`；连接超时 15 秒；普通会话空闲 60 秒关闭；OTA/Transfer 由会话显式释放但最长 15 分钟。

- [ ] **Step 4: 与 PowerService 协调**

  低电量 15% 以下拒绝 Transfer 和 OTA；5% 以下拒绝所有新 Wi-Fi 会话。进入 LightSleep 前必须确认 WifiService 已 Off。

- [ ] **Step 5: 验证功耗并提交**

  测量 Wi-Fi Off、Connected idle、Weather request 三种电流；会话结束 5 秒内回到 Off 基线。

  ```powershell
  git add libraries/FireflyOS/src/firefly/services/WifiService.* libraries/FireflyOS/src/firefly/services/PowerService.* tests/FireflyCoreTests
  git commit -m "feat: add on-demand Wi-Fi sessions"
  ```

## 3. Task 2：通过 BLE 安全配网

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/ConnectivityService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/WifiService.*`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/MainActivity.kt`

- [ ] **Step 1: 预览审批**

  展示手机选择 Wi-Fi、手表确认网络名称、连接进度、失败原因和忘记网络页面。手表不显示密码明文。

- [ ] **Step 2: 接收认证配网消息**

  `WifiProvision` 只接受已 bonding 且 HMAC 验证通过的连接；payload 包含 SSID 长度、SSID、密码长度、密码和 60 秒有效 nonce。

- [ ] **Step 3: 安全保存**

  将凭据写入专用 NVS 命名空间；日志只记录 SSID 的 SHA-256 前 8 位，不记录密码或完整 SSID。恢复出厂和忘记网络必须清除凭据。

- [ ] **Step 4: 连接反馈**

  手表发送 Connecting/Success/AuthFailed/NotFound/Timeout；Android 用明确中文文案显示，不自动无限重试。

- [ ] **Step 5: 验证并提交**

  测试正确密码、错误密码、隐藏网络、nonce 过期、未认证客户端和忘记网络。

  ```powershell
  git add libraries/FireflyOS/src/firefly/services AndroidCompanion/app/src
  git commit -m "feat: provision Wi-Fi through secured BLE"
  ```

## 4. Task 3：实现 NTP 与 WeatherService

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/WeatherService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/TimeService.*`
- Create: `tests/python/test_weather_payload.py`

- [ ] **Step 1: 定义统一天气模型**

  ```cpp
  struct WeatherSnapshot {
      int16_t temperature_tenths_c = 0;
      int16_t high_tenths_c = 0;
      int16_t low_tenths_c = 0;
      uint16_t weather_code = 0;
      int64_t updated_epoch = 0;
      char city[32]{};
      bool valid = false;
      bool stale = true;
  };
  ```

- [ ] **Step 2: 手机数据优先**

  收到 Android WeatherUpdate 时验证数值范围和时间戳，写入 LittleFS 缓存；3 小时后 `stale=true`，24 小时后仍可显示但明确“较早数据”。

- [ ] **Step 3: 实现 Open-Meteo 备用请求**

  使用固定 URL 结构：

  ```text
  https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=2
  ```

  纬度/经度由 Android 设置或手表设置缓存，不使用 GPS。HTTPS 使用随固件更新的受信 CA 证书链并设置 15 秒总超时；JSON 只解析必需字段，响应体上限 8KB，超限立即中止。

- [ ] **Step 4: NTP 校时**

  Wi-Fi 成功后使用 SNTP 获取时间；只有 RTC 无效或网络时间与本地差异超过 2 秒时写回 RTC。闹钟触发期间不突然调整系统时间，延后到闹钟关闭后同步。

- [ ] **Step 5: 写 payload 与缓存测试**

  测试正常响应、字段缺失、极端温度、超过 8KB、缓存 3/24 小时和手机/直连来源切换。

- [ ] **Step 6: 验证并提交**

  ```powershell
  python -m unittest tests.python.test_weather_payload -v
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/services tests/python/test_weather_payload.py
  git commit -m "feat: add cached weather and NTP services"
  ```

## 5. Task 4：实现 WeatherApp

**Files:**
- Create: `docs/UI预览/05-天气与更新/天气.html`
- Create: `libraries/FireflyOS/src/firefly/apps/weather/WeatherApp.*`

- [ ] **Step 1: 先绘制并审批预览**

  状态包括：新鲜数据、3 小时旧数据、24 小时旧数据、无位置、无网络、正在更新和服务错误。图标遵守流萤日常层视觉，不使用复杂动画。

- [ ] **Step 2: 页面只读取 WeatherSnapshot**

  页面不得发 HTTP；用户点击刷新只调用 WeatherService 请求，显示进度事件。隐藏页面停止刷新动画。

- [ ] **Step 3: 图标资源**

  天气码映射到最多 12 个 48 × 48 单色/A8 图标；使用总纲 AI 图标提示词生成母图并经用户批准后转换。

- [ ] **Step 4: 验证并提交**

  ```powershell
  git add docs/UI预览/05-天气与更新/天气.html libraries/FireflyOS/src/firefly/apps/weather
  git commit -m "feat: add offline-tolerant weather app"
  ```

## 6. Task 5：实现临时大文件传输

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/BulkTransferService.*`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/ConnectionRepository.kt`

- [ ] **Step 1: 会话协商**

  Android 通过 BLE 请求 Transfer，手表检查电量、SD、空间和当前音频/OTA 会话；允许后生成 128-bit 一次性 token 和 5 分钟会话。

- [ ] **Step 2: 传输通道**

  首选双方连接同一已配置 WLAN，由手表启动仅局域网可见的 HTTP 服务；无法共网时手表启动临时 SoftAP。HTTP 请求必须携带一次性 token，禁止目录浏览。

- [ ] **Step 3: 安全写入**

  文件只写入 `/FireflyOS/Themes`、`Pictures`、`Music`、`Updates`；先写 `.part`，校验声明的 SHA-256 和大小后原子重命名。单文件上限 64MB。

- [ ] **Step 4: 取消与超时**

  用户取消、5 分钟无数据、低电量或断连时删除 `.part`、停止服务器和 Wi-Fi，并通过 BLE/本地 UI报告原因。

- [ ] **Step 5: 验证并提交**

  测试 1KB、1MB、32MB、错误哈希、中断恢复、目录逃逸、超时和拔卡。Expected: 无损坏正式文件，无 Wi-Fi 常驻。

  ```powershell
  git add libraries/FireflyOS/src/firefly/services/BulkTransferService.* AndroidCompanion/app/src
  git commit -m "feat: add authenticated bulk transfer sessions"
  ```

## 7. Task 6：建立 32MB 双 OTA 分区

**Files:**
- Create: `Firefly/partitions.csv`
- Create: `tests/python/test_partition_layout.py`
- Modify: `tools/build_firmware.ps1`

- [ ] **Step 1: 写分区测试**

  Python 测试解析 CSV，验证无重叠、总结束地址不超过 `0x2000000`、存在 `ota_0`/`ota_1`、两槽大小相同且均为 `0xB00000`、otadata 为 `0x2000`。

- [ ] **Step 2: 写分区表**

  ```csv
  # Name,     Type, SubType, Offset,    Size,       Flags
  nvs,        data, nvs,     0x9000,    0x5000,
  otadata,    data, ota,     0xE000,    0x2000,
  app0,       app,  ota_0,   0x10000,   0xB00000,
  app1,       app,  ota_1,   0xB10000,  0xB00000,
  littlefs,   data, spiffs,  0x1610000, 0x9F0000,
  ```

- [ ] **Step 3: 让构建使用该分区表**

  编译后检查生成的 partitions 输出，确认最终地址与 CSV 一致；固件二进制必须小于 9,227,469 字节，即 11MB 槽位的 80%，否则先把壁纸/主题迁移到文件系统或 SD，不压缩安全余量。

- [ ] **Step 4: 验证并提交**

  ```powershell
  python -m unittest tests.python.test_partition_layout -v
  powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target Firefly
  git add Firefly/partitions.csv tests/python/test_partition_layout.py tools/build_firmware.ps1
  git commit -m "build: add 32MB dual OTA partition layout"
  ```

## 8. Task 7：实现签名 OTA 与回滚

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/UpdateService.*`
- Create: `tools/sign_update.py`
- Create: `tests/python/test_update_manifest.py`
- Create: `docs/UI预览/05-天气与更新/系统更新.html`
- Create: `docs/模块说明/10-OTA发布规范.md`

- [ ] **Step 1: 先绘制并审批更新页面**

  展示版本、包大小、电量要求、下载、校验、写入、重启、首次启动验证、失败和回滚。不可中断写入阶段使用 SAM 守护层视觉。

- [ ] **Step 2: 定义更新清单**

  清单 JSON 与下列强类型字段一一对应；`sign_update.py` 从实际固件生成 size、SHA-256 和签名，不接受人工空值：

  ```cpp
  struct UpdateManifest {
      uint16_t schema;
      char product[16];
      char version[16];
      uint32_t build;
      uint32_t min_build;
      uint32_t size;
      uint8_t sha256[32];
      uint8_t ecdsa_p256_signature[64];
  };
  ```

- [ ] **Step 3: 实现离线签名工具**

  `sign_update.py` 读取固件、计算 SHA-256、使用离线 ECDSA P-256 私钥签名并输出清单。私钥路径从环境变量 `FIREFLY_SIGNING_KEY` 读取，私钥文件不得进入仓库；公钥以只读常量嵌入固件。

- [ ] **Step 4: OTA 前置条件**

  电量至少 40% 或正在充电；不允许录音、音乐、文件传输和闹钟响铃；目标 build 必须高于当前 build；包大小不得超过 OTA 槽。

- [ ] **Step 5: 下载、校验和写入**

  包可来自 HTTPS 或 `/FireflyOS/Updates`；先验证清单签名，再流式计算固件 SHA-256，匹配后写入非活动 OTA 槽。任何失败保留当前启动槽。

- [ ] **Step 6: 首次启动确认**

  新固件启动后 30 秒内完成 RTC、PMU、显示、触摸、NVS 和主 UI 自检；成功调用 `esp_ota_mark_app_valid_cancel_rollback()`。自检失败调用 `esp_ota_mark_app_invalid_rollback_and_reboot()`。

- [ ] **Step 7: 断电与回滚测试**

  在下载 25%、写入 25/50/75%、重启前和首次启动自检阶段分别模拟断电。Expected: 至少一个旧版本始终可启动；失败包不会标为有效。

- [ ] **Step 8: 验证并提交**

  ```powershell
  python -m unittest tests.python.test_update_manifest -v
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/services/UpdateService.* tools/sign_update.py tests/python/test_update_manifest.py docs/UI预览/05-天气与更新/系统更新.html docs/模块说明/10-OTA发布规范.md
  git commit -m "feat: add signed rollback-safe OTA"
  ```

## 9. Task 8：全系统资源与性能收口

**Files:**
- Create: `docs/模块说明/11-最终验收报告.md`
- Create: `libraries/FireflyOS/src/firefly/services/DiagnosticService.*`

- [ ] **Step 1: 增加诊断快照**

  每分钟和关键会话开始/结束记录：内部空闲堆、最低堆、最大块、PSRAM、UI/后台任务栈高水位、事件队列峰值、当前功耗模式和重启原因。日志为固定 64 条环形缓存。

- [ ] **Step 2: 运行 24 小时稳定性测试**

  场景包含：每小时 UI 操作、6 次天气、100 次 BLE 重连、30 分钟音乐、10 分钟录音、20 次闹钟、10 次 SD 拔插。Expected: 无看门狗复位，应用退出后内存回到稳定区间。

- [ ] **Step 3: 运行 400mAh 续航测试**

  默认亮度、每小时抬腕 20 次、BLE 日常连接、天气 6 次、音乐 30 分钟。目标至少 24 小时，代表性平均电流不高于 14mA。未达到时按耗电排序关闭非关键后台刷新，重新测试。

- [ ] **Step 4: UI 性能测试**

  记录锁屏到桌面、打开设置、下拉控制中心、打开媒体列表和天气页面。触摸反馈通常小于 100ms，任何无反馈阻塞不得超过 250ms。

- [ ] **Step 5: Flash/RAM 余量**

  固件低于 OTA 槽 80%；各内存区域保留至少 15%；UI 和后台任务栈保留至少 25% 高水位余量。

## 10. Task 9：安全、隐私与恢复出厂验收

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/StorageService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/ConnectivityService.*`
- Modify: `docs/模块说明/11-最终验收报告.md`

- [ ] **Step 1: 恢复出厂行为**

  清除配对、Wi-Fi、通知、天气、设置和内部缓存；默认不删除 SD 媒体。用户明确选择“同时清除 SD FireflyOS 数据”时才删除 `/FireflyOS/`。

- [ ] **Step 2: 隐私检查**

  日志不得出现 Wi-Fi 密码、app token、HMAC、通知全文或录音内容；锁屏隐藏正文设置默认开启。

- [ ] **Step 3: 权限检查**

  Android 只请求 Nearby devices 和用户主动授权的通知监听；无后台位置、通讯录、麦克风或存储广泛权限。

- [ ] **Step 4: 故障恢复**

  验证 RTC、PMU、IMU、SD、Codec、BLE、Wi-Fi 各自不可用时，系统显示明确降级且其他模块正常。

## 11. Task 10：公开发布与 Gate E

**Files:**
- Create: `docs/模块说明/12-发布清单.md`
- Modify: `docs/项目介绍.md`
- Modify: `Firefly/README.md`
- Modify: `docs/FireflyOS系统架构总纲.md`

- [ ] **Step 1: 主题与内核分离检查**

  中性基础主题可独立编译；流萤主题为可替换资源包；系统核心、协议和服务不引用具体角色素材路径。

- [ ] **Step 2: 文档检查**

  项目介绍反映真实已实现能力；README 包含构建、烧录、分区、配对、SD 目录、OTA、恢复与已知限制；不得把规划功能写成已完成。

- [ ] **Step 3: 完整验证**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  cd AndroidCompanion
  .\gradlew.bat testDebugUnitTest assembleDebug
  ```

  Expected: 全部通过。

- [ ] **Step 4: 创建候选版本并真机验收**

  ```powershell
  git tag -a v1.0.0-rc1 -m "FireflyOS 1.0.0 release candidate 1"
  ```

  在候选版本上完成 24 小时稳定性、400mAh 续航、BLE、Wi-Fi、音频、SD、闹钟和 OTA 回滚测试。

- [ ] **Step 5: 完成最终报告并提交**

  ```powershell
  git add docs Firefly/README.md
  git commit -m "docs: finalize FireflyOS release verification"
  ```
