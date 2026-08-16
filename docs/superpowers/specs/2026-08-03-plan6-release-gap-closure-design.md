# 计划 6 代码缺口收口设计

## 目标

在不提交、合并、推送或创建标签的前提下，补齐计划 6 审查确认的代码级缺口：让 HTTPS OTA 成为正式可达路径，建立不可误用开发密钥的发布构建门禁，补齐发布文档与真实性测试，并让恢复出厂的 SD 清理文案与实际安全边界一致。

本轮只完成软件实现和自动验证。24 小时稳定性、400mAh 续航、真机 Wi-Fi、断电 OTA、回滚和 Gate E 均保持 `PENDING`。

## 方案选择

采用已确认的 A+ 双源方案：

- SD OTA 保持可离线使用。
- HTTPS OTA 使用编译期固定的 base URL、host、CA 和受限文件名，不接受 BLE、HTTP 或其他未认证输入提供任意 URL。
- 开发构建可以没有线上端点；这时明确返回 `NoHttpsEndpoint`，但 SD OTA 仍可用。
- 发布构建必须同时提供生产 OTA 公钥和 HTTPS 配置，否则编译失败。
- 更新网络发现、TLS 握手、数据读取和 OTA 写入全部在固定后台任务执行；UI 回调只投递固定容量命令，LVGL 仍只由 UI 主循环访问。

## 组件边界

### 1. 发布配置与构建门禁

新增统一发布配置头，优先包含未跟踪的本地文件：

- `FireflyUpdatePublicKey.local.h`：只定义 65 字节非压缩 P-256 公钥 `kUpdatePublicKey`。
- `FireflyUpdateConfig.local.h`：定义 `FIREFLY_UPDATE_BASE_URL`、`FIREFLY_UPDATE_HOST`、`FIREFLY_UPDATE_CA_CERT`、清单文件名和固件文件名。

开发构建缺少本地文件时继续使用明确标记的测试公钥并关闭 HTTPS。发布构建定义 `FIREFLY_RELEASE_BUILD=1`；缺少任意本地文件、URL 不是 HTTPS、host 与 base URL 不一致或文件名不安全时直接编译失败。

`tools/build_firmware.ps1` 增加 `Development` 与 `Release` 配置。Release 只允许正式 `Firefly` 目标，并在调用 Arduino CLI 前检查两个本地头存在，再注入发布宏。`verify_all.ps1` 继续执行 Development 构建；发布清单单独记录 Release 命令和外部材料要求。

本地发布头加入 `.gitignore`，防止外部发布材料被意外纳入工作区差异。

### 2. HTTPS 清单与固件数据源

在 `UpdateSources` 中增加有界的 HTTPS 清单读取能力，并复用现有 TLS 安全规则：

- 只连接编译期 host 的 443 端口，保留原 host 作为 SNI/证书校验名称。
- 拒绝 HTTP、重定向、chunked、压缩响应、缺失或不匹配的 `Content-Length`。
- 清单响应上限使用 `UpdateManifestCodec::kMaxJsonBytes`，固件大小必须等于已验签 manifest 的 `size`。
- DNS、TLS、HTTP 头、读取停顿和总时长均使用单调绝对截止。
- 清单解析后仍由 `UpdateService::offer()` 执行产品、build、slot size 和 ECDSA 校验；下载前不会信任网络字段。

`HttpsUpdateSource` 的实例进入正式固件状态，不再是不可达类。

### 3. 后台更新协调器

新增固定容量、无 LVGL 依赖的 `UpdateCoordinator`：

- 命令固定为 `Check`、`Start`、`Cancel`，使用静态 FreeRTOS 队列，拒绝队列溢出而不覆盖旧命令。
- `Check` 先尝试受管 SD 清单；没有可用 SD 候选时请求 `WifiPurpose::Ota`，连接成功后读取 HTTPS 清单。
- HTTPS 清单成功后保持 OTA Wi-Fi 会话，直到取消、失败、完成或重启待处理；所有终态都释放 Wi-Fi。
- `Start` 在后台调用可能阻塞的数据源 `open()`，随后每轮只执行一个不超过 4096 字节的 `UpdateService::tick()`。
- `Cancel` 使用原有 first-terminal-wins 状态机；重复取消幂等。
- SD 更新不启动 Wi-Fi；HTTPS 未配置时产生明确 `NoHttpsEndpoint` 状态。
- UI 主循环只读取 `UpdateSnapshot` 并刷新 UpdateApp，不执行 DNS、TLS、SD 清单读取、OTA open/write/finalize。

### 4. 恢复出厂 SD 语义

继续保留七个受管子目录白名单和 SD 独占租约，不扩大为递归删除任意 `/FireflyOS` 内容。正式 UI、HTML 预览、模块说明和发布文档统一表述为“删除 FireflyOS 受管数据”，并列出 Music、Recordings、Pictures、Themes、Updates、Backups、Logs。未知文件和未知目录保留。

### 5. 发布文档与真实性门禁

新增：

- `docs/模块说明/10-OTA发布规范.md`
- `docs/模块说明/11-最终验收报告.md`
- `docs/模块说明/12-发布清单.md`
- `tests/python/test_release_documentation.py`

文档测试要求：

- 三份文档存在，并记录双槽地址/大小、80% 门禁、签名命令、开发/生产密钥区别、SD/HTTPS 更新流程、回滚、恢复出厂安全边界和发布阻断条件。
- 项目介绍、README 与架构总纲只陈述已实现的软件能力，不再把 Wi-Fi、天气或诊断写成未集成。
- 自动测试、固件编译和 APK 哈希与真机状态分栏记录。
- 24 小时、续航、真机 Wi-Fi、BLE、音频、SD、断电 OTA、回滚和 Gate E 必须是 `PENDING`，不得出现已创建 `v1.0.0-rc1` 的陈述。
- `verify_all.ps1` 必须显式运行该测试；缺文档时不能输出完整通过。

## 错误处理

- 未配置 HTTPS：不尝试连接，返回 `NoHttpsEndpoint`，SD 更新继续可用。
- Wi-Fi 门禁、连接超时或链路中断：停止来源、释放 `WifiPurpose::Ota`，当前启动槽不变。
- 清单解析、签名、build 或大小失败：不调用 writer。
- 固件短包、超长包、停顿、哈希或写入失败：只 abort 非活动槽一次，不选择启动槽。
- 发布配置不完整：Release 构建在编译前失败，Development 构建不得伪装成 Release。
- 命令队列满：UI 显示明确失败/忙状态，不同步执行更新工作。

## 测试策略

严格按 RED-GREEN 执行：

1. Python 契约先证明 HTTPS 实例、后台任务、Release 参数和发布文档当前缺失。
2. CoreTests 使用 fake catalog/source/radio/writer 验证双源选择、Wi-Fi 生命周期、取消和 first-terminal-wins。
3. 发布文档测试先因文件缺失和旧介绍内容失败，再补文档与校验入口。
4. 运行 Python 全集、Android 单测与 Debug APK、FireflyCoreTests、AudioProbe、Firefly 正式 Development 构建。
5. 运行 `git diff --check`、提示词目录差异检查和工作区状态检查。

Release 构建因为生产公钥与发布端点由仓库外管理，不伪造通过结果；自动测试只验证缺少材料时会可靠失败，以及提供测试配置时宏和构建参数正确注入。

## 非目标

- 不提供或猜测生产服务器域名、CA、生产私钥或生产公钥。
- 不上传固件、不部署更新服务器。
- 不执行真机、续航、断电或 Gate E 验收。
- 不创建 RC 标签，不提交、合并或推送。
- 不修改或覆盖 `image/图片生成提示词`。

