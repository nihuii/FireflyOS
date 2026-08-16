# FireflyOS OTA 发布规范

## 1. 适用范围

本规范约束计划 6 的固件签名、SD/HTTPS 双源更新、发布构建和回滚边界。自动测试与编译只证明代码路径可构建，不能替代断电、首启确认和真机回滚验收。

## 2. 固定分区边界

`Firefly/partitions.csv` 使用 16MB Flash：

| 区域 | Offset | Size | 十进制容量 |
| --- | ---: | ---: | ---: |
| `nvs` | `0x9000` | `0x5000` | 20,480 |
| `otadata` | `0xE000` | `0x2000` | 8,192 |
| `app0` | `0x10000` | `0xB00000` | 11,534,336 |
| `app1` | `0xB10000` | `0xB00000` | 11,534,336 |
| `spiffs` | `0x1610000` | `0x290000` | 2,686,976 |

每个 OTA 槽的发布预算为槽容量的 80%，即 `9,227,468` 字节。清单声明大小、实际 Content-Length 和已写入字节数都不得超过槽容量；发布检查以 80% 预算作为更严格门禁。

## 3. 清单与离线签名

清单格式由 `UpdateManifestCodec` 固定，包含 schema、product、version、build、min_build、size、SHA-256 和 ECDSA P-256 签名。`tools/sign_update.py` 从环境变量 `FIREFLY_SIGNING_KEY` 获取离线私钥路径；私钥不得进入仓库、构建日志、APK 或 SD 发布包。

固件先计算 SHA-256，再对规范化签名消息执行 ECDSA P-256 签名。设备必须先验证清单签名和产品/版本/容量/升级序列，再接受固件流；下载过程中再次流式计算 SHA-256，摘要不一致时不得切换启动槽。

## 4. Release 构建门禁

正式构建使用：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target Firefly -Configuration Release
```

该入口注入 `FIREFLY_RELEASE_BUILD=1`，且仅允许 `Firefly` 目标。仓库外必须提供：

- `FireflyUpdatePublicKey.local.h`：生产 ECDSA P-256 公钥。
- `FireflyUpdateConfig.local.h`：固定 HTTPS 主机、基础路径、CA、清单名和固件名。

任一文件缺失时 Release 构建必须失败闭锁。Development 构建使用测试公钥并允许 SD 更新，但 HTTPS 未配置时明确返回无端点，不能被标记为发布固件。

本地配置头必须把 `FIREFLY_UPDATE_BASE_URL`、`FIREFLY_UPDATE_HOST`、`FIREFLY_UPDATE_CA_CERT`、`FIREFLY_UPDATE_MANIFEST_FILE` 和 `FIREFLY_UPDATE_FIRMWARE_FILE` 定义为非空字符串字面量；清单名只能是单一安全 `.json` 文件名，固件名只能是单一安全 `.bin` 文件名。公钥头在 `namespace firefly` 中提供 65 字节未压缩 P-256 `kUpdatePublicKey`。空值、错误类型或缺失符号均应在 Release 编译期失败。

Development 全量构建入口为 `powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1`。烧录时必须保持 ESP32-S3、32MB Flash、OPI PSRAM、QIO 与项目 `Firefly/partitions.csv` 一致；由操作者选择实际串口后使用 Arduino IDE 或同一 FQBN 的 Arduino CLI upload。脚本成功编译不等于已经烧录。

## 5. SD 与 HTTPS 双源规则

检查更新由无 LVGL 的后台 `UpdateCoordinator` 拥有：

1. 优先读取 SD 的 `/FireflyOS/Updates/update.json`。
2. SD 清单不存在时，才申请 `WifiPurpose::Ota` 临时会话并读取固定 HTTPS 清单。
3. SD 清单存在但格式或签名错误时显式失败，不回退到网络来掩盖损坏。
4. 清单来源决定固件来源：SD 清单配 SD 固件，HTTPS 清单配 HTTPS 固件，不混用信任上下文。
5. HTTPS 只接受固定主机、SNI、受信 CA、状态码 200、identity 编码和有界 Content-Length；不跟随重定向。
6. Available、Downloading、Verifying、Writing 和 RebootPending 期间阻止录音、音乐、批量传输和 LightSleep 冲突。

UI 线程仅投递 Check/Start/Cancel 命令并读取固定快照；后台任务不访问 LVGL。

## 6. 写入、首启确认与回滚

更新前要求电量至少 40% 或正在充电，且无闹钟响铃、录音、音乐和文件传输。目标 build 必须高于当前 build。写入始终指向非活动 OTA 槽，任何解析、签名、摘要、容量、网络或写入失败都保留当前可启动槽。

新固件首次启动在 30 秒确认窗口内检查 RTC、PMU、显示、触摸、NVS 和主 UI。成功后标记新槽有效；失败则标记无效并回滚重启。下载 25%、写入 25%/50%/75%、重启前和首启自检阶段的断电回滚均为真机项目，完成前保持 `PENDING`。

## 7. 发布证据边界

- Python、Android 单元测试和编译结果属于自动软件证据。
- SD/HTTPS 真机下载、断电 OTA、首启确认、旧槽回滚和长期稳定性属于硬件证据。
- 未完成 Gate E 前禁止声明正式发布、RC 已通过或回滚已在真机验证。
