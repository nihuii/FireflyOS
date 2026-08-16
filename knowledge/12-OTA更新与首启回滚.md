# OTA 更新与首启回滚

## 来源

- 固定提交：`e4fb0d1ff71bcc0330b507fa90c653be9929e611`
- 主要路径：`services/UpdateManifest.*`、`UpdateService.*`、`UpdateCoordinator.*`、`UpdateSources.*`、`BootValidationService.*`
- 取回示例：`git show "e4fb0d1:libraries/FireflyOS/src/firefly/services/UpdateService.h"`

## 复用等级

**需要重构后复用，安全边界必须原样保留或加强。** 签名清单、等大双 OTA 分区、运行时门控、流式摘要、下次启动分区选择和首启回滚是不可降级要求。旧主程序集成与后台轮询只能作为装配参考。

## 模块定位

OTA 数据面支持 SD 和 HTTPS 两种来源，协调器优先读取 SD，缺失时才回退 HTTPS。包写入非当前 OTA 分区前必须验证清单签名和运行时资源；写完后核对 SHA-256，选择下次启动分区。新固件首启在 30 秒内完成最小硬件/UI 检查后才标记有效，否则请求回滚。

## 职责与边界

- `UpdateManifest`：限制 JSON 大小、字段长度、产品、版本、build、包大小、摘要和 ECDSA P-256 签名。
- `UpdateCoordinator`：有界命令队列，协调 SD/HTTPS 来源并拥有 `WifiPurpose::Ota`。
- `UpdateService`：预检、下载、验证、写分区、选择启动、取消和结果快照。
- `BootValidationService`：仅在 pending-verify 启动时执行，按顺序收集检查并决定确认或回滚。
- `UpdateTrustAnchor`：Development 可用测试公钥；Release 必须从仓库外本地头文件取得生产公钥。
- 发布脚本：Release 缺生产公钥或 HTTPS 配置时立即失败，不产生可发布镜像。

知识库只记录接口和闭锁规则，不保存生产私钥、公钥材料、真实端点或证书。

## 数据流与线程边界

```text
Check 命令 -> SD manifest --缺失--> HTTPS manifest -> 解析/签名/版本/资源预检
                                                        |
Start 命令 -> SD package  --缺失--> HTTPS package -> 4 KiB 流式写 OTA 分区
                                                        |
                                                 SHA-256 一致
                                                        |
                                              select next boot + reboot
                                                        |
pending verify -> RTC -> PMU -> Display -> Touch -> NVS -> MainUi
                  全部通过 -> mark valid
                  任一失败/30s 超时 -> mark invalid and rollback
```

后台协调器只能推进 OTA 状态和发布快照，不能调用 LVGL。启动确认必须由真实初始化阶段逐项提交，不能在一个函数里无条件把所有检查写成通过。

## 关键接口

| 项目 | 固定边界 |
|---|---:|
| manifest JSON | 最大 1,024 B |
| product / version | 各 16 B |
| canonical signed bytes | 86 B |
| 签名 | ECDSA P-256，64 B `r||s` |
| OTA 写块 | 4,096 B |
| 写入停滞超时 | 15 s |
| 单个 OTA slot | `0xB00000`，11 MiB |
| 首启确认时限 | 30 s |
| 协调命令队列 | 4 项 |

## 精选代码

来源：`Firefly/partitions.csv`，两个应用槽等大且存在 `otadata`。

```csv
nvs,        data, nvs,     0x9000,    0x5000,
otadata,    data, ota,     0xE000,    0x2000,
app0,       app, ota_0,    0x10000,   0xB00000,
app1,       app, ota_1,    0xB10000,  0xB00000,
littlefs,   data, spiffs,  0x1610000, 0x9F0000,
```

来源：`libraries/FireflyOS/src/firefly/services/UpdateService.cpp`，运行时门控首先处理电量，再排除互斥会话。

```cpp
UpdateFailure UpdateService::preflightFailure(const UpdateRuntimeGate & gate) const {
    if(!gate.charging && (!gate.battery_valid || gate.battery_percent < 40)) {
        return UpdateFailure::LowPower;
    }
    if(gate.alarm_active) return UpdateFailure::AlarmActive;
    if(gate.music_active) return UpdateFailure::MusicActive;
    if(gate.recording_active) return UpdateFailure::RecordingActive;
    if(gate.transfer_active) return UpdateFailure::TransferActive;
    if(gate.ota_active) return UpdateFailure::Busy;
    return UpdateFailure::None;
}
```

来源：`libraries/FireflyOS/src/firefly/services/BootValidationService.h`，首启检查是固定有界集合。

```cpp
enum class BootValidationCheck : uint8_t {
    Rtc = 0,
    Pmu,
    Display,
    Touch,
    Nvs,
    MainUi,
    Count,
};

static constexpr uint32_t kValidationDeadlineMs = 30000;
```

来源：`tools/build_firmware.ps1`，Release 输入缺失时失败闭锁。

```powershell
if($Configuration -eq 'Release') {
    if(-not (Test-Path -LiteralPath $releasePublicKey -PathType Leaf)) {
        throw 'Release OTA public key is missing: FireflyUpdatePublicKey.local.h'
    }
    if(-not (Test-Path -LiteralPath $releaseUpdateConfig -PathType Leaf)) {
        throw 'Release HTTPS configuration is missing: FireflyUpdateConfig.local.h'
    }
}
```

## 源码与测试映射

- 清单与签名：`services/UpdateManifest.h/.cpp`、`UpdateTrustAnchor.h`
- 更新状态机：`services/UpdateService.h/.cpp`
- 来源与协调：`services/UpdateSources.h/.cpp`、`UpdateCoordinator.h/.cpp`
- 首启回滚：`services/BootValidationService.h/.cpp`、ESP32 boot control adapter
- 分区与构建：`Firefly/partitions.csv`、`tools/build_firmware.ps1`
- 测试：`test_update_manifest_contract.py`、`test_update_service_contract.py`、`test_update_coordinator_contract.py`、`test_boot_validation_contract.py`、`test_release_contract.py`
- 文档：`docs/模块说明/10-OTA发布规范.md`

## 验证边界

自动测试和 Development 编译可以证明格式、状态迁移、失败闭锁与尺寸检查；不能证明真实 flash 掉电恢复、双分区切换、首启失败回滚、HTTPS 证书、生产签名或发布设备兼容性。Release 因仓库外生产配置缺失而失败是设计结果，不是 Release 验收通过。OTA 和回滚正式真机项仍为 `PENDING`。

## 已知问题

- Development 测试公钥绝不能出现在 Release 信任链；知识库也不复制其字节。
- 不能先写包再补验签；清单签名、产品、build、最小 build、大小和资源门控必须在写入前通过。
- SHA-256 只证明包与已签清单一致，签名才提供来源真实性，两者不可互相替代。
- `MainUi` 通过必须代表界面真实建成且主循环运行，不能在初始化函数末尾无条件提交。
- 新架构如果改变分区表，必须重新验证 bootloader、otadata、两个 slot 和文件系统都不重叠。

## 基于 main 的复用步骤

1. 先冻结目标板分区表并做静态重叠/尺寸检查，不接网络和 UI。
2. 独立实现清单 canonicalization、签名验证、产品/build/大小拒绝和测试向量。
3. 用内存 writer 验证状态机、4 KiB 流、摘要、取消和所有资源门控。
4. 接入 SD 单源，在真机验证写非当前槽、断电/坏包失败和临时文件策略。
5. 接入首启 pending-verify，逐个真实检查并完成成功确认与失败回滚。
6. 最后增加 HTTPS 回退、生产信任锚注入和 Release 失败闭锁；未经完整发布验收不得创建 RC。

