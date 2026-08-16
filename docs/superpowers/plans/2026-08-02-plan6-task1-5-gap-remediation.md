# Plan 6 Task 1—5 Gap Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete every code-level gap found in Plan 6 Tasks 1—5 while preserving the existing service architecture, authenticated BLE boundary, fixed-capacity design, and UI-thread-only LVGL access.

**Architecture:** Apply the approved A+ design: add backward-compatible protocol v2 messages, enforce absolute and idle deadlines independently, make command results explicit, and route every termination path through idempotent cleanup. Keep the existing services and Android activity, adding only focused helpers and fixed-size state.

**Tech Stack:** ESP32-S3 Arduino/ESP-IDF, FreeRTOS static recursive mutexes, LVGL 8.3.11 A8 images, Kotlin/JUnit, Python unittest/Pillow asset conversion, Arduino CLI.

**Design:** `docs/superpowers/specs/2026-08-02-plan6-task1-5-gap-remediation-design.md`

**Repository rule:** Commit steps are intentionally omitted because the user explicitly prohibited unrequested commits, merges, and pushes.

---

### Task 1: Critical-power gate and absolute Wi-Fi deadlines

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/PowerService.cpp`
- Modify: `libraries/FireflyOS/src/firefly/services/WifiService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/WifiService.cpp`
- Test: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: Add failing power and SoftAP deadline tests**

Add cases that assert a valid 5% battery rejects Wi-Fi even while charging, and that an active SoftAP Transfer session becomes Off after `kLongSessionLimitMs` despite recent activity.

```cpp
power.updateBattery({.valid = true, .charging = true, .percent = 5});
expect_true(!power.allowsWifiSession(false),
            "critical charging battery rejects new Wi-Fi");

expect_true(wifi.beginSoftApSession(firefly::WifiPurpose::Transfer, 1000),
            "transfer starts SoftAP");
wifi.tick(1000 + firefly::WifiService::kLongSessionLimitMs + 1);
expect_true(wifi.mode() == firefly::WifiMode::Off,
            "SoftAP obeys absolute session limit");
```

- [ ] **Step 2: Compile FireflyCoreTests and confirm RED**

Run the repository FireflyCoreTests compile command from `tools/verify_all.ps1`. Expected: the new behavior assertion is not yet satisfied on hardware tests; add a Python contract assertion for ordering/SoftAP deadline so the off-device run fails before implementation.

- [ ] **Step 3: Implement the minimal gates**

Order the power checks as follows and move the high-power absolute deadline ahead of the Connected-only branch:

```cpp
if(battery_.valid && battery_.percent <= kCriticalBatteryPercent) return false;
if(battery_.charging || battery_.vbus_present) return true;

if(hasHighPowerSession() &&
   now_ms - session_started_ms_ >= kLongSessionLimitMs) {
    stop();
    return;
}
```

- [ ] **Step 4: Run focused Python contracts and compile FireflyCoreTests GREEN**

Run `python -m unittest tests.python.test_companion_features_contract -v` and the FireflyCoreTests compile. Expected: zero failures and compile exit 0.

### Task 2: Provisioning v2 and sensitive-state cleanup

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/WifiService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/WifiService.cpp`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/wifi/WifiProvisioning.kt`
- Test: `AndroidCompanion/app/src/test/java/com/fireflyos/companion/wifi/WifiProvisioningTest.kt`
- Test: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: Add failing codec and service tests**

Android expected v2 prefix:

```kotlin
val payload = WifiProvisioningCodec.encode("Lab", "secret", nonce, 60)
assertEquals(2, payload[0].toInt())
assertEquals(60, payload[1].toInt())
assertArrayEquals(nonce, payload.copyOfRange(2, 10))
```

C++ tests stage v2 with `now_epoch == 0`, reject TTL 0/61, reject duplicate nonce, and verify `clearSensitiveState()` clears credentials and replay memory.

- [ ] **Step 2: Run Android focused test and FireflyCoreTests compile RED**

Run `gradlew.bat :app:testDebugUnitTest --tests "com.fireflyos.companion.wifi.WifiProvisioningTest"`. Expected: schema assertion fails. Compile FireflyCoreTests; expected missing `clearSensitiveState` or v2 assertion failure.

- [ ] **Step 3: Implement v2 monotonic decoding**

Use fixed replay entries:

```cpp
struct ProvisioningNonceRecord {
    uint8_t bytes[8]{};
    uint32_t expires_at_ms = 0;
    bool valid = false;
};
```

Decode schema 2 from TTL/nonce/lengths, set confirmation and replay deadlines from `now_ms`, and use signed wrap-safe deadline comparison. Keep schema 1 only when `now_epoch > 0`.

- [ ] **Step 4: Implement one cleanup primitive**

Add `bool clearSensitiveState()` that stops Wi-Fi, clears the NVS store, zeroes SSID/password/pending credentials, nonce records, and provisioning state. Make `forgetNetwork()` call it and then set `Forgotten` in its caller.

- [ ] **Step 5: Run focused Android test, Python contracts, and FireflyCoreTests compile GREEN**

Expected: zero failures and compile exit 0.

### Task 3: Cross-core TimeService and complete SNTP lifecycle

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/TimeService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/TimeService.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Test: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Test: `tests/python/test_companion_features_contract.py`

- [ ] **Step 1: Add failing lock/lifecycle contracts**

Require `StaticSemaphore_t`, `xSemaphoreCreateRecursiveMutexStatic`, a non-inline locked `networkSyncPending()`, and a single `stop_network_time_request()` called when NTP becomes inactive, alarms defer sync, and sync completes.

- [ ] **Step 2: Run the focused Python contract RED**

Run `python -m unittest tests.python.test_companion_features_contract -v`. Expected: new mutex and stop-helper assertions fail.

- [ ] **Step 3: Add a static recursive mutex to TimeService**

Initialize it in the constructor and lock all public reads/writes. Recursive locking is required because `applyNetworkTime()` and `flushDeferredNetworkTime()` call `setLocalTime()`.

```cpp
mutable StaticSemaphore_t mutex_storage_{};
mutable SemaphoreHandle_t mutex_ = nullptr;
```

- [ ] **Step 4: Centralize SNTP cleanup**

```cpp
void stop_network_time_request() {
    if(ntp_request_configured) esp_sntp_stop();
    ntp_request_configured = false;
}
```

Call it in every terminal/inactive branch before releasing the NTP purpose. Do not call any LVGL API from this helper.

- [ ] **Step 5: Run Python contract and FireflyCoreTests compile GREEN**

Expected: zero failures and compile exit 0.

### Task 4: One 15-second HTTPS budget

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/WeatherService.cpp`
- Test: `tests/python/test_weather_payload.py`
- Test: `tests/python/test_companion_features_contract.py`

- [ ] **Step 1: Add a failing source contract for a shared start time and remaining budget**

The contract must require the timer to start before `request.begin()` and require timeout values to be reset to the remaining budget after `GET()`.

- [ ] **Step 2: Run the focused Python tests RED**

Run `python -m unittest tests.python.test_weather_payload tests.python.test_companion_features_contract -v`. Expected: the new total-budget contract fails.

- [ ] **Step 3: Implement remaining-budget calculation**

Use one `started_ms` and a helper that returns zero after 15 seconds. Apply the initial budget to connect/HTTP/TLS, recompute after `GET()`, apply the remaining budget to HTTP/TLS reads, and use the same start time in the body loop.

- [ ] **Step 4: Run the focused Python tests GREEN**

Expected: zero failures.

### Task 5: Convert and integrate the approved weather icons

**Files:**
- Create: `tools/assets/convert_weather_icons.py`
- Create: `libraries/FireflyOS/src/firefly/apps/weather/WeatherIcons.h`
- Create: `libraries/FireflyOS/src/firefly/apps/weather/WeatherIcons.cpp`
- Modify: `libraries/FireflyOS/src/firefly/apps/weather/WeatherApp.h`
- Modify: `libraries/FireflyOS/src/firefly/apps/weather/WeatherApp.cpp`
- Modify: `tests/python/test_companion_features_contract.py`
- Read only: `docs/UI预览/05-天气与更新/天气图标母图-v1.png`

- [ ] **Step 1: Add a failing resource contract**

Require twelve `lv_img_dsc_t` descriptors, `48 × 48`, `LV_IMG_CF_ALPHA_8BIT`, exact `data_size = 2304`, WeatherApp `lv_img_set_src`, and absence of the placeholder strings `SUN`, `PART`, `RAIN`, `STORM`, `SNOW`, `FOG`.

- [ ] **Step 2: Run the focused Python contract RED**

Expected: missing WeatherIcons files/descriptors.

- [ ] **Step 3: Implement deterministic conversion**

The converter opens the approved 1280×1280 mother image, crops a 4×3 grid of 320px cells in row-major order, converts luminance to alpha, scales each cell to 48×48 with LANCZOS, and emits fixed C arrays/descriptors. It must never write the source image or `image/图片生成提示词`.

- [ ] **Step 4: Replace the text placeholder with an LVGL image**

Change the icon member to `lv_obj_t * icon_image_`, create it with `lv_img_create`, map WMO codes to `const lv_img_dsc_t *`, and call `lv_img_set_src` only from WeatherApp UI methods.

- [ ] **Step 5: Run the converter twice and verify deterministic output**

Hash `WeatherIcons.cpp` after both runs. Expected: identical SHA-256. Run the Python contract; expected zero failures.

### Task 6: BulkTransfer v2 preflight and explicit command results

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/BulkTransferService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/BulkTransferService.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/transfer/BulkTransfer.kt`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/sync/CompanionController.kt`
- Test: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Test: `AndroidCompanion/app/src/test/java/com/fireflyos/companion/transfer/BulkTransferCodecTest.kt`

- [ ] **Step 1: Add failing v2 codec tests**

Assert Start encodes request ID, LAN flag, 64-bit size, binary SHA and UTF-8 managed path; Cancel encodes its request ID; Ready and Status decode only when the echoed request ID and payload lengths are valid.

- [ ] **Step 2: Add failing C++ preflight/result tests**

Assert invalid path, zero/over-limit size and insufficient space fail before token/transport creation. Assert two Busy commands with unchanged service state still increment `result_generation` and retain their respective request IDs.

- [ ] **Step 3: Run Android focused test and FireflyCoreTests compile RED**

Expected: missing v2 APIs/fields.

- [ ] **Step 4: Implement fixed-capacity negotiated metadata**

Extend `BulkTransferRequest`/snapshot with request ID, declared size, 32-byte SHA and 192-byte path. Validate all of it before random token generation. `beginFile()` must require exact path, size and SHA equality with the negotiated values.

- [ ] **Step 5: Implement explicit result generation**

Every valid Start/Cancel command records `request_id`, state, failure and increments a fixed-width generation counter. `FireflyInteraction` tracks generation rather than only `BulkTransferState`, so repeated Busy/Error results are transmitted.

- [ ] **Step 6: Run focused Android tests and FireflyCoreTests compile GREEN**

Expected: zero Android failures and compile exit 0.

### Task 7: Android cancellation and transfer lifecycle

**Files:**
- Modify: `AndroidCompanion/app/src/main/res/layout/activity_main.xml`
- Modify: `AndroidCompanion/app/src/main/res/values/strings.xml`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/MainActivity.kt`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/sync/CompanionController.kt`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/ConnectionRepository.kt`
- Test: `AndroidCompanion/app/src/test/java/com/fireflyos/companion/transfer/BulkTransferCodecTest.kt`
- Test: `tests/python/test_android_companion_scaffold_contract.py`

- [ ] **Step 1: Add failing UI/cancellation contracts**

Require a `bulkCancelButton` with `android:minHeight="48dp"`, a tracked upload `Job`, one `cancelBulkTransfer()` path, BLE Cancel emission, network release, and cancellation calls from permission denial and SoftAP failure.

- [ ] **Step 2: Run Android and Python focused tests RED**

Expected: cancel control/state is missing.

- [ ] **Step 3: Implement one idempotent Android cancellation path**

Cancel the tracked job, close/release the Android network, clear pending file/session, optionally send authenticated BLE Cancel using the active request ID, and update UI exactly once. Do not block the main thread.

- [ ] **Step 4: Send complete metadata in Start**

Generate a monotonically wrapping nonzero request ID after hashing; send path, size and SHA through `CompanionController`. Enable Cancel while hashing, awaiting Ready, connecting SoftAP, or uploading.

- [ ] **Step 5: Run focused tests GREEN**

Expected: zero failures.

### Task 8: Error preservation, orphan cleanup, and bounded large-file tests

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/BulkTransferService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/BulkTransferService.cpp`
- Modify: `libraries/FireflyOS/src/firefly/services/StorageService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/StorageService.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/transfer/BulkTransfer.kt`
- Create: `AndroidCompanion/app/src/test/java/com/fireflyos/companion/transfer/BulkTransferUploaderTest.kt`
- Test: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: Add failing error and orphan cleanup tests**

Extend the fake storage with a fixed orphan list. Assert cleanup removes only `.part` files below the four managed roots, leaves formal files untouched, and preserves LowPower/SdUnavailable when an HTTP write callback returns false.

- [ ] **Step 2: Add bounded uploader tests**

Use a generated-pattern `InputStream` rather than byte arrays to test 1KB, 1MB and 32MB uploads. Assert maximum read request is at most 64KB and the declared byte count is exact.

- [ ] **Step 3: Run focused C++ compile and Android tests RED**

Expected: cleanup API and uploader coverage are missing.

- [ ] **Step 4: Implement startup orphan cleanup**

Add a fixed-root, non-recursive cleanup API in StorageService/SdBulkTransferStorage and call it after successful SD attachment during startup. Never enumerate or delete outside the four literal managed roots.

- [ ] **Step 5: Preserve sink failure reasons**

Expose the current `BulkTransferFailure` through the sink or transport result. In `handleUpload()`, call generic `WriteFailed` only if the sink has not already moved to a terminal LowPower/SdUnavailable/WriteFailed reason.

- [ ] **Step 6: Run focused tests GREEN**

Expected: zero failures and FireflyCoreTests compile exit 0.

### Task 9: Full automatic verification and requirement audit

**Files:**
- Modify: `docs/模块说明/09-WiFi天气与传输.md`
- Verify only: all Task 1—8 files

- [ ] **Step 1: Run all Python tests**

Run `python -m unittest discover -s tests/python -v`. Expected: zero failures.

- [ ] **Step 2: Run Android unit tests and Debug APK build**

Run `gradlew.bat :app:testDebugUnitTest :app:assembleDebug` with the configured Android Studio JBR. Expected: zero failures and Debug APK present.

- [ ] **Step 3: Compile FireflyCoreTests, AudioProbe, and Firefly**

Run the repository build commands used by `tools/verify_all.ps1`. Expected: all three compile commands exit 0.

- [ ] **Step 4: Run static and repository checks**

Run `git diff --check`, inspect `git status --short`, verify no Task 6+ implementation files were created or modified, and verify `image/图片生成提示词` has no deletion or content change.

- [ ] **Step 5: Re-read the approved design and audit every completion criterion**

Update the module document with provisioning schema v2, BulkTransfer schema v2, absolute/idle deadline semantics and deterministic cleanup behavior. Record automatic evidence separately from remaining hardware `PENDING` items. Do not claim power, hidden-network, SD hot-unplug, or real HTTP/SoftAP success without device evidence.
