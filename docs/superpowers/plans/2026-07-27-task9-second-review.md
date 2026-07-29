# Task 9 Second Review Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the four remaining Task 9 gaps without adding pages or changing the existing local-first watch behavior.

**Architecture:** Keep the fixed four-entry `CompanionSettingsSnapshot` as the authoritative conflict journal. Add pure, host-testable state/presentation helpers for Music and Weather, then connect them to the existing LVGL screens only from the main loop. Use `SettingsGet { schema=1 }` as a request and a full `SettingsSet` snapshot as the watch response; Android resolves all four kinds independently and sends the winning snapshot back once.

**Tech Stack:** C++17/Arduino ESP32, LVGL 8.3.11, Android Kotlin Views/XML, JUnit 4, Python `unittest`.

---

### Task 1: Isolate BLE short and long actions

**Files:**
- Modify: `Firefly/Firefly.ino`
- Modify: `tests/python/test_companion_features_contract.py`

- [ ] **Step 1: Write the failing contract**

```python
self.assertIn("ble_quick_action_cb, LV_EVENT_SHORT_CLICKED", sketch)
self.assertIn("ble_find_phone_action_cb", sketch)
self.assertIn("LV_EVENT_LONG_PRESSED", sketch)
self.assertNotIn("ble_quick_action_cb\n    );", clicked_registration)
```

- [ ] **Step 2: Run the Task 9 Python contract and confirm it fails because the BLE action still uses `LV_EVENT_CLICKED`.**

- [ ] **Step 3: Register `ble_quick_action_cb` for `LV_EVENT_SHORT_CLICKED` and keep `ble_find_phone_action_cb` on `LV_EVENT_LONG_PRESSED`.**

- [ ] **Step 4: Run the contract and confirm this case passes.**

### Task 2: Make Music target explicit

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/apps/music/MusicApp.h`
- Modify: `libraries/FireflyOS/src/firefly/apps/music/MusicApp.cpp`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `tests/python/test_companion_features_contract.py`

- [ ] **Step 1: Add failing host tests for a local-default selector**

```cpp
firefly::MusicControlSelector selector;
expect_true(selector.target() == firefly::MusicControlTarget::LocalLibrary,
            "music target defaults local");
selector.toggle();
expect_true(selector.target() == firefly::MusicControlTarget::PhoneRemote,
            "long refresh can select phone");
selector.noteLocalTrackSelected();
expect_true(selector.target() == firefly::MusicControlTarget::LocalLibrary,
            "track selection returns local");
```

- [ ] **Step 2: Compile `FireflyCoreTests` and confirm RED because `MusicControlSelector` does not exist.**

- [ ] **Step 3: Add the fixed selector; change Refresh to `SHORT_CLICKED` plus a `LONG_PRESSED` target toggle; route play/previous/next/volume by selector only; make track selection choose Local; do not mutate target during scans.**

- [ ] **Step 4: Centralize status/track/time rendering so both target names and the opposite “Hold Refresh” action are always visible.**

- [ ] **Step 5: Compile `FireflyCoreTests` and run the Python contract GREEN.**

### Task 3: Add bidirectional settings reconciliation

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/CompanionSyncService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/CompanionSyncService.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `Firefly/FireflyApp.h`
- Modify: `libraries/FireflyOS/src/firefly/apps/themes/ThemesApp.h`
- Modify: `libraries/FireflyOS/src/firefly/apps/themes/ThemesApp.cpp`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/data/SettingsSync.kt`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/sync/CompanionController.kt`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/ble/ConnectionRepository.kt`
- Modify: `AndroidCompanion/app/src/main/java/com/fireflyos/companion/MainActivity.kt`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `AndroidCompanion/app/src/test/java/com/fireflyos/companion/data/SettingsSyncTest.kt`
- Modify: `AndroidCompanion/app/src/test/java/com/fireflyos/companion/sync/CompanionControllerTest.kt`
- Modify: `tests/python/test_companion_features_contract.py`

- [ ] **Step 1: Add failing C++ tests for `recordLocalSetting`, independent revision increments, local/remote timestamp winners, restart restore, Alarm slot 1 preserving slot 0, and a `SettingsGet`/full snapshot response round trip.**

```cpp
expect_true(service.recordLocalSetting(
    firefly::CompanionSettingKind::Brightness, brightness, 1000),
    "local brightness recorded");
expect_true(service.buildSettingsSnapshot(
    service.settingsSnapshot(), sequence, response),
    "full settings response encoded");
```

- [ ] **Step 2: Add failing Kotlin tests for a bounded local snapshot store and controller reconciliation**

```kotlin
controller.handleInbound(Frame(type = MessageType.SettingsSet, payload = watchPayload))
assertEquals(phoneNewer, applied.single()[SettingKind.Brightness])
assertEquals(MessageType.SettingsSet, sent.last().first)
```

- [ ] **Step 3: Run both targeted suites and confirm failures are caused by missing local record/round-trip APIs.**

- [ ] **Step 4: Implement C++ local record and full snapshot encoding. Validate the entire staged snapshot and persist it once before committing. Keep Alarm as one setting kind whose value names the most recently changed slot.**

- [ ] **Step 5: Route every explicit local brightness, volume, Alarm, and applied-theme operation through the local record chain with epoch milliseconds; retain existing per-setting runtime behavior and never overwrite unrelated Alarm slots.**

- [ ] **Step 6: Accept only `SettingsGet {1}`, reply from the main-loop event handler with an authenticated ACK-required full `SettingsSet`, and leave malformed requests unapplied.**

- [ ] **Step 7: Implement Android’s four-kind bounded store and reconciliation. On inbound full `SettingsSet`, resolve by `changed_at`, then uint32 serial revision, apply winners to current UI state, and send the winning full snapshot back. Queue `SettingsGet {1}` when an authenticated connection becomes ready.**

- [ ] **Step 8: Run targeted C++/Kotlin/Python tests GREEN.**

### Task 4: Render companion Weather in the existing app shell

**Files:**
- Modify: `libraries/FireflyOS/src/firefly/services/CompanionSyncService.h`
- Modify: `libraries/FireflyOS/src/firefly/services/CompanionSyncService.cpp`
- Modify: `libraries/FireflyOS/src/firefly/ui/screens/AppShellScreen.h`
- Modify: `libraries/FireflyOS/src/firefly/ui/screens/AppShellScreen.cpp`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `Firefly/Firefly.ino`
- Modify: `Firefly/FireflyApp.h`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Modify: `tests/python/test_companion_features_contract.py`

- [ ] **Step 1: Add failing host tests for a fixed `CompanionWeatherView` showing city, current, high/low, code, fresh/stale/expired, and offline state.**

```cpp
const firefly::CompanionWeatherView view =
    firefly::CompanionWeatherPresenter::build(weather, freshness, false);
expect_true(strstr(view.status, "offline") != nullptr,
            "cached weather marks offline");
```

- [ ] **Step 2: Compile `FireflyCoreTests` and confirm RED because the presenter is missing.**

- [ ] **Step 3: Implement the fixed presenter and extend `AppShellScreen` with fixed Weather labels in its existing content area.**

- [ ] **Step 4: Refresh Weather only from the main loop, once per second while the Weather route is active, using `weatherAt(time(nullptr), connectivity_service.connected())`.**

- [ ] **Step 5: Compile and run contracts GREEN.**

### Task 5: Full verification

- [ ] **Step 1: Run targeted Android tests for settings reconciliation and reliable delivery.**
- [ ] **Step 2: Run Android `testDebugUnitTest`.**
- [ ] **Step 3: Run `tests/python/test_companion_features_contract.py`.**
- [ ] **Step 4: Compile `FireflyCoreTests`.**
- [ ] **Step 5: Compile the full `Firefly` firmware.**
- [ ] **Step 6: Run `git diff --check` and report only Task 9 files. Do not stage, commit, merge, push, delete, or touch Task 10.**
