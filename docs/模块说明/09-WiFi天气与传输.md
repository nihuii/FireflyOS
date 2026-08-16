# Wi-Fi、天气与临时文件传输

## 边界与线程模型

- `WifiService` 是唯一的 Wi-Fi 会话仲裁器。Station 与 SoftAP 互斥，普通会话空闲 60 秒关闭，连接最长 15 秒，Transfer/OTA 最长 15 分钟。
- BLE 服务、天气 HTTPS 和文件 HTTP 分属独立 FreeRTOS 任务；`WifiService`、`WeatherService`、`BulkTransferService` 使用固定静态递归互斥量保护跨核状态。LVGL 仍只由 UI 主循环访问。
- 低电量边界为包含式：已知电量 `<=5%` 时即使充电或存在 VBUS 也拒绝全部新 Wi-Fi 会话，`<=15%` 拒绝未充电的 Transfer/OTA。百分比不在 `0..100`（包括部分遥测中的 `-1`）时按未知遥测处理：允许短会话、拒绝高功耗会话。

## BLE 安全配网

- `WifiProvision` 仅在 bonding、会话认证和 HMAC 验证完成后交给业务层。
- Android 默认发送 schema 2：60 秒相对 TTL、8 字节 nonce、定长上限 SSID 32 字节/密码 64 字节，不依赖手机墙上时钟或手表首次启动时尚未校准的 RTC。schema 1 仅使用 `TimeService` 已验证的 RTC 时间兼容。设备按单调时钟保存最近 8 个有效窗口内 nonce，确认窗口不超过 payload TTL；8 个槽均仍有效时拒绝第 9 个请求而不逐出重放记录，到期槽才可复用。设备拒绝 A→B→A 重放，也拒绝覆盖待确认或正在连接的请求。
- 确认配网时，正在 Connecting、Connected、SoftAP 或仍有活动 purpose 的射频会话返回 `Busy`，不能被配网或遗忘动作抢占；普通 Station 失败留下的无活动 `Error` 状态会在下一次请求前归一为 `Off`，因此仍可进入 SoftAP、替换错误凭据或确认遗忘。
- 手表只显示 SSID；密码不进入 UI 或日志。凭据仅在首次连接成功后写入专用 `ff_wifi` NVS 命名空间。遗忘操作只有在 NVS 删除成功后才报告 `Forgotten`，否则报告 `PersistenceFailed` 并保留可恢复状态。
- 遗忘网络通过单一敏感状态清理入口同时清除 NVS 凭据、内存中的 SSID/密码、待确认状态和 nonce 重放缓存；该入口允许重复调用。
- 当前开发配置将凭据作为 NVS 二进制记录保存。正式发布前必须在生产烧录配置启用 Flash Encryption/加密 NVS，或在发布威胁模型中明确接受物理提取风险；自动构建不证明该生产安全项已满足。

## NTP 与天气

- 手机天气优先，3 小时后标记旧、24 小时后明确显示较早数据但不丢弃。直接请求仅在手机数据不新鲜时替代当前快照。
- Open-Meteo 请求固定字段、固定主机、HTTPS CA、15 秒总预算和 8 KB 响应上限；DNS 使用固定静态回调上下文异步解析并最多占用前 5 秒，随后以解析后的 IP 建连但仍传入原主机做 SNI/证书域名验证。TCP、TLS 和请求写入共享 DNS 后的剩余绝对预算，每个阻塞阶段最多取得扣除响应与调度余量后的三分之一；写入前再次确认剩余时间足以覆盖连接时固化的 TLS socket 超时。之后由有界手工响应读取器按同一个绝对单调时钟截止点解析状态行、响应头、Content-Length、chunked 或关闭定界正文，不会因响应头或正文慢速滴答而刷新超时窗口。已声明长度的短响应和未知长度但耗尽预算的部分响应均被拒绝。解析器在 `current`/`daily` 对象范围内做有界数字解析，不读取 units 对象。
- 天气快照使用带校验和的双代缓存：新记录写入 `.part` 并验证，旧记录暂存 `.bak`，替换失败时回滚。
- `TimeService` 的跨核公开状态统一受静态递归互斥量保护，闹钟响铃标志使用原子变量，后台 NTP 每轮只取得一次一致快照。SNTP 在成功、失败、purpose 失活和闹钟响铃延期路径上都经过同一停止入口；闹钟响铃时同时释放 NTP purpose，闹钟关闭后重新申请一次校时会话。首次配网成功后也会请求校时。RTC 仅在无效或偏差超过 2 秒时写回。

## 临时文件传输

- Android 先流式计算文件大小和 SHA-256，再发送 schema 2 BLE Start 命令。命令携带非零 `request_id`、受管路径、声明大小和 SHA-256；手表在生成一次性 128-bit token 前完成路径、大小、电量、音频/OTA 互斥、SD 和剩余空间检查。Ready、Busy、完成和失败均回显 `request_id`，不会把旧会话结果误配给新文件。
- 会话空闲上限 5 分钟，绝对上限 15 分钟；持续传输只能刷新空闲期限，不能延长绝对期限。
- 完成结果与活动资源分离保存：成功后的 HTTP/Wi-Fi/SD 清理完成即可启动下一文件会话，迟到的同 ID Cancel 仍能重发上一终态，不需要重启设备。
- 优先共享私有局域网；Station 连接在 15 秒截止点失败或其 purpose 已被 Wi-Fi 状态机释放时，立即尝试单客户端 SoftAP，不会把该回退竞争误报为超时。活动传输真正丢失网络时报告 `NetworkUnavailable`，只有绝对/空闲截止才报告 `Timeout`。Android 对 SoftAP 使用系统 `WifiNetworkSpecifier`，并在 Android 13+ 请求 Nearby Wi-Fi 运行时权限。
- HTTP 仅开放 `/upload`，要求 Bearer token、受管路径、纯十进制声明大小和 SHA-256；无目录浏览。认证后缺失或格式错误的元数据立即形成对应 `InvalidPath`、`SizeMismatch` 或 `HashMismatch` 终态并断开客户端，不继续接收文件正文。Android 只接受私有 IPv4、80 端口的该路径。
- 仅允许 `/FireflyOS/Themes`、`Pictures`、`Music`、`Updates` 四个根目录的直接子文件，禁止嵌套路径；正式目标不得以保留后缀 `.part` 结尾，避免启动清理误删已提交文件。单文件最多 64 MB。SD 操作全部经过 `StorageService` 互斥路径；普通文件和目录句柄有固定计数，未全部关闭时 Bulk 独占租约返回 `Busy`，已持有者在 Bulk 状态下仍可串行关闭。传输先写 `.part`，流式校验大小和 SHA-256，最后同卷改名且不覆盖正式文件。
- Android 的取消按钮、权限拒绝、SoftAP 获取失败、上传异常和 Activity 销毁共用幂等取消入口：取消协程、主动断开可能阻塞的 HTTP 连接、释放临时网络，并在仍有关联会话时发送 schema 2 Cancel 命令；重复选文件通过操作代次取消旧的哈希准备或传输，Ready/SoftAP 回调通过启动门闩最多启动一次上传。SoftAP 可用/不可用回调同时核对活动 `request_id` 与待处理 session，迟到回调不能启动或取消新会话。手表对每个控制命令发布相关结果代次；已认证的路径、大小、哈希、空间或写入拒绝立即终止 HTTP 并保留精确失败原因，同 ID 的迟到 Cancel 仅重发原终态，不把它改写成 `Cancelled`。
- 取消、断连、低电、拔卡、超时和校验失败会删除 `.part`、停止 HTTP/Wi-Fi，并通过 BLE 状态和手表本地覆盖层报告结果。启动挂载或拔卡后重新挂载 SD 时，都会非递归扫描四个受管根目录，只清理普通 `.part` 文件，不删除正式文件或越出受管目录。

## 验证边界

### 恢复出厂的 SD 清理范围

“Delete managed FireflyOS data”只清理 FireflyOS 创建并管理的七个目录：
`Music`、`Recordings`、`Pictures`、`Themes`、`Updates`、`Backups` 和
`Logs`。清理必须取得 SD 独占租约，不接受调用方传入路径、通配符或父目录。
`/FireflyOS` 下的未知文件和未知目录会保留，避免把不属于系统的数据误删。

自动测试包含 1 KB、1 MB、32 MB 生成式 InputStream，验证 Android 上传读取缓冲不超过 64 KB；这仍只证明流式代码边界。自动测试和编译不等同于真机通过。电流、真实 AP/隐藏网络、TLS 握手、真实文件 1 MB/32 MB 传输、传输中拔卡/断连、UI 视觉和触摸延迟仍必须按真机矩阵记录为 `PASS/FAIL`；未执行时保持 `PENDING`。
