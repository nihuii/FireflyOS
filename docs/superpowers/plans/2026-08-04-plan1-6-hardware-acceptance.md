# FireflyOS 计划 1～6 真机功能验收表实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新建一份能直接指导六个 worktree 顺序真机验收的统一表，并把六分支现场、自动证据和待验边界同步到对话衔接版。

**Architecture:** 根目录验收表作为跨分支唯一总入口，先给出状态口径、公共准备和同步链，再以六个工作树分表承载可执行步骤、次数、通过标准和证据。对话衔接版只做项目现场摘要与导航，不复制整份矩阵。

**Tech Stack:** Markdown、Git worktree、Arduino/ESP32-S3、Android、PowerShell。

---

### Task 1: 建立计划 1～6 真机验收总入口

**Files:**
- Create: `docs/计划1-6真机功能验收表.md`

- [x] **Step 1: 写判定口径和公共记录模板**

写明 `PENDING/PASS/FAIL/BLOCKED`、自动验证不能替代真机、每项证据字段、烧录前 HEAD/产物哈希/设备信息、失败停止和最早责任分支规则。

- [x] **Step 2: 写六工作树总矩阵**

矩阵固定使用以下链：

```text
firefly-core -> ui-shell -> core-apps-power-imu -> storage-audio-media -> ble-android -> wifi-weather-ota
```

列出分支、Gate、新增职责、前置 Gate、修复责任和下游同步目标。

- [x] **Step 3: 写计划 1～4 分表**

逐项覆盖：

- 计划 1：基线页面、BOOT 30 次、息屏/唤醒 10 次、自动息屏 5 次、SRAM 85%、转场基线 +15% 上限。
- 计划 2：六张批准预览、圆角/48px、设置 50 次内存回收 95%、20 次转场、10 次息屏轮播和覆盖层优先级。
- 计划 3：RTC/闹钟/计时/设置、BOOT/PWR、QMI8658、静置误计步、100 次抬腕、3000 步对照、LightSleep 唤醒矩阵和四档功耗。
- 计划 4：SD 三场景各 10 次、AudioProbe、音频循环/铃声抢占、Files 分页、Music、Recorder、Themes、UI 响应、堆趋势和空闲功耗。

- [x] **Step 4: 写计划 5～6 分表**

计划 5 原样保留 D-01～D-12 的操作和通过标准。计划 6保留 E-01～E-12，并把 Wi-Fi 配网/NTP/天气、1MB/32MB 传输、SD/HTTPS OTA、各断电点、首启回滚、24 小时、400mAh、恢复出厂和隐私拆成可操作子项。

- [x] **Step 5: 写分支同步和总完成条件**

明确每阶段 `PASS` 后才允许在用户授权下合并本地 `main`、决定是否推送，再逐级 merge 下游；有 `FAIL/BLOCKED/PENDING` 时不得关闭对应 Gate。

### Task 2: 更新对话衔接版

**Files:**
- Modify: `docs/项目介绍-对话衔接版.md`

- [x] **Step 1: 更新 Git 与六工作树现场**

保留计划 1～5 已推送提交，增加 `wifi-weather-ota`：基于 `15d675d`，计划 6 改动尚未提交且工作树不干净；不得虚构新的提交哈希。

- [x] **Step 2: 增加计划 6 软件与自动证据摘要**

记录 Python 158/158、Android 21 套件 101 项、Debug APK 哈希、最终 Development 固件大小/哈希、Release 缺生产材料时失败闭锁，并明确这些不是 Gate E 真机通过。

- [x] **Step 3: 更新下一步和启动提示**

加入 `docs/计划1-6真机功能验收表.md`，把顺序扩展到第六分支，移除“不开始计划 6”，改为“不开始计划 7”；保留禁止删除/覆盖、禁止擅自 Git 操作和 LVGL 边界。

### Task 3: 文档一致性验证

**Files:**
- Verify: `docs/计划1-6真机功能验收表.md`
- Verify: `docs/项目介绍-对话衔接版.md`

- [x] **Step 1: 检查必需内容**

Run:

```powershell
rg -n "firefly-core|ui-shell|core-apps-power-imu|storage-audio-media|ble-android|wifi-weather-ota|D-01|D-12|E-01|E-12|PENDING" docs/计划1-6真机功能验收表.md
```

Expected: 六工作树、D/E 全编号和状态口径均有匹配。

- [x] **Step 2: 检查错误结论和占位符**

Run:

```powershell
rg -n "真机已通过|Gate [A-E] 已通过|TBD|TODO|FIXME" docs/计划1-6真机功能验收表.md docs/项目介绍-对话衔接版.md
```

Expected: 不出现无证据的通过结论或未定义占位符。

- [x] **Step 3: 检查 Git 范围**

Run:

```powershell
git diff --check
git diff --name-only -- image/图片生成提示词
git status --short
```

Expected: diff 格式通过，提示词目录无差异，仅新增/修改本轮文档及既有用户现场；不提交、不合并、不推送。
