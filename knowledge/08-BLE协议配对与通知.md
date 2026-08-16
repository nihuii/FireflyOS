# BLE 协议、配对与通知

## 目标

当前 main 只有能力枚举和 UI 占位，没有 BLE 协议实现。本卡片保留旧实现中值得重新设计的帧边界、显式配对、敏感消息认证、严格 ACK 和通知隐私原则。

## 推荐边界

- BLE 回调只复制有界数据到队列，不解析业务、不调用 LVGL。
- 帧头、payload、ATT 分片和接收队列全部固定容量；大文件不走 BLE。
- 链路加密与应用认证分层：敏感帧还需 token、HMAC 和重放检查。
- 顺序固定为认证 → 重放判断 → 业务入队 → 推进 sequence → ACK。
- ACK 必须匹配类型、标志、序号和空载荷，不能宽松接受。
- 配对记录先暂存，双方确认成功后才稳定；通知默认只同步有界摘要。

## 最小实现

1. 冻结 Hello、PairRequest、PairConfirm、Ack、Error 五类消息。
2. 用 C++/Kotlin 共享黄金帧验证小端、CRC 和边界。
3. 真机完成 Secure Connections、MITM、用户确认和取消。
4. 加入敏感帧认证、严格 ACK、重放和断线恢复。
5. 每次只增加一种业务消息并单独验收。

## 主要风险

连接成功不等于安全业务会话就绪；认证前处理重复序号可能给伪造帧返回成功；单个服务承担 codec、session、pairing 和业务分发会形成复杂状态机。单元测试无法证明真实 MTU、系统配对弹窗、天线、重连和长连接功耗。

## 参考路径

- 当前能力占位：`libraries/FireflyOS/src/firefly/core/CapabilityRegistry.*`
- 当前事件边界：`libraries/FireflyOS/src/firefly/core/EventBus.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/protocol/`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/hal/BlePeripheralDevice.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/ConnectivityService.*`
- 设计计划：`docs/执行计划/05-BLE与Android伴侣应用.md`
