# FireflyOS Plan 6 Task 6–10 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完成 FireflyOS 32MB 双槽、签名 OTA、回滚确认、更新 UI、固定容量诊断、恢复出厂和发布文档的软件闭环，同时把所有未执行真机项目保持为 PENDING。

**Architecture:** `Firefly/partitions.csv` 是正式固件唯一分区真源；UpdateManifest、UpdateService、ESP32 平台适配和 BootValidation 分层，UI 只读快照。DiagnosticService 提供固定 64 条证据环，FactoryResetService 只协调各数据所有者的幂等清理，发布文档只消费自动验证事实。

**Tech Stack:** Arduino ESP32 2.0.17、ESP-IDF OTA API、mbedTLS ECDSA P-256/SHA-256、WiFiClientSecure、LittleFS/SD_MMC、LVGL 8.3.11、Python unittest/cryptography、PowerShell、Gradle/JUnit。

---

## 0. 执行约束

- 工作目录固定为 `D:\Study\Projects\ESP32Projects\FireflyOS\.worktrees\wifi-weather-ota`。
- 保留 Task 1–5 的全部未提交改动；不得 reset、checkout 或清理未知文件。
- 每个生产行为先写失败测试并确认失败原因，再写最小实现。
- 本计划的“差异检查点”只运行 `git diff --check` 和查看状态；未经用户另行授权，不提交、合并、推送或创建标签。
- 不删除或覆盖 `image/图片生成提示词`。
- 自动测试、编译和模拟结果不得写成真机 PASS。

### Task 1: 建立可解析的双 OTA 分区真源

**Files:**
- Create: `Firefly/partitions.csv`
- Create: `tools/validate_partition_layout.py`
- Create: `tests/python/test_partition_layout.py`
- Modify: `tests/python/test_repository_contracts.py`

- [ ] **Step 1: 写 CSV 与二进制分区解析失败测试**

在 `tests/python/test_partition_layout.py` 导入 `parse_csv`、`parse_binary`、`validate_firefly_layout` 和 `compare_layouts`。测试以下行为：

```python
def test_firefly_layout_has_equal_11mb_ota_slots(self):
    rows = parse_csv(ROOT / "Firefly" / "partitions.csv")
    validate_firefly_layout(rows)
    by_name = {row.name: row for row in rows}
    self.assertEqual(by_name["otadata"].size, 0x2000)
    self.assertEqual(by_name["app0"].subtype, "ota_0")
    self.assertEqual(by_name["app1"].subtype, "ota_1")
    self.assertEqual(by_name["app0"].size, 0xB00000)
    self.assertEqual(by_name["app1"].size, 0xB00000)
    self.assertLessEqual(max(r.offset + r.size for r in rows), 0x2000000)
```

另写重叠、越界、槽位不等、错误 otadata 大小，以及由 `struct.pack("<HBBII16sI", 0x50AA, 0, 0x10, 0x10000, 0xB00000, b"app0\0".ljust(16, b"\0"), 0)` 生成的二进制表与 CSV 不一致测试。

- [ ] **Step 2: 运行测试并确认 RED**

Run:

```powershell
python -m unittest tests.python.test_partition_layout -v
```

Expected: FAIL，因为验证模块和正式 CSV 尚不存在。

- [ ] **Step 3: 实现分区模型与正式 CSV**

`tools/validate_partition_layout.py` 定义：

```python
@dataclass(frozen=True)
class Partition:
    name: str
    type: str
    subtype: str
    offset: int
    size: int
    flags: int = 0

def parse_csv(path: Path) -> list[Partition]:
    rows = []
    for raw in csv.reader(path.read_text(encoding="utf-8").splitlines()):
        if not raw or raw[0].strip().startswith("#"):
            continue
        cells = [cell.strip() for cell in raw]
        rows.append(Partition(cells[0], cells[1], cells[2],
                              int(cells[3], 0), int(cells[4], 0),
                              int(cells[5], 0) if len(cells) > 5 and cells[5] else 0))
    return rows

def parse_binary(path: Path) -> list[Partition]:
    type_names = {0: "app", 1: "data"}
    subtype_names = {(1, 2): "ota", (1, 1): "nvs", (1, 0x82): "spiffs",
                     (0, 0x10): "ota_0", (0, 0x11): "ota_1"}
    rows = []
    data = path.read_bytes()
    for offset in range(0, len(data), 32):
        chunk = data[offset:offset + 32]
        if len(chunk) < 32 or chunk == b"\xff" * 32:
            break
        magic, kind, subtype, address, size, label, flags = struct.unpack(
            "<HBBII16sI", chunk)
        if magic != 0x50AA:
            break
        rows.append(Partition(label.split(b"\0", 1)[0].decode("ascii"),
                              type_names[kind],
                              subtype_names[(kind, subtype)],
                              address, size, flags))
    return rows

def validate_firefly_layout(rows: Sequence[Partition]) -> None:
    by_name = {row.name: row for row in rows}
    if len(by_name) != len(rows):
        raise ValueError("duplicate partition name")
    ordered = sorted(rows, key=lambda row: row.offset)
    for left, right in zip(ordered, ordered[1:]):
        if left.offset + left.size > right.offset:
            raise ValueError("partition overlap")
    if not ordered or ordered[-1].offset + ordered[-1].size > 0x2000000:
        raise ValueError("partition table exceeds 32MB")
    if by_name["otadata"].size != 0x2000:
        raise ValueError("otadata must be 0x2000")
    if (by_name["app0"].subtype, by_name["app1"].subtype) != ("ota_0", "ota_1"):
        raise ValueError("missing OTA slots")
    if by_name["app0"].size != 0xB00000 or by_name["app1"].size != 0xB00000:
        raise ValueError("OTA slots must both be 11MB")

def compare_layouts(expected: Sequence[Partition], actual: Sequence[Partition]) -> None:
    fields = lambda row: (row.name, row.type, row.subtype,
                          row.offset, row.size, row.flags)
    if [fields(row) for row in expected] != [fields(row) for row in actual]:
        raise ValueError("compiled partition table differs from source CSV")
```

解析规则：忽略空行和 `#` 注释；数字接受十进制或 `0x`；名称唯一；按 offset 排序后检查相邻范围；二进制仅接受 magic `0x50AA` 的 32 字节条目并在全 `0xFF` 终止。正式 CSV 精确写入设计规范中的五个分区。

- [ ] **Step 4: 更新旧仓库契约并验证 GREEN**

把 `test_repository_contracts.py` 对旧 `app5M_fat24M_32MB.csv` 的正式 Firefly 断言替换为 `Firefly/partitions.csv`，但保留旧表供 CoreTests/AudioProbe 使用。

Run:

```powershell
python -m unittest tests.python.test_partition_layout tests.python.test_repository_contracts -v
```

Expected: PASS。

- [ ] **Step 5: 差异检查点**

Run:

```powershell
git diff --check -- Firefly/partitions.csv tools/validate_partition_layout.py tests/python/test_partition_layout.py tests/python/test_repository_contracts.py
git status --short
```

Expected: 无 whitespace error；不执行 commit。

### Task 2: 让正式构建使用并核验双槽布局

**Files:**
- Modify: `tools/build_firmware.ps1`
- Modify: `tests/python/test_partition_layout.py`
- Modify: `tools/verify_all.ps1`

- [ ] **Step 1: 写构建链路失败契约**

新增测试读取 `build_firmware.ps1`，要求出现：

```python
self.assertIn("Firefly\\partitions.csv", script)
self.assertIn("validate_partition_layout.py", script)
self.assertIn("Firefly.ino.partitions.bin", script)
self.assertIn("9227468", script)
self.assertNotIn("Copy-Item -LiteralPath $partitionSource -Destination $partitionTarget -Force\n\nfunction Resolve-ArduinoCli", script)
```

最后一项迫使脚本在复制后执行源布局预检，而不是直接进入编译。

- [ ] **Step 2: 运行契约并确认 RED**

Run: `python -m unittest tests.python.test_partition_layout -v`  
Expected: FAIL，指出构建脚本仍只使用旧分区表且无二进制核验。

- [ ] **Step 3: 修改构建脚本**

`$Target -eq 'Firefly'` 时 `$partitionSource` 指向 `Firefly\partitions.csv`，其他目标继续使用 `tools\partitions\app5M_fat24M_32MB.csv`。复制后先运行：

```powershell
& $python (Join-Path $root 'tools\validate_partition_layout.py') `
    --csv $partitionSource
if($LASTEXITCODE -ne 0) { throw 'Partition source validation failed' }
```

Arduino 编译后运行下列命令核对实际二进制表，再检查 `.bin` 长度：

```powershell
& $python (Join-Path $root 'tools\validate_partition_layout.py') `
    --csv $partitionSource `
    --binary (Join-Path $buildPath 'Firefly.ino.partitions.bin')
if($LASTEXITCODE -ne 0) { throw 'Compiled partition validation failed' }
```

```powershell
$firmwareLimit = 9227468
$firmware = Get-Item -LiteralPath (Join-Path $buildPath 'Firefly.ino.bin')
if($firmware.Length -gt $firmwareLimit) {
    throw "Firmware exceeds 80% OTA slot budget: $($firmware.Length) > $firmwareLimit"
}
```

- [ ] **Step 4: 运行布局测试和正式构建**

Run:

```powershell
python -m unittest tests.python.test_partition_layout -v
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target Firefly
```

Expected: 测试 PASS；构建输出确认两个 `0xB00000` 槽；固件小于 9,227,468 字节。

- [ ] **Step 5: 把布局测试加入全量入口并做差异检查**

确认 `verify_all.ps1` 的 Python discover 能发现新测试，并运行 `git diff --check`；不执行 commit。

### Task 3: 定义清单、确定性签名输入和离线签名工具

**Files:**
- Create: `tools/sign_update.py`
- Create: `tests/python/test_update_manifest.py`
- Create: `tests/fixtures/update_test_private_key.pem`
- Create: `tests/fixtures/update_test_public_key.pem`
- Create: `libraries/FireflyOS/src/firefly/services/UpdateManifest.h`
- Create: `libraries/FireflyOS/src/firefly/services/UpdateManifest.cpp`
- Create: `libraries/FireflyOS/src/firefly/services/UpdateTrustAnchor.h`
- Modify: `libraries/FireflyOS/src/FireflyOS.h`

- [ ] **Step 1: 写 Python 清单失败测试**

测试使用固定测试 P-256 私钥对临时固件签名，断言：

```python
manifest = sign_firmware(
    firmware=firmware,
    product="FireflyOS",
    version="0.1.1",
    build=101,
    min_build=100,
    private_key_path=FIXTURE_KEY,
)
self.assertEqual(manifest["size"], len(payload))
self.assertEqual(manifest["sha256"], hashlib.sha256(payload).hexdigest())
self.assertEqual(len(bytes.fromhex(manifest["signature"])), 64)
verify_manifest(manifest, FIXTURE_PUBLIC_KEY)
```

另测错误曲线、空 product/version、长度超过 15、build 不递增、固件超过 `0xB00000`、缺少 `FIREFLY_SIGNING_KEY` 和人工 size/hash 参数被拒绝。

- [ ] **Step 2: 运行 Python 测试并确认 RED**

Run: `python -m unittest tests.python.test_update_manifest -v`  
Expected: FAIL，因为签名工具尚不存在。

- [ ] **Step 3: 实现签名工具和固定编码**

`canonical_manifest_bytes()` 输出：

```python
return b"FFOTA1\0\0" + struct.pack(
    "<H16s16sIII32s",
    schema,
    fixed_utf8(product, 16),
    fixed_utf8(version, 16),
    build,
    min_build,
    size,
    sha256,
)
```

使用 `cryptography.hazmat.primitives.asymmetric.ec.SECP256R1()` 和 SHA-256；DER 签名通过 `decode_dss_signature` 转为 32 字节大端 `r` 加 32 字节大端 `s`。CLI 只接受 firmware/product/version/build/min-build/output，私钥路径只读环境变量。

- [ ] **Step 4: 写 C++ 解析与黄金向量失败测试**

在 `FireflyCoreTests.ino` 构造由 Python 测试导出的固定 canonical bytes、digest、开发公钥和 raw signature，断言 `UpdateManifestCodec::canonicalize()` 字节完全一致、`verifySignature()` 成功，修改任一 build/hash 字节后失败；缺字段、重复字段、超长字段、非法十六进制和 1025 字节 JSON 被拒绝。

- [ ] **Step 5: 实现 C++ 清单模型和验证**

公开接口固定为：

```cpp
struct UpdateManifest {
    uint16_t schema = 0;
    char product[16]{};
    char version[16]{};
    uint32_t build = 0;
    uint32_t min_build = 0;
    uint32_t size = 0;
    uint8_t sha256[32]{};
    uint8_t ecdsa_p256_signature[64]{};
};

class UpdateManifestCodec {
public:
    static constexpr size_t kMaxJsonBytes = 1024;
    static constexpr size_t kCanonicalBytes = 86;
    static bool parseJson(const char * json, size_t length, UpdateManifest & out);
    static bool canonicalize(const UpdateManifest & manifest,
                             uint8_t output[kCanonicalBytes]);
    static bool verifySignature(const UpdateManifest & manifest,
                                const uint8_t public_key[65]);
};
```

JSON 解析使用有界扫描器，拒绝重复/未知关键字段；mbedTLS 只启用 `MBEDTLS_ECP_DP_SECP256R1`，公钥必须是 65 字节未压缩点。

- [ ] **Step 6: 实现开发/发布信任锚门禁并验证 GREEN**

`UpdateTrustAnchor.h` 优先包含本地 `FireflyUpdatePublicKey.local.h`；否则提供明确命名的开发公钥。若定义 `FIREFLY_RELEASE_BUILD` 且本地头不存在，用 `#error` 阻止发布构建。运行 Python 清单测试和 FireflyCoreTests 编译，Expected: PASS。

同时在 `FireflyOS.h` 保持 `FIREFLYOS_VERSION_MAJOR/MINOR/PATCH` 为 `0/1/0`，新增 `FIREFLYOS_BUILD 100`；CoreTests 断言这四个值，避免清单比较使用散落常量。

- [ ] **Step 7: 差异检查点**

运行 `git diff --check`，并用 `rg -n "PRIVATE KEY|FIREFLY_SIGNING_KEY"` 确认生产目录没有私钥内容；测试夹具必须包含“TEST ONLY”说明。不执行 commit。

### Task 4: 用纯状态机实现 OTA 门禁和首终态

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/UpdateService.h`
- Create: `libraries/FireflyOS/src/firefly/services/UpdateService.cpp`
- Modify: `libraries/FireflyOS/src/firefly/core/ResourceGovernor.*`
- Modify: `libraries/FireflyOS/src/firefly/services/PowerService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/AudioService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/BulkTransferService.*`
- Modify: `Firefly/FireflyApp.h`
- Modify: `Firefly/FireflyState.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 写门禁与状态失败测试**

用 FakeUpdateSource/FakeUpdateWriter 覆盖：40% 阈值、充电豁免、闹钟、Music、Recorder、Bulk、重复 OTA、product/build/min_build/size、取消时机、首终态和清理幂等。核心断言：

```cpp
expect_true(!updates.offer(manifest, source, blocked, 1000),
            "active recording blocks OTA");
expect_true(updates.snapshot().failure == UpdateFailure::AudioBusy,
            "OTA exposes exact audio gate");
```

- [ ] **Step 2: 编译并确认 RED**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target FireflyCoreTests`  
Expected: FAIL，因为 UpdateService 类型尚不存在。

- [ ] **Step 3: 定义固定容量接口**

```cpp
enum class UpdateState : uint8_t {
    Idle, Available, Blocked, Downloading, Verifying, Writing,
    RebootPending, BootChecking, Completed, Failed,
    RollbackRequested, RolledBack
};

enum class UpdateFailure : uint8_t {
    None, LowPower, AlarmActive, AudioBusy, TransferBusy, OtaBusy,
    WrongProduct, BuildNotNewer, MinBuildMismatch, PackageTooLarge,
    NoHttpsEndpoint, SourceUnavailable, ManifestInvalid, SignatureInvalid,
    ShortPackage, OversizedPackage, HashMismatch, WriteFailed,
    FinalizeFailed, Cancelled, BootValidationFailed, Timeout
};

struct UpdateSnapshot {
    UpdateState state;
    UpdateFailure failure;
    uint32_t build;
    uint32_t size;
    uint32_t processed;
    uint8_t progress_percent;
    char version[16];
    bool cancel_allowed;
};
```

`static_assert(sizeof(UpdateSnapshot) <= 64)`；所有跨核读取受静态互斥量保护。

- [ ] **Step 4: 实现 preflight、offer、cancel 和 first-terminal-wins**

UpdateService 通过只读 `UpdateRuntimeGate` 快照检查 Power/Alarm/Audio/Bulk/ResourceGovernor。成功获取 `ResourceKind::Ota` 后进入 Available；取消仅在 Available/Downloading/Verifying 生效；Writing 后返回 false。`fail()` 只在非终态写入 failure，所有资源在 `cleanup()` 中幂等释放。

在 `FireflyState.cpp` 建立唯一 `ResourceGovernor`、`SystemLifecycle` 和 UpdateService 实例，在 `FireflyApp.h` 声明 extern；主循环只组装一次一致的 gate 快照后传给 UpdateService，不能让服务跨核读取 LVGL 或松散全局变量。

- [ ] **Step 5: 验证 GREEN 与旧服务反向门禁**

AudioService/BulkTransferService 开始入口查询 Ota resource；OTA 持有期间返回现有 `OtaBusy`。运行 CoreTests 编译和 Python 契约，Expected: PASS。

### Task 5: 实现 SD/HTTPS 固定分块源与 ESP32 非活动槽写入

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/UpdateSources.h`
- Create: `libraries/FireflyOS/src/firefly/services/UpdateSources.cpp`
- Create: `libraries/FireflyOS/src/firefly/hal/EspOtaPlatform.h`
- Create: `libraries/FireflyOS/src/firefly/hal/EspOtaPlatform.cpp`
- Modify: `libraries/FireflyOS/src/firefly/services/UpdateService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/StorageService.*`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `tests/python/test_companion_features_contract.py`

- [ ] **Step 1: 写流式边界失败测试**

Fake source 分别返回 1KB、恰好 manifest.size、短包、超长包、零字节卡顿和中途错误；Fake writer 记录 begin/write/end/abort/select 次数。断言每次 read/write 不超过 4096 字节，哈希失败永不 select，写失败只 abort 一次，当前槽从不作为目标。

- [ ] **Step 2: 编译并确认 RED**

Run CoreTests build。Expected: FAIL，因为源和 writer 接口尚不存在。

- [ ] **Step 3: 实现受管 SD 数据源**

`SdUpdateSource::open()` 只接受 `/FireflyOS/Updates/<name>`，通过 StorageService 的 OTA 专用只读租约打开；`read()` 最大 4096；`close()` 幂等并释放 normal handle。SD 拔出映射 `SourceUnavailable`，不删除正式包。

- [ ] **Step 4: 实现编译期 HTTPS 数据源**

只有同时定义 `FIREFLY_UPDATE_BASE_URL`、`FIREFLY_UPDATE_HOST` 和 CA 常量时才连接；否则 begin 返回 `NoHttpsEndpoint`。使用解析 IP + 原 host SNI/CA，拒绝 HTTP、重定向、chunked 固件、缺失或不匹配 Content-Length；连接/读取使用单调绝对截止，URL 只能由固定 base URL 加受限文件名构造。

- [ ] **Step 5: 实现 EspOtaPlatform**

接口：

```cpp
class UpdateWriter {
public:
    virtual bool begin(uint32_t size) = 0;
    virtual bool write(const uint8_t * data, size_t size) = 0;
    virtual bool finish() = 0;
    virtual bool selectForNextBoot() = 0;
    virtual void abort() = 0;
};
```

ESP32 实现用 `esp_ota_get_next_update_partition(nullptr)`、`esp_ota_begin`、`esp_ota_write`、`esp_ota_end`、`esp_ota_set_boot_partition`；开始时拒绝目标等于当前 running partition。

- [ ] **Step 6: 实现 UpdateService tick 流程并验证 GREEN**

`tick()` 每轮最多处理一个 4096 字节块；更新 SHA-256 和 processed；达到 size 后额外探测一个字节拒绝超长包；digest 常量时间比较成功后才 finish/select。状态语义固定为：Downloading 表示可取消地把来源流写入非活动槽，Verifying 表示长度/哈希终检，Writing 只包含不可取消的 `esp_ota_end` 与 boot partition 选择。运行 CoreTests 和 companion contract，Expected: PASS。

### Task 6: 实现首次启动验证与回滚平台

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/BootValidationService.h`
- Create: `libraries/FireflyOS/src/firefly/services/BootValidationService.cpp`
- Modify: `libraries/FireflyOS/src/firefly/hal/EspOtaPlatform.*`
- Modify: `libraries/FireflyOS/src/firefly/core/HardwareCapabilities.*`
- Modify: `Firefly/FireflyApp.h`
- Modify: `Firefly/FireflyState.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 写 pending-verify 自检失败测试**

Fake platform 覆盖普通启动不调用 OTA API、六项全过时 mark valid、任一项失败时 mark invalid、30 秒超时回滚、回滚 API 失败进入稳定 Error 且不重复调用。

- [ ] **Step 2: 编译并确认 RED**

Run CoreTests build。Expected: FAIL，因为 BootValidationService 不存在。

- [ ] **Step 3: 实现固定六项状态机**

定义 `BootCheck::{Rtc,Pmu,Display,Touch,Nvs,MainUi}`、`BootValidationState`、六项结果数组和 `kDeadlineMs=30000`。每次 `tick()` 最多推进一项；触摸检查只调用驱动 ready 和一次非阻塞 sample，不等待用户输入。

- [ ] **Step 4: 接入 setup/loop**

setup 在各设备初始化后提交对应健康结果；UiShell 完成创建且 loop 首次心跳后提交 MainUi。仅 pending-verify 镜像显示 BootChecking 覆盖状态。成功 mark valid；失败记录诊断后 mark invalid and reboot。

- [ ] **Step 5: 验证 GREEN**

运行 CoreTests、Firefly 正式构建和 `rg -n "lv_" BootValidationService.*`；Expected: 测试/构建 PASS，服务文件无 LVGL 调用。

### Task 7: 实现已批准的状态聚焦更新 UI

**Files:**
- Create: `docs/UI预览/05-天气与更新/系统更新.html`
- Create: `libraries/FireflyOS/src/firefly/apps/update/UpdateApp.h`
- Create: `libraries/FireflyOS/src/firefly/apps/update/UpdateApp.cpp`
- Modify: `libraries/FireflyOS/src/FireflyOS.h`
- Modify: `libraries/FireflyOS/src/firefly/ui/NavigationController.h`
- Modify: `Firefly/FireflyApp.h`
- Modify: `Firefly/FireflyState.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `tests/python/test_companion_features_contract.py`

- [ ] **Step 1: 写 UI 契约失败测试**

要求 UpdateApp 只接受 UpdateSnapshot、包含全部 11 个状态映射、48px 最小按钮、410×502 安全区、hide 后停止动画；禁止 `WiFiClientSecure`、`StorageService`、`esp_ota_*` 和网络调用出现在 app 文件。

- [ ] **Step 2: 运行契约并确认 RED**

Run: `python -m unittest tests.python.test_companion_features_contract -v`  
Expected: FAIL，因为正式更新页面和 UpdateApp 尚不存在。

- [ ] **Step 3: 固化 HTML 审批稿**

把已选 A 布局转为项目正式 HTML，覆盖 Available、Blocked、Downloading、Verifying、Writing、RebootPending、BootChecking、Completed、Failed、RolledBack。Writing 页面无取消按钮并显示 SAM 守护文案。

- [ ] **Step 4: 实现 UpdateApp**

公开 `create/show/hide/refresh` 和 start/cancel/diagnostics callbacks。一个根对象、标题、说明、图标、进度条、百分比、主按钮和次按钮固定复用，不随状态重复创建。所有按钮宽高至少 48；所有坐标落入安全区。

- [ ] **Step 5: 接入路由和主循环**

新增 Route::Update；诊断页或设置页提供系统更新入口。UI 回调只调用 UpdateService command；`loop()` 读取 snapshot 后 refresh。隐藏时 `lv_anim_del` 并停止刷新动画。

- [ ] **Step 6: 验证 GREEN**

运行 UI 契约、CoreTests 编译和 Firefly 编译。Expected: PASS；正式 UI 仍只能由主循环访问。

### Task 8: 实现固定 64 条 DiagnosticService

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/DiagnosticService.h`
- Create: `libraries/FireflyOS/src/firefly/services/DiagnosticService.cpp`
- Modify: `libraries/FireflyOS/src/firefly/core/EventBus.*`
- Modify: `libraries/FireflyOS/src/FireflyOS.h`
- Modify: `Firefly/FireflyApp.h`
- Modify: `Firefly/FireflyState.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 写环形缓存和聚合失败测试**

插入 65 条记录后断言 count=64、最旧一条被覆盖、顺序稳定；测试一分钟采样去重、会话边界即时记录、EventBus peak/drop 聚合、显式 Serial 导出和 SD 不可用导出失败不清空 RAM。

- [ ] **Step 2: 编译并确认 RED**

Run CoreTests build。Expected: FAIL，因为 DiagnosticService 不存在。

- [ ] **Step 3: 实现固定记录模型**

```cpp
struct DiagnosticRecord {
    uint32_t timestamp_ms;
    DiagnosticReason reason;
    uint32_t internal_free;
    uint32_t internal_minimum;
    uint32_t internal_largest;
    uint32_t psram_free;
    uint16_t ui_stack_words;
    uint16_t background_stack_words;
    uint16_t event_drops;
    uint8_t event_size;
    uint8_t event_peak;
    PowerMode power_mode;
    uint8_t restart_reason;
};
```

类内固定 `DiagnosticRecord records_[64]`，静态互斥保护读写；不使用 String/vector/new。

- [ ] **Step 4: 扩展 EventBus 只读指标并接入采样**

EventBus 增加饱和投递计数和 peak size，只在现有临界区更新。主循环每分钟采样一次，并在 Wi-Fi/Bulk/OTA/Audio/Recorder/SD/BootValidation 开始与结束调用 `record(reason, snapshot)`。

- [ ] **Step 5: 实现显式导出并验证 GREEN**

Serial 输出逐条固定字段；SD 导出仅写 `/FireflyOS/Logs/diagnostics.csv.part`，完成后受管重命名。OTA/Bulk SD 独占时返回 Busy。运行 CoreTests 和 Firefly build，Expected: PASS。

### Task 9: 实现恢复出厂协调与安全边界

**Files:**
- Create: `libraries/FireflyOS/src/firefly/services/FactoryResetService.h`
- Create: `libraries/FireflyOS/src/firefly/services/FactoryResetService.cpp`
- Modify: `libraries/FireflyOS/src/firefly/services/StorageService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/ConnectivityService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/WifiService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/WeatherService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/NotificationService.*`
- Modify: `Firefly/FireflyApp.h`
- Modify: `Firefly/FireflyState.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `tests/python/test_pairing_security_contract.py`

- [ ] **Step 1: 写默认不删 SD 的失败测试**

Fake owners 记录调用。断言内部 reset 调用 pairing/Wi-Fi/notification/weather/settings/cache，默认 `eraseSd=false` 时 SD 删除次数为零；只有独立确认 token 匹配且 `eraseSd=true` 才请求受管根清理；任一关键清理失败时不 reboot、不显示 Completed。

- [ ] **Step 2: 编译并确认 RED**

Run CoreTests build。Expected: FAIL，因为 FactoryResetService 不存在。

- [ ] **Step 3: 为数据所有者增加幂等清理接口**

StorageService 提供 `clearInternalUserData()` 与 `clearManagedSdRoot()`；前者逐命名空间清除后重建 schema，后者要求 SD 独占租约、只遍历七个固定子目录并拒绝调用方路径。Connectivity 清 token 和 bonds，Wifi 清 credential/nonce，Weather 删除缓存，Notification 清摘要。

- [ ] **Step 4: 实现协调状态机和双确认**

状态为 Preview/InternalConfirmed/SdConfirmed/Running/Completed/Failed；SD 确认使用一次性固定 32-bit generation，只对当前 reset request 有效。成功后通过可注入 RebootCallback 请求重启。

- [ ] **Step 5: 接入恢复出厂 UI**

第一屏明确“保留 SD 媒体”；独立按钮进入第二确认屏“删除 /FireflyOS 数据”。按钮均至少 48px。运行路径不接受任意目录文字。

- [ ] **Step 6: 隐私与权限契约 GREEN**

Python 测试扫描日志格式串，禁止 password/token/HMAC/notification body/recording content；Android Manifest 继续禁止后台位置、通讯录、麦克风和广泛存储权限。运行 CoreTests、pairing security 和 Android 单测，Expected: PASS。

### Task 10: 建立硬件降级映射和中性基础主题

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/core/HardwareCapabilities.*`
- Modify: `libraries/FireflyOS/src/firefly/services/ThemePackageService.*`
- Modify: `libraries/FireflyOS/src/firefly/services/StorageService.*`
- Modify: `Firefly/FireflyTheme.cpp`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `tests/python/test_theme_manifest.py`
- Modify: `tests/python/test_repository_contracts.py`

- [ ] **Step 1: 写降级和主题迁移失败测试**

对 RTC/PMU/IMU/SD/Codec/BLE/Wi-Fi 各置 unavailable，断言仅相关 capability false 且系统仍能进入 Lock/Home；加载旧 `firefly-default` 时迁移为 `system-default`，现有 palette 保持；核心/protocol/HAL/services 文件不得出现角色素材路径。

- [ ] **Step 2: 运行测试并确认 RED**

Run Python 主题/仓库契约和 CoreTests build。Expected: FAIL，因为中性标识和完整降级矩阵尚未实现。

- [ ] **Step 3: 实现能力错误映射**

为每个设备保存 `Available/Unavailable/Degraded` 和固定失败码；UI 诊断页读取能力快照。初始化失败只关闭依赖该设备的入口，不把 lifecycle 全局置 Error，除非显示或主 UI 无法建立。

- [ ] **Step 4: 实现 system-default 与兼容别名**

内置 manifest id/name 改为 `system-default`/`Default`；StorageService 读取旧 `firefly-default` 时一次性保存新 id。流萤壁纸和睡眠图片仍由 Firefly 外壳引用，不进入 service/protocol/HAL。

- [ ] **Step 5: 验证 GREEN**

运行 CoreTests、theme manifest、repository contracts 和 Firefly build。Expected: PASS。

### Task 11: 完成 OTA、验收和发布文档

**Files:**
- Create: `docs/模块说明/10-OTA发布规范.md`
- Create: `docs/模块说明/11-最终验收报告.md`
- Create: `docs/模块说明/12-发布清单.md`
- Modify: `docs/项目介绍.md`
- Modify: `Firefly/README.md`
- Modify: `docs/FireflyOS系统架构总纲.md`
- Create: `tests/python/test_release_documentation.py`

- [ ] **Step 1: 写文档真实性失败测试**

测试要求三份文档存在，包含双槽偏移、签名命令、生产密钥门禁、恢复出厂默认保留 SD、APK/固件哈希字段和每项真机状态；禁止把 24h、400mAh、断电和真机 OTA 写成 PASS；禁止出现已创建 `v1.0.0-rc1` 的陈述。

- [ ] **Step 2: 运行测试并确认 RED**

Run: `python -m unittest tests.python.test_release_documentation -v`  
Expected: FAIL，因为发布文档尚不存在。

- [ ] **Step 3: 写 OTA 发布规范**

记录版本/build 规则、分区、签名输入、`FIREFLY_SIGNING_KEY`、开发与发布公钥区别、SD/HTTPS 更新步骤、失败码、首次启动确认、回滚、密钥轮换和禁止事项。

- [ ] **Step 4: 写最终验收报告**

建立资源、24h、续航、UI、BLE、Wi-Fi、SD、Audio、OTA、安全、隐私和故障降级表。自动项填入实际命令与结果；硬件项初始化为 `PENDING`，证据字段为空但说明执行方法。

- [ ] **Step 5: 写发布清单并修订现有介绍**

README 包含构建、烧录、分区、配对、SD 目录、OTA、恢复与限制；项目介绍/架构总纲只写已实现软件和明确 PENDING；发布清单要求所有真机 Gate 完成后才允许 RC 标签。

- [ ] **Step 6: 验证 GREEN**

运行 release documentation test 和 `verify_all.ps1` 文档检查。Expected: PASS。

### Task 12: 全量自动验证与独立代码审查

**Files:**
- Verify all files changed by Tasks 1–11
- Do not modify hardware PASS/PENDING records unless evidence exists

- [ ] **Step 1: 运行 Python 全集**

Run:

```powershell
python -m unittest discover -s tests\python -v
```

Expected: 全部 PASS；记录确切数量。

- [ ] **Step 2: 编译三个固件目标**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
```

Expected: FireflyCoreTests、AudioProbe、Firefly 和文档检查全部通过；记录 Flash/RAM；Firefly 小于 9,227,468 字节。

- [ ] **Step 3: 运行 Android 单测与 APK 构建**

Run:

```powershell
$env:JAVA_HOME='C:\APP\Android\Android Studio\jbr'
$env:ANDROID_HOME='C:\APP\Android\Sdk'
$env:ANDROID_SDK_ROOT='C:\APP\Android\Sdk'
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug --offline
```

Expected: BUILD SUCCESSFUL；汇总 XML 中 tests/failures/errors/skipped；记录 Debug APK size 与 SHA-256。

- [ ] **Step 4: 做安全和范围检查**

Run:

```powershell
git diff --check
git diff --name-only -- "image/图片生成提示词"
git status --short
```

Expected: diff check 无错误；提示词目录无差异；没有计划外删除、提交、合并、推送或标签。

- [ ] **Step 5: 请求独立代码审查**

审查重点：分区真源是否真正进入构建、签名编码一致性、当前槽不写入、首次启动回滚、固定容量、LVGL 主循环边界、恢复出厂默认不删 SD、日志隐私、真机 PENDING 真实性。发现问题先新增失败回归再修复。

- [ ] **Step 6: 最终差异检查点**

汇总软件实现、自动验证、APK/固件哈希与仍 PENDING 的硬件矩阵。不执行 commit、merge、push 或 tag，等待用户下一步指令。
