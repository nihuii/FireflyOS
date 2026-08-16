# Wi-Fi、NTP 与天气

## 来源

- 固定提交：`e4fb0d1ff71bcc0330b507fa90c653be9929e611`
- 主要路径：`services/WifiService.*`、`services/NtpService.*`、`services/WeatherService.*`、`services/PowerService.*`
- 取回示例：`git show "e4fb0d1:libraries/FireflyOS/src/firefly/services/WifiService.h"`

## 复用等级

**需要重构后复用。** 按用途计数、空闲断网、功耗门控、配网确认、天气缓存和响应上限值得保留；不要把 Wi-Fi、NTP、天气和界面一次性接回主程序。

## 模块定位

旧实现把 Wi-Fi 当作由服务按需租用的稀缺资源，而不是常驻基础设施。NTP 和天气分别申请自己的用途位，完成或失败后释放。天气可接收手机侧快照，也可在已有位置时走 HTTPS 直连。

## 职责与边界

- `WifiService`：保存有界凭据，管理 `Ntp`、`Weather`、`Transfer`、`Ota` 四种用途和射频生命周期。
- `WifiProvisioningService`：只接受已认证 BLE 会话中的配网材料，暂存后等待设备端明确确认。
- `NtpService`：只负责一次校时会话及结果提交，不直接修改 UI。
- `WeatherService`：管理手机快照、位置、缓存、直连刷新和错误状态。
- `PowerService`：裁决当前电量是否允许普通或高功率 Wi-Fi 会话。
- UI：只读取快照、发起命令和显示确认，不拥有网络对象。

凭据最大为 32 字节 SSID、64 字节密码；天气响应最大 8,192 字节。两者都使用固定容量缓冲区，不能因远端输入改成无界字符串。

## 数据流与线程边界

```text
已认证 BLE 配网帧 -> 暂存凭据/nonce -> UI 明确确认 -> 持久化
                                               |
NTP/天气命令 -> request(WifiPurpose) -> 功耗门控 -> 连接 -> 单次业务
                                               |
                                  release() / 超时 / stop() -> 关射频

手机天气快照 -------------------------------> 有界缓存 -> UI 快照
直连天气 -> Wi-Fi -> HTTPS -> 有界解析 --------^
```

网络任务可以更新服务内部状态，但 LVGL 只能由 UI 主循环读取快照后更新。`request()` 与 `release()` 必须成对；每条成功、失败、超时路径都要归还用途位。

## 关键接口

| 项目 | 固定边界 |
|---|---:|
| SSID / 密码 | 32 B / 64 B |
| 连接超时 | 15,000 ms |
| 默认空闲关闭 | 60,000 ms |
| 长会话上限 | 15 min |
| 记忆的配网 nonce | 8 个 |
| 天气响应 | 8,192 B |
| 天气新鲜 / 陈旧 / 过期 | 3 h / 3 h 后 / 24 h |
| 天气请求超时 | 15,000 ms |

## 精选代码

来源：`libraries/FireflyOS/src/firefly/services/WifiService.h`，用途不是布尔开关，而是可组合的固定枚举。

```cpp
enum class WifiPurpose : uint8_t {
    Ntp = 0,
    Weather,
    Transfer,
    Ota,
};

static constexpr uint32_t kConnectionTimeoutMs = 15000;
static constexpr uint32_t kDefaultIdleTimeoutMs = 60000;
static constexpr uint32_t kLongSessionLimitMs = 15UL * 60UL * 1000UL;
static constexpr size_t kMaxSsidLength = 32;
static constexpr size_t kMaxPasswordLength = 64;
```

来源：`libraries/FireflyOS/src/firefly/services/WifiService.cpp`，所有用途统一经过配网和功耗门控。

```cpp
bool WifiService::purposeAllowed(WifiPurpose purpose) const {
    const uint8_t bit = purposeBit(purpose);
    if(bit == 0 || !provisioned()) return false;
    return !power_ || power_->allowsWifiSession(isHighPower(purpose));
}

void WifiService::stop() {
    active_purposes_ = 0;
    if(radio_ && mode_ != WifiMode::Off) radio_->disconnectAndPowerOff();
    mode_ = WifiMode::Off;
    setPowerSessionActive(false);
}
```

来源：`libraries/FireflyOS/src/firefly/services/WeatherService.h`，天气快照和响应缓冲区都有界。

```cpp
static constexpr size_t kMaxResponseBytes = 8192;
static constexpr uint32_t kStaleAfterSeconds = 3UL * 60UL * 60UL;
static constexpr uint32_t kExpireAfterSeconds = 24UL * 60UL * 60UL;
static constexpr uint32_t kRequestTimeoutMs = 15000;

char response_[kMaxResponseBytes + 1]{};
```

来源：`libraries/FireflyOS/src/firefly/services/WeatherService.cpp`，网络用途在结束路径显式释放。

```cpp
void WeatherService::finishNetworkRequest() {
    if(wifi_) wifi_->release(WifiPurpose::Weather, millis());
}
```

## 源码与测试映射

- Wi-Fi：`services/WifiService.h/.cpp`、`hal/Esp32WifiRadio.*`
- NTP：`services/NtpService.h/.cpp`、`hal/Esp32NtpClient.*`
- 天气：`services/WeatherService.h/.cpp`、`hal/Esp32WeatherHttpClient.*`
- 配网：`services/WifiProvisioningService.*`、BLE 敏感帧认证路径
- 测试：`test_wifi_service_contract.py`、`test_wifi_security_contract.py`、`test_ntp_service_contract.py`、`test_weather_service_contract.py`
- 文档：`docs/模块说明/09-WiFi天气与传输.md`

## 验证边界

自动测试能证明用途计数、响应上限、缓存时效、nonce 和失败路径契约；不能证明天线性能、路由器兼容性、TLS 根证书时效、弱网恢复、真实功耗或跨时区校时。Wi-Fi、NTP 和天气正式真机项仍为 `PENDING`。

## 已知问题

- 旧集成让多个后台任务高频轮询网络状态，增加共享状态、锁和调度复杂度。
- `Transfer` 与 `Ota` 属于高功率用途；临界电量下不能用普通天气成功经验推断它们可用。
- 天气直连依赖外部 HTTPS 服务，URL、证书、响应模式和限流都可能变化，必须通过适配层隔离。
- 配网 nonce 和记忆窗口只能防协议级重放，不能替代 BLE bond、MITM 与设备端用户确认。
- 任何凭据、生产端点或私有证书都不应写入本知识库。

## 基于 main 的复用步骤

1. 先实现只含 `Off/Connecting/Connected/Error` 的 Wi-Fi 状态机和单一 NTP 用途。
2. 真机验证连接、超时、断网、重连、空闲关射频及临界电量拒绝。
3. 接入已认证 BLE 配网，保留暂存与设备端确认，不允许收到帧即覆盖凭据。
4. 增加手机天气快照和缓存；先不启用直连 HTTPS。
5. 手机路径真机通过后，再加入直连天气、响应上限和所有失败路径释放。
6. 最后才允许 Transfer/OTA 申请高功率用途，并分别执行功耗和并发门槛。
