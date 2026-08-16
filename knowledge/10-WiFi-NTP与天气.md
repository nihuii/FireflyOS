# Wi-Fi、NTP 与天气

## 目标

当前 main 只有 Wi-Fi 能力枚举和视觉占位，没有网络服务。新实现应把 Wi-Fi 当作按用途租用的高功耗资源：NTP、天气、传输和 OTA 分别申请，结束后释放。

## 推荐边界

- Wi-Fi 状态机、凭据、响应和用途集合均固定容量。
- 配网材料只接受已认证会话，先暂存，再由手表明确确认后持久化。
- 所有成功、失败、超时和取消路径都必须归还用途租约并按策略关闭射频。
- NTP 只提交时间结果；RTC 写回由时间服务决定。
- 天气优先接收手机快照，缓存区分新鲜、陈旧和过期；直连 HTTPS 后加。
- 密码、完整 SSID、生产端点和证书不得进入日志或知识库。

## 最小实现

1. 只实现 Off、Connecting、Connected、Error 和单一 NTP 用途。
2. 真机验证连接、超时、断网、空闲关射频和低电拒绝。
3. 接入认证配网及设备端确认。
4. 增加手机天气快照和缓存，不启用直连。
5. 最后加入 HTTPS 适配、响应上限，并分别开放传输与 OTA 用途。

## 主要风险

多个高频网络任务会增加锁和功耗；普通天气成功不能证明传输或 OTA 可用；外部服务的证书、响应和限流会变化，必须隔离在适配层。自动状态测试不能证明路由器兼容性、弱网恢复、TLS 和真实功耗。

## 参考路径

- 当前能力占位：`libraries/FireflyOS/src/firefly/core/CapabilityRegistry.*`
- 当前预览：`docs/UI预览/05-天气与更新/`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/WifiService.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/WifiProvisioningService.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/NtpService.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/WeatherService.*`
