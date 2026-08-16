# Arduino Stable Partition Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 Arduino IDE 和项目构建脚本都不依赖 ESP32 2.0.17 菜单参数顺序，稳定使用当前 32MB FATFS 分区布局。

**Architecture:** 由草图目录内的 `partitions.csv` 提供第一优先级的项目分区表，同时纠正 CLI FQBN 的菜单顺序作为后备。Python 契约从仓库真实文件解析边界并阻止分区表或构建参数回退。

**Tech Stack:** Arduino CLI、ESP32 Arduino 2.0.17、PowerShell、CSV、Python 3 `unittest`。

---

### Task 1: 建立失败的构建配置契约

**Files:**
- Create: `tests/python/test_build_configuration.py`

- [ ] **Step 1: 写分区与 FQBN 顺序测试**

测试应读取 `Firefly/partitions.csv` 和 `tools/build_firmware.ps1`，断言本地分区文件存在，分区无重叠、应用槽等大、Flash 末端为 `0x2000000`，并断言 `PartitionScheme=app5M_fat24M_32MB` 出现在 `FlashSize=32M` 前。

- [ ] **Step 2: 验证测试按预期失败**

Run: `python -m unittest tests.python.test_build_configuration -v`
Expected: 两项失败，原因分别为本地分区表缺失和 FQBN 参数顺序错误。

### Task 2: 固定本地分区并修正脚本

**Files:**
- Create: `Firefly/partitions.csv`
- Modify: `tools/build_firmware.ps1:8`

- [ ] **Step 1: 写入当前 32MB FATFS 布局**

分区内容固定为 NVS `0x9000/0x5000`、OTA data `0xe000/0x2000`、app0 `0x10000/0x480000`、app1 `0x490000/0x480000`、FFat `0x910000/0x16E0000`、coredump `0x1FF0000/0x10000`。

- [ ] **Step 2: 修正 FQBN 参数顺序**

将脚本中的相关片段改为：

```powershell
FlashMode=qio,PartitionScheme=app5M_fat24M_32MB,FlashSize=32M,PSRAM=opi
```

- [ ] **Step 3: 验证回归测试转绿**

Run: `python -m unittest tests.python.test_build_configuration -v`
Expected: 所有测试通过。

### Task 3: 全量验证与范围审计

**Files:**
- Verify: `Firefly/`
- Verify: `tools/build_firmware.ps1`
- Verify: `tests/python/test_build_configuration.py`

- [ ] **Step 1: 运行全部 Python 测试**

Run: `python -m unittest discover -s tests/python -v`
Expected: 全部测试通过。

- [ ] **Step 2: 使用全新目录编译 Firefly**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,PartitionScheme=app5M_fat24M_32MB,FlashSize=32M,PSRAM=opi,LoopCore=0,EventsCore=0 --libraries ./libraries --build-path ./.build/Firefly-stable-fix --warnings all ./Firefly`
Expected: 退出码 0，并输出程序存储空间和动态内存占用。

- [ ] **Step 3: 核对 Git 范围**

Run: `git status --short` 和 `git diff --check`
Expected: 只出现本计划列出的文件，无行尾空白错误。遵照用户约束，不执行 `git add`、`git commit` 或 `git push`。
