# Arduino 稳定分区构建设计

日期：2026-08-16
状态：用户已批准实施
适用基线：`main@0ec8d82`

## 问题

当前 ESP32 Arduino 2.0.17 的 `esp32s3` 板卡定义同时由 `FlashSize=32M` 和 `PartitionScheme=app5M_fat24M_32MB` 写入 `build.partitions`。现有构建脚本把 `FlashSize` 放在 `PartitionScheme` 前，最终可能选择不存在的 `app5M_fat24M_32MB.csv`；交换顺序后会正确选择平台自带的 `large_fat_32MB.csv`。

Arduino IDE 生成菜单参数的顺序不应成为项目可编译的隐含条件，因此仅调整脚本不足以稳定 IDE 构建。

## 设计

1. 在 `Firefly/partitions.csv` 固定当前 32MB FATFS 布局。内容与 ESP32 Arduino 2.0.17 的 `large_fat_32MB.csv` 一致：NVS、OTA data、两个 `0x480000` 应用槽、FATFS 和 coredump，覆盖到 32MB Flash 末端且互不重叠。
2. 在 `tools/build_firmware.ps1` 中把 `PartitionScheme` 放在 `FlashSize` 前。即使本地分区文件被误删，命令行构建仍会解析到平台现有的 `large_fat_32MB.csv`。
3. 新增标准库 `unittest` 契约，检查本地分区文件存在、布局连续合法、总边界不超过 32MB，以及 FQBN 参数顺序正确。
4. 使用全新 `.build/Firefly-stable-fix` 目录执行完整 Firefly 编译，避免旧缓存中的 `partitions.csv` 掩盖缺陷。

## 非目标

- 不升级 ESP32 Arduino 开发板包。
- 不引入计划 6 的两个 11MiB OTA 槽。
- 不修改 FireflyOS 业务代码、LVGL 或硬件驱动。
- 不提交、合并或推送。

## 成功条件

- 回归测试在修复前因缺少 `Firefly/partitions.csv` 和错误参数顺序失败。
- 修复后回归测试通过。
- 全新构建目录中的完整固件编译退出码为 0，并报告程序与动态内存占用。
- Git 变更只包含设计、计划、测试、分区表和构建脚本。
