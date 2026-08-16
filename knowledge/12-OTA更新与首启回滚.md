# OTA 更新与首启回滚

## 目标

当前 main 已有 32MB app0/app1 双应用分区布局和静态分区测试，但没有更新状态机、签名清单或首启确认服务。后续实现必须坚持“写入前验签、写入后验摘要、首启失败可回滚、Release 缺材料即失败”。

## 推荐边界

- 两个应用槽等大且不与 FFat、NVS、otadata、coredump 重叠。
- 清单固定容量，验证产品、版本、build、包大小、SHA-256 和 ECDSA P-256 签名。
- 只有清单和运行时资源门控通过后才写非当前槽；流式写入使用固定块。
- SD 与 HTTPS 是两个适配来源，状态机不依赖具体传输实现。
- pending-verify 首启在固定期限内逐项验证 RTC、PMU、显示、触摸、NVS 和主 UI。
- Development 测试信任锚与 Release 生产信任锚严格分离，缺生产输入时不生成发布产物。

## 最小实现

1. 先锁定当前分区表并持续做重叠、尺寸和镜像容量测试。
2. 独立实现 canonical 清单、签名验证和拒绝向量。
3. 用内存 writer 测试流写、摘要、取消和资源门控。
4. 接入 SD 单源，真机验证坏包、断电和非当前槽写入。
5. 实现 pending-verify 的真实成功确认与失败回滚。
6. 最后加入 HTTPS、生产信任锚注入和 Release 闭锁。

## 主要风险

SHA-256 只证明包与清单一致，签名才证明来源；不能写完固件后再补验签；主 UI 检查必须代表界面和主循环真实工作。当前双分区仅证明布局存在，不表示 OTA、回滚或发布已经可用。

## 参考路径

- 当前：`Firefly/partitions.csv`
- 当前：`tests/python/test_build_configuration.py`
- 当前构建入口：`tools/build_firmware.ps1`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/UpdateManifest.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/UpdateService.*`
- 本地历史对象参考：`libraries/FireflyOS/src/firefly/services/BootValidationService.*`
