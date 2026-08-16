# FireflyOS 计划 6 Task 6–10 软件收口设计

日期：2026-08-02  
状态：已批准，待生成实施计划  
工作分支：`codex/wifi-weather-ota`

## 1. 范围与验收边界

本轮完成计划 6 Task 6–10 的软件实现、自动测试、构建链路和验收记录结构：

- 32MB 双 OTA 分区与构建后分区核验；
- 签名 OTA、非活动槽写入、首次启动确认和回滚请求；
- 正式系统更新 UI；
- 固定容量诊断服务；
- 恢复出厂、安全、隐私和模块降级的软件闭环；
- 中性基础主题检查、构建/烧录/恢复/OTA 文档与发布清单。

以下项目不能由自动测试替代，保持 `PENDING`：24 小时稳定性、400mAh 续航、真实 AP/HTTPS、OTA 各阶段断电、真机回滚、触摸时延、真实硬件故障注入和候选版本全矩阵。没有这些结果时不得创建或宣称通过 `v1.0.0-rc1`。本轮不提交、合并、推送或创建 Git 标签。

## 2. 总体架构

采用按依赖顺序执行的 A+ 方案：Task 6 分区是 Task 7 OTA 的硬前置；UpdateService、BootValidation 和 DiagnosticService 为 Task 8–10 提供统一状态与证据；恢复出厂调用各数据所有者的清理接口；发布文档只消费已验证事实。

所有服务保持固定容量。后台服务不访问 LVGL；正式 UI 只在 UI 主循环读取不可变快照并更新控件。耗时操作使用固定分块推进，不在 UI 回调中执行长阻塞工作。

## 3. 32MB 双 OTA 分区

`Firefly/partitions.csv` 是正式固件的唯一分区真源：

- NVS：`0x9000`，大小 `0x5000`；
- otadata：`0xE000`，大小 `0x2000`；
- ota_0：`0x10000`，大小 `0xB00000`；
- ota_1：`0xB10000`，大小 `0xB00000`；
- LittleFS：从 `0x1610000` 开始，大小 `0x9F0000`；
- 最终地址不得超过 `0x2000000`，分区不得重叠。

构建脚本在编译前解析并验证源 CSV，在编译后读取实际生成的分区二进制或 CSV 并核对名称、偏移和大小。Firefly 固件二进制必须小于 11MB 槽位的 80%，即不超过 9,227,468 字节；达到或超过该值时构建失败。测试与 AudioProbe 可以继续使用适合自身的测试布局，但不得让正式 Firefly 构建回退到旧 `app5M_fat24M_32MB` 布局。

## 4. 更新清单与信任模型

当前固件版本保持 `0.1.0`，当前 build 定为 100；自动测试使用 `0.1.1` / build 101 作为升级样例。文档不提前宣称 1.0.0 已发布。

清单包含 schema、product、version、build、min_build、size、SHA-256 和 64 字节 ECDSA P-256 签名。JSON 仅作为传输外壳；签名输入为确定性二进制编码：固定协议标识、定长/带长度字段、固定字节序的整数以及原始 SHA-256。签名字段本身不进入签名输入，从而不依赖 JSON 空白、字段顺序或文本编码差异。

`tools/sign_update.py`：

- 只从 `FIREFLY_SIGNING_KEY` 读取 PEM 私钥路径；
- 拒绝错误曲线、空字段、非普通文件、超槽文件和不合法 build；
- 从实际固件计算 size 与 SHA-256；
- 输出固定 schema、确定性字段顺序和十六进制 `r || s` 签名；
- 不输出或复制私钥内容。

仓库只包含明确标注的测试私钥夹具和对应开发公钥，测试私钥不得用于发布。普通开发构建可以使用开发公钥；`FIREFLY_RELEASE_BUILD=1` 时，如果没有提供本地 `FireflyUpdatePublicKey.local.h`，构建必须失败。该本地头文件和生产私钥均不进入仓库。

## 5. UpdateService 与数据流

UpdateService 是无 LVGL 的固定容量状态机，公开不可变 `UpdateSnapshot`。状态为：

`Idle → Available/Blocked → Downloading → Verifying → Writing → RebootPending → BootChecking → Completed`

失败分支为 `Failed`、`RollbackRequested` 和 `RolledBack`。首个终态原因保持不变，迟到取消或底层清理不得覆盖原始失败。

启动门禁：

- 电量至少 40%，或设备正在充电；
- Alarm 不在响铃；
- AudioUse 不是 Music/Recorder；
- BulkTransfer 不在活动状态；
- 不存在另一 OTA 会话；
- manifest product 匹配，build 高于当前 build，min_build 兼容，size 不超过槽位。

OTA 激活后反向阻止音乐、录音、Bulk 和新的高功耗会话。门禁失败返回明确枚举，不隐式重试。

SD 数据源只接受 `/FireflyOS/Updates` 下的受管路径。HTTPS 数据源由编译期可信 base URL、主机与 CA 配置；未配置时明确返回 `NoHttpsEndpoint`，SD 更新仍可用。URL 不从未认证输入直接决定，不允许 HTTP 降级或任意重定向。

清单签名先于包处理。包按固定缓冲写入非活动槽并同步计算 SHA-256；只有长度、哈希、`esp_ota_end` 和 boot partition 切换全部成功才进入 `RebootPending`。任何失败中止 OTA handle，不改变当前 boot partition。下载和校验阶段可取消；开始关键写入后 UI 不再提供取消。

## 6. 首次启动验证与回滚

BootValidation 仅在当前镜像状态为 `ESP_OTA_IMG_PENDING_VERIFY` 时启动，总截止 30 秒。检查固定顺序为 RTC、PMU、显示、触摸驱动、NVS 和主 UI：

- RTC/PMU/NVS 使用已有 HAL 或服务的健康结果；
- 显示要求初始化成功并存在有效显示对象；
- 触摸采用非交互式驱动初始化和一次非阻塞采样，不要求用户在 30 秒内触摸；
- 主 UI 要求 UiShell 建立并完成一次主循环心跳。

每项结果进入 DiagnosticService。全部通过后调用 `esp_ota_mark_app_valid_cancel_rollback()`；任一必需项失败或超时后记录原因并调用 `esp_ota_mark_app_invalid_rollback_and_reboot()`。非 pending-verify 的普通启动不得误触发回滚 API。

## 7. 系统更新 UI

正式页面采用用户选择并批准的 A“状态聚焦”方案。页面位于 410×502 圆角安全区，交互目标至少 48px；状态包括可用、门禁阻止、下载、校验、写入、等待重启、首次启动验证、完成、失败和已回滚。

每个状态只突出一个标题、一个说明、一个进度或主动作。版本、包大小和电量要求在开始前显示。下载/校验可取消；写入阶段使用 SAM 守护视觉并移除取消控件。失败与回滚页面提供次级诊断入口。页面只读取 UpdateSnapshot，不直接访问网络、SD、ESP OTA API 或加密库；隐藏页面停止动画。

## 8. DiagnosticService 与验收数据

DiagnosticService 保存固定 64 条环形记录和一个聚合快照。记录字段包括时间、触发原因、内部空闲堆、最低堆、最大连续块、PSRAM、UI/后台任务栈高水位、EventBus 当前/峰值/投递失败、PowerMode、重启原因和关键会话类型。

记录时机为每分钟，以及 Wi-Fi、Bulk、OTA、音频、录音、SD 和启动验证的开始/结束。RAM 环不周期写 Flash，避免磨损；用户显式请求时可输出到 Serial，或在 SD 可用且没有独占会话时导出到 `/FireflyOS/Logs`。导出失败不破坏 RAM 环。

最终验收报告预置 24 小时、400mAh、UI 性能、资源余量、安全和故障降级矩阵。自动得到的数据填写实际数值；依赖真机的单元保持 `PENDING`，不预填 PASS。

## 9. 恢复出厂、安全与降级

恢复出厂使用预检、确认、执行、汇总、重启五阶段。默认清理配对、Wi-Fi、通知、天气、设置和内部缓存，不删除 SD。各类数据由其所有者暴露幂等清理接口，协调层汇总结果；关键清理失败时不显示成功，也不提前重启。

“同时清除 SD FireflyOS 数据”使用独立第二次确认，并且仅允许 StorageService 在 SD 独占租约下删除精确根 `/FireflyOS/` 的受管内容。不得接受外部路径、通配符或父目录。完成后设备重启并以默认设置启动。

日志过滤禁止 Wi-Fi 密码、完整 SSID、配对 token、HMAC、通知正文和录音内容。Android 保持最小权限，不新增位置、通讯录、麦克风或广泛存储权限。

RTC、PMU、IMU、SD、Codec、BLE、Wi-Fi 的不可用状态映射到明确能力/诊断枚举；单模块失败不得阻断无关本地功能。生产加密 NVS 是否真正启用属于发布环境/真机验收项，软件只能检查配置并记录状态，不能据此宣称真机通过。

## 10. 中性主题与发布文档

新增中性基础主题标识 `system-default`。旧 `firefly-default` 作为兼容别名迁移到中性主题，避免破坏已有设置。系统核心、协议、HAL 和服务不得引用角色素材路径；流萤壁纸、睡眠图和角色资源继续作为可替换外壳资源，不迁入核心服务。

更新 `docs/项目介绍.md`、`Firefly/README.md` 和系统架构总纲，只描述已落地能力。新增 OTA 发布规范、最终验收报告和发布清单，记录构建、烧录、分区、配对、SD、OTA、恢复、已知限制、APK/固件哈希和真机 PENDING 项。

## 11. 错误处理与安全不变量

- 当前启动槽永不被写入；boot partition 只在完整验证后切换。
- 签名失败、哈希失败、短包、超长包、断流、写失败和取消均不得选择新槽。
- 状态机使用首个终态，清理幂等，重启后能区分普通启动、待确认新固件和已回滚结果。
- 所有路径通过 StorageService 的受管目录校验；恢复出厂默认不触碰 SD。
- 所有数组、队列、清单字符串、JSON 输入和 I/O 缓冲都有固定上限。
- LVGL 仅由 UI 主循环访问；服务通过快照或 EventBus 通知 UI。

## 12. 测试策略

严格按 TDD：每个行为先写失败测试，再写最小实现。

- Python：分区解析/重叠/边界/实际构建布局、清单编码、签名工具、错误密钥、超槽和发布文档契约；
- FireflyCoreTests：OTA 门禁、状态转移、取消边界、短包/哈希/签名失败、首终态、BootValidation、诊断环覆盖、恢复出厂默认与 SD 双确认、降级映射；
- 构建：FireflyCoreTests、AudioProbe、Firefly 正式固件，并验证固件大小低于 80%；
- Android：现有 101 项回归，不因计划 6 收口增加越权权限；
- 静态检查：核心服务不得引用角色素材或 LVGL，日志不得包含敏感字段；
- 真机矩阵：只创建可填写记录和执行说明，未执行项保持 `PENDING`。

## 13. 非目标

- 不创建或保管生产私钥；
- 不选择未经用户提供的生产发布域名；
- 不执行长时间或断电真机测试；
- 不创建 RC 标签；
- 不提交、合并或推送；
- 不删除或覆盖 `image/图片生成提示词`。
