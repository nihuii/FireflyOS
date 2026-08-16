# Plan 6 Release Gap Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 接通双源后台 OTA、建立发布构建门禁、补齐发布真实性文档和测试，同时保持所有真机项目为 `PENDING`。

**Architecture:** `UpdateSources` 提供固定配置的 HTTPS 清单与固件读取；`UpdateCoordinator` 用静态命令队列在后台拥有检查、启动、取消、tick 和 Wi-Fi 生命周期；UI 只投递命令和读取快照。Release 构建必须注入仓库外的生产公钥与 HTTPS 配置，Development 构建保持 SD OTA 可用。

**Tech Stack:** ESP32 Arduino、FreeRTOS、LVGL 8.3.11、mbedTLS、PowerShell、Python unittest、Android Gradle。

---

### Task 1: 建立发布与 OTA 运行时失败契约

**Files:**
- Create: `tests/python/test_release_documentation.py`
- Modify: `tests/python/test_companion_features_contract.py`

- [x] **Step 1: 写发布文档失败测试**

新增测试，要求三份模块文档存在、项目介绍不再声称 Wi-Fi/天气/诊断未集成、所有硬件矩阵包含 `PENDING`、`verify_all.ps1` 显式运行本测试。

```python
def test_release_documents_exist_and_keep_hardware_pending(self):
    required = [
        ROOT / "docs/模块说明/10-OTA发布规范.md",
        ROOT / "docs/模块说明/11-最终验收报告.md",
        ROOT / "docs/模块说明/12-发布清单.md",
    ]
    for path in required:
        self.assertTrue(path.is_file(), path)
    report = required[1].read_text(encoding="utf-8")
    self.assertIn("24 小时", report)
    self.assertIn("PENDING", report)
    self.assertNotIn("v1.0.0-rc1 已创建", report)
```

- [x] **Step 2: 写 OTA 正式接线失败测试**

要求 `FireflyState.cpp` 实例化 `HttpsUpdateSource` 和 `UpdateCoordinator`，UI 回调只调用 `postCheck/postStart/postCancel`，`update_service.tick` 不再出现在 Arduino `loop()`。

```python
def test_https_ota_is_runtime_wired_and_background_owned(self):
    state = read("Firefly/FireflyState.cpp")
    ino = read("Firefly/Firefly.ino")
    self.assertIn("HttpsUpdateSource https_update_source", state)
    self.assertIn("UpdateCoordinator update_coordinator", state)
    self.assertIn("update_coordinator.postCheck()", ino)
    self.assertNotIn("update_service.tick(millis())", ino)
```

- [x] **Step 3: 运行测试确认 RED**

Run:

```powershell
python -m unittest tests.python.test_release_documentation tests.python.test_companion_features_contract -v
```

Expected: FAIL，原因是发布文档、后台协调器和 HTTPS 正式实例不存在。

### Task 2: 实现发布配置与 HTTPS 清单读取

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/UpdateReleaseConfig.h`
- Modify: `libraries/FireflyOS/src/firefly/services/UpdateTrustAnchor.h`
- Modify: `libraries/FireflyOS/src/firefly/services/UpdateSources.h`
- Modify: `libraries/FireflyOS/src/firefly/services/UpdateSources.cpp`
- Modify: `.gitignore`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [x] **Step 1: 写 CoreTests 失败用例**

增加 `test_https_update_configuration_and_manifest_bounds()`，断言开发构建未配置时返回 `NoEndpoint`，文件名拒绝 `/`、`..`、`.part`，清单容量不超过 `UpdateManifestCodec::kMaxJsonBytes`。

- [x] **Step 2: 编译确认 RED**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target FireflyCoreTests
```

Expected: FAIL，原因是 `HttpsManifestSource`/发布配置接口不存在。

- [x] **Step 3: 实现配置门禁**

`UpdateReleaseConfig.h` 包含本地配置头；Development 缺失时定义 `FIREFLY_UPDATE_CONFIGURED=0`，Release 缺失时 `#error`。`UpdateTrustAnchor.h` 保持 Development 测试公钥，但 Release 必须包含本地公钥。

- [x] **Step 4: 实现有界 HTTPS 清单源**

新增：

```cpp
class HttpsManifestSource {
public:
    UpdateIoResult fetch(UpdateManifest & output);
    static bool configured();
};
```

`fetch()` 使用固定 `FIREFLY_UPDATE_MANIFEST_FILE`，要求 200、identity、固定 Content-Length 且不超过 `kMaxJsonBytes`，读取完成后调用 `UpdateManifestCodec::parseJson()`。

- [x] **Step 5: 运行 CoreTests 确认 GREEN**

Run 同 Step 2。Expected: 编译成功。

### Task 3: 实现固定后台 UpdateCoordinator

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/UpdateCoordinator.h`
- Create: `libraries/FireflyOS/src/firefly/services/UpdateCoordinator.cpp`
- Modify: `libraries/FireflyOS/src/FireflyOS.h`
- Modify: `Firefly/FireflyApp.h`
- Modify: `Firefly/FireflyState.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [x] **Step 1: 写协调状态机失败测试**

使用 fake SD catalog、HTTPS catalog、WifiService、UpdateService gateway，验证：SD 优先且不启 Wi-Fi；SD 不可用时请求 OTA Wi-Fi；HTTPS 未配置返回 NoEndpoint；终态释放 Wi-Fi；队列满拒绝新命令。

- [x] **Step 2: 编译确认 RED**

Run FireflyCoreTests build。Expected: FAIL，原因是 `UpdateCoordinator` 不存在。

- [x] **Step 3: 实现无 LVGL 协调器**

接口固定为：

```cpp
enum class UpdateCommand : uint8_t { Check, Start, Cancel };
class UpdateCoordinator {
public:
    bool postCheck();
    bool postStart();
    bool postCancel();
    void runOnce(uint32_t now_ms, const UpdateRuntimeGate & gate);
};
```

内部使用长度 4 的静态 FreeRTOS queue；`runOnce()` 每次最多消费一条命令并推进一个 update tick。协调器不包含 `lv_`、`delay()` 或动态容器。

- [x] **Step 4: 接入后台任务**

在 `FireflyInteraction.cpp` 增加固定 update task，循环调用 `runOnce()` 后 `vTaskDelay(1)`；三个 UI 回调只 post 命令。删除 Arduino `loop()` 中直接 `update_service.tick()` 的路径。

- [x] **Step 5: 编译和 Python 契约确认 GREEN**

Run:

```powershell
python -m unittest tests.python.test_companion_features_contract -v
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target FireflyCoreTests
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target Firefly
```

Expected: 全部通过，且 `rg -n "lv_" UpdateCoordinator.*` 无结果。

### Task 4: 建立 Release 构建入口

**Files:**
- Modify: `tools/build_firmware.ps1`
- Modify: `tests/python/test_partition_layout.py`
- Modify: `tests/python/test_release_documentation.py`

- [x] **Step 1: 写 Release 构建契约并确认 RED**

要求脚本有 `[ValidateSet('Development','Release')]`、Release 只允许 Firefly、检查两个本地头、注入 `-DFIREFLY_RELEASE_BUILD=1`。

Run:

```powershell
python -m unittest tests.python.test_partition_layout tests.python.test_release_documentation -v
```

Expected: FAIL，原因是构建脚本没有 Configuration。

- [x] **Step 2: 实现构建门禁**

为 `build_firmware.ps1` 增加 `-Configuration Development|Release`。Release 在编译前检查本地公钥与配置头；缺失时抛出固定错误；Development 不注入发布宏。

- [x] **Step 3: 验证 GREEN 和 fail-closed**

Run:

```powershell
python -m unittest tests.python.test_partition_layout tests.python.test_release_documentation -v
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target Firefly -Configuration Release
```

Expected: Python PASS；Release 命令因仓库外生产材料缺失而以固定错误失败，且不生成被称为发布版的固件。

### Task 5: 对齐恢复出厂 SD 文案

**Files:**
- Modify: `Firefly/Firefly.ino`
- Modify: `docs/UI预览/05-天气与更新/恢复出厂.html`
- Modify: `docs/模块说明/09-WiFi天气与传输.md`
- Modify: `tests/python/test_pairing_security_contract.py`

- [x] **Step 1: 写文案契约并确认 RED**

要求正式 UI 使用 `Delete managed FireflyOS data`，模块文档列出七个受管目录并说明未知内容保留。

- [x] **Step 2: 修改 UI 与文档**

不改布局、尺寸或清理代码，只修改文案，避免把白名单清理描述成递归删除整个根目录。

- [x] **Step 3: 验证 GREEN**

Run pairing security Python test。Expected: PASS。

### Task 6: 补齐发布文档和真实性检查

**Files:**
- Create: `docs/模块说明/10-OTA发布规范.md`
- Create: `docs/模块说明/11-最终验收报告.md`
- Create: `docs/模块说明/12-发布清单.md`
- Modify: `docs/项目介绍.md`
- Modify: `Firefly/README.md`
- Modify: `docs/FireflyOS系统架构总纲.md`
- Modify: `tools/verify_all.ps1`
- Modify: `tests/python/test_release_documentation.py`

- [x] **Step 1: 写三份真实性文档**

记录分区、签名、双源 OTA、密钥门禁、恢复出厂、自动验证字段和硬件 `PENDING` 矩阵；禁止声称 RC 标签已创建。

- [x] **Step 2: 修订现有说明**

项目介绍、README、架构总纲准确陈述计划 6 软件能力与限制；明确 HTTPS 只有 Release 配置后可用，Development 默认只保证 SD OTA。

- [x] **Step 3: 强化 verify_all**

在文档检查阶段显式运行：

```powershell
python -m unittest tests.python.test_release_documentation -v
```

并在该命令成功后才输出完整通过。

- [x] **Step 4: 运行发布文档测试确认 GREEN**

Run:

```powershell
python -m unittest tests.python.test_release_documentation -v
```

Expected: PASS。

### Task 7: 全量验证和最终范围检查

**Files:**
- Verify all changed files

- [x] **Step 1: 运行 Python 全集**

Run `python -m unittest discover -s tests/python -v`。Expected: 全部 PASS，记录数量。

- [x] **Step 2: 编译三个固件目标**

Run `powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1`。Expected: 三个目标和文档门禁通过，记录 Flash/RAM。

- [x] **Step 3: Android 单测与 APK**

设置 `JAVA_HOME`、`ANDROID_HOME` 后运行 `gradlew.bat :app:testDebugUnitTest :app:assembleDebug`，汇总 XML、APK 大小和 SHA-256。

- [x] **Step 4: 范围检查**

Run:

```powershell
git diff --check
git diff --name-only -- "image/图片生成提示词"
git status --short
```

Expected: diff check 无错误，提示词目录无差异，没有删除、commit、merge、push 或 tag。

- [x] **Step 5: 停止并报告**

汇总代码实现、自动验证和所有 `PENDING` 真机项目；不执行任何 Git 状态变更操作。
