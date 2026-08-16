# Android 伴侣应用

## 目标

当前 main 不包含 Android 工程。本卡片仅保存重建伴侣应用时的架构思路：系统权限显式可见、GATT 操作串行、业务状态与 Activity 解耦、持久化先于内存提交。

## 推荐边界

- Bluetooth 回调切回主 Looper，由固定 GATT 操作队列串行推进并设置超时。
- 连接、配对、可靠发送和业务同步分成独立组件，Activity 只渲染状态。
- 安全 Hello 获得严格 ACK 后才启用设置重放和通知桥。
- token、设置快照和解绑事务保存在应用私有目录并原子替换。
- 通知监听、媒体控制和日历访问分别检查普通权限与系统特殊访问。
- 临时 SoftAP 只绑定单次网络请求，不能永久绑定整个进程。

## 最小实现

1. 创建只显示权限状态和单次扫描的最小应用。
2. 加入 GATT 操作队列、MTU/CCCD 状态机和超时清理。
3. 接入最小配对与安全 Hello，完成多品牌手机矩阵。
4. 增加私有设置快照，再逐项增加通知、日历和媒体适配。
5. Wi-Fi 配网和大文件上传最后实现并单独验证取消路径。

## 主要风险

厂商 BLE 回调顺序不同；权限被拒、服务被系统回收和进程重启都必须可恢复；把所有页面和业务装进单个 Activity 会放大耦合。Debug APK 和 JVM 单元测试不能证明真实权限弹窗、后台限制或 BLE 互操作。

## 参考路径

- 当前 UI 预览：`docs/UI预览/04-Android伴侣/`
- 设计计划：`docs/执行计划/05-BLE与Android伴侣应用.md`
- 本地历史对象参考：`AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/`
- 本地历史对象参考：`AndroidCompanion/app/src/main/java/com/fireflyos/companion/sync/`
- 本地历史对象参考：`AndroidCompanion/app/src/main/java/com/fireflyos/companion/notifications/`
- 本地历史对象参考：`AndroidCompanion/app/src/test/`
