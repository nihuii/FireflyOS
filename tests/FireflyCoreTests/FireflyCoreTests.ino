#include <Arduino.h>
#include <FireflyOS.h>
#include <string.h>
#include <firefly/apps/clock/ClockApp.h>
#include <firefly/apps/calendar/CalendarApp.h>
#include <firefly/apps/settings/SettingsApp.h>
#include <firefly/apps/tools/ToolsApp.h>
#include <firefly/services/AlarmService.h>
#include <firefly/services/InputService.h>
#include <firefly/services/MotionService.h>
#include <firefly/services/PowerService.h>
#include <firefly/services/StorageService.h>
#include <firefly/services/TimeService.h>
#include <firefly/services/ThemePackageService.h>
#include <firefly/services/AudioService.h>
#include <firefly/services/FileScanService.h>
#include <firefly/hal/SdCardDevice.h>
#include <firefly/apps/files/FilesApp.h>
#include <firefly/apps/music/MusicApp.h>
#include <firefly/apps/recorder/RecorderApp.h>
#include <firefly/apps/themes/ThemesApp.h>

static uint16_t failures = 0;

static void expect_true(bool value, const char * name) {
    if(!value) {
        ++failures;
        Serial.printf("FAIL %s\n", name);
    }
}

static void test_event_bus_fifo() {
    firefly::EventBus bus;
    firefly::SystemEvent first{firefly::EventType::ShortPress, 1, 10};
    firefly::SystemEvent second{firefly::EventType::EnterSleep, 2, 20};
    expect_true(bus.post(first), "post first event");
    expect_true(bus.post(second), "post second event");
    firefly::SystemEvent out{};
    expect_true(bus.take(out) && out.type == first.type && out.value == 1,
                "event FIFO first");
    expect_true(bus.take(out) && out.type == second.type && out.value == 2,
                "event FIFO second");
    expect_true(!bus.take(out), "empty queue returns false");
}

static void test_event_bus_full_policy() {
    firefly::EventBus bus;
    for(uint8_t i = 0; i < firefly::EventBus::kCapacity; ++i) {
        const firefly::EventPriority priority = i == 3
            ? firefly::EventPriority::Refresh
            : firefly::EventPriority::Normal;
        expect_true(
            bus.post({firefly::EventType::BatteryChanged, i, i, priority}),
            "fill event bus"
        );
    }
    expect_true(
        !bus.post({firefly::EventType::TimeChanged, 99, 99}),
        "normal event rejected when full"
    );
    expect_true(
        bus.post({firefly::EventType::AlarmTriggered, 7, 100,
                  firefly::EventPriority::Critical}),
        "critical event replaces refresh"
    );
    expect_true(bus.size() == firefly::EventBus::kCapacity,
                "replacement keeps capacity");

    bool found_critical = false;
    firefly::SystemEvent out{};
    while(bus.take(out)) {
        found_critical = found_critical || out.type == firefly::EventType::AlarmTriggered;
    }
    expect_true(found_critical, "critical event remains queued");
}

static void test_event_bus_preserves_full_critical_queue() {
    firefly::EventBus bus;
    for(uint8_t i = 0; i < firefly::EventBus::kCapacity; ++i) {
        expect_true(
            bus.post({firefly::EventType::AlarmTriggered, i, i,
                      firefly::EventPriority::Critical}),
            "fill critical event bus"
        );
    }
    expect_true(
        !bus.post({firefly::EventType::Wake, 0, 200,
                   firefly::EventPriority::Critical}),
        "critical event rejected without refresh slot"
    );
}

static void test_state_store_revision() {
    firefly::StateStore store;
    const uint32_t before = store.revision();
    firefly::BatteryState battery{};
    battery.percent = 73;
    battery.temperature_c = 31;
    battery.battery_mv = 3970;
    battery.valid = true;
    store.setBattery(battery);
    const firefly::SystemState snapshot = store.snapshot();
    expect_true(snapshot.battery.percent == 73, "battery snapshot");
    expect_true(store.revision() == before + 1, "state revision increments");
    store.setBattery(battery);
    expect_true(store.revision() == before + 1, "unchanged state keeps revision");
    store.setSleepState(true, true);
    expect_true(store.snapshot().sleeping && store.snapshot().screen_off,
                "sleep state snapshot");
}

static void test_capability_registry() {
    firefly::CapabilityRegistry capabilities;
    expect_true(!capabilities.has(firefly::Capability::Motion),
                "capability starts unavailable");
    capabilities.set(firefly::Capability::Motion, true);
    expect_true(capabilities.has(firefly::Capability::Motion),
                "capability can be enabled");
    capabilities.set(firefly::Capability::Motion, false);
    expect_true(!capabilities.has(firefly::Capability::Motion),
                "capability can be disabled");
    expect_true(
        firefly::capabilityBit(firefly::Capability::Audio) !=
            firefly::capabilityBit(firefly::Capability::Sd),
        "capability bits are distinct"
    );
}

static void test_app_registry() {
    firefly::AppRegistry registry;
    const uint16_t display = firefly::capabilityBit(firefly::Capability::Display);
    const firefly::AppDescriptor settings{"settings", "Settings", display};
    expect_true(registry.add(settings), "register settings");
    expect_true(registry.count() == 1, "registry count");
    expect_true(registry.find("settings") != nullptr, "find settings");
    expect_true(!registry.add({"settings", "Duplicate", display}),
                "reject duplicate app id");
    expect_true(!registry.add({"", "Empty", display}), "reject empty app id");

    firefly::CapabilityRegistry capabilities;
    expect_true(!registry.available(settings, capabilities),
                "missing capability hides app");
    capabilities.set(firefly::Capability::Display, true);
    expect_true(registry.available(settings, capabilities),
                "available capability shows app");
}

static void test_lifecycle_and_resource_governor() {
    firefly::SystemLifecycle lifecycle;
    expect_true(lifecycle.phase() == firefly::SystemPhase::Booting,
                "lifecycle starts booting");
    expect_true(lifecycle.transition(firefly::SystemPhase::Locked),
                "boot completes to lock");
    expect_true(!lifecycle.transition(firefly::SystemPhase::Updating),
                "locked cannot jump into update");
    expect_true(lifecycle.transition(firefly::SystemPhase::Active),
                "unlock enters active");
    expect_true(!lifecycle.transition(firefly::SystemPhase::Updating),
                "update requires resource governor");

    firefly::ResourceGovernor resources;
    expect_true(resources.acquire(firefly::ResourceKind::AudioPlayback),
                "audio playback lease");
    expect_true(!resources.acquire(firefly::ResourceKind::AudioRecording),
                "recording conflicts with playback");
    expect_true(!resources.acquire(firefly::ResourceKind::Ota),
                "ota conflicts with playback");
    expect_true(!lifecycle.transition(firefly::SystemPhase::Updating, resources),
                "playback blocks update phase");
    resources.release(firefly::ResourceKind::AudioPlayback);
    resources.setConstrained(true);
    expect_true(!resources.canAcquire(firefly::ResourceKind::Ota),
                "power constraint rejects ota");
    expect_true(!lifecycle.transition(firefly::SystemPhase::Updating, resources),
                "power constraint blocks update phase");
    expect_true(!resources.acquire(firefly::ResourceKind::HighRateMotion),
                "power constraint rejects high rate motion");
    resources.setConstrained(false);
    expect_true(resources.canAcquire(firefly::ResourceKind::Ota),
                "idle resources allow ota");
    expect_true(lifecycle.transition(firefly::SystemPhase::Updating, resources),
                "guarded active phase can enter update");
}

static void test_app_manager_publishes_requests() {
    firefly::AppRegistry registry;
    firefly::CapabilityRegistry capabilities;
    firefly::EventBus bus;
    capabilities.set(firefly::Capability::Display, true);
    registry.add({"settings", "Settings",
                  firefly::capabilityBit(firefly::Capability::Display)});
    firefly::AppManager manager(registry, capabilities, bus);
    expect_true(manager.requestOpen("settings", 42), "request known app");
    firefly::SystemEvent event{};
    expect_true(bus.take(event) &&
                event.type == firefly::EventType::AppOpenRequested,
                "app open event published");
    expect_true(!manager.requestOpen("missing", 43), "reject missing app");
    manager.confirmOpened("settings");
    expect_true(manager.hasCreatedPage(), "opened page recorded");
}

class FakePowerDevice : public firefly::PowerDevice {
public:
    firefly::BatteryState readBattery() override {
        firefly::BatteryState state{};
        state.percent = 73;
        state.valid = true;
        return state;
    }

    void setDisplayBrightness(uint8_t value) override {
        brightness = value;
    }

    firefly::PowerButtonEvent readPowerButtonEvent() override {
        const firefly::PowerButtonEvent result = next_button_event;
        next_button_event = firefly::PowerButtonEvent::None;
        return result;
    }

    void shutdown() override {
        shutdown_requested = true;
    }

    uint8_t brightness = 0;
    firefly::PowerButtonEvent next_button_event =
        firefly::PowerButtonEvent::None;
    bool shutdown_requested = false;
};

class FakeClockDevice : public firefly::ClockDevice {
public:
    bool readEpoch(int64_t & epoch_seconds) override {
        if(!read_ok) {
            return false;
        }
        epoch_seconds = epoch;
        return true;
    }

    bool writeEpoch(int64_t epoch_seconds) override {
        if(!write_ok) {
            return false;
        }
        epoch = epoch_seconds;
        last_written = epoch_seconds;
        wrote = true;
        return true;
    }

    bool read_ok = true;
    bool write_ok = true;
    bool wrote = false;
    int64_t epoch = 0;
    int64_t last_written = 0;
};

static void test_hardware_abstraction() {
    FakePowerDevice power;
    const firefly::BatteryState battery = power.readBattery();
    expect_true(battery.valid && battery.percent == 73,
                "power interface returns value state");
    power.setDisplayBrightness(128);
    expect_true(power.brightness == 128, "power interface controls brightness");
    power.next_button_event = firefly::PowerButtonEvent::LongPress;
    expect_true(power.readPowerButtonEvent() ==
                    firefly::PowerButtonEvent::LongPress,
                "power interface exposes semantic button event");
    expect_true(power.readPowerButtonEvent() == firefly::PowerButtonEvent::None,
                "power button event is consumed once");
    power.shutdown();
    expect_true(power.shutdown_requested,
                "power interface exposes controlled shutdown");

    firefly::I2cBusManager i2c(Wire);
    expect_true(i2c.lock(10), "i2c manager acquires mutex");
    i2c.unlock();
    expect_true(&i2c.wire() == &Wire, "i2c manager retains bus");

    firefly::Qmi8658ControlAdapter motion(i2c);
    firefly::Es8311ControlAdapter codec(i2c);
    expect_true(motion.address() == 0x6B, "qmi8658 default address");
    expect_true(codec.address() == 0x18, "es8311 default address");
    expect_true(!motion.readRegisters(0x00, nullptr, 1),
                "register read rejects null output");
    expect_true(!codec.writeRegisters(0x00, nullptr, 1),
                "register write rejects null input");
}

static void test_default_theme_tokens() {
    const firefly::UiTokens tokens = firefly::UiTheme::fireflyDefault();
    expect_true(tokens.bg_base == 0x0041, "AMOLED base is near black");
    expect_true(tokens.radius_card == 24, "card radius token");
    expect_true(tokens.touch_min == 48, "minimum touch target");

    const uint16_t pixels[] = {0x0000, 0x07E0, 0x07E0, 0xFFFF};
    const firefly::UiTokens sampled = firefly::UiTheme::sampleWallpaper(pixels, 2, 2);
    expect_true(sampled.touch_min == 48, "sampled theme keeps geometry");
    expect_true(sampled.firefly_primary != tokens.firefly_primary,
                "wallpaper sampling changes accent");
    expect_true(firefly::UiTheme::samAlert().sam_ignition != tokens.sam_ignition,
                "sam alert has distinct ignition color");
}

static void test_navigation_stack() {
    firefly::NavigationController nav;
    expect_true(nav.current() == firefly::Route::Lock, "starts locked");
    expect_true(nav.open(firefly::Route::Home), "open home");
    expect_true(nav.open(firefly::Route::Settings), "open settings");
    expect_true(nav.back() == firefly::Route::Home, "back to home");
    expect_true(nav.back() == firefly::Route::Lock, "home back locks");

    expect_true(nav.open(firefly::Route::Home), "reopen home");
    for(uint8_t i = 0; i < firefly::NavigationController::kDepth - 2; ++i) {
        expect_true(nav.open(firefly::Route::Tools), "fill navigation stack");
    }
    expect_true(!nav.open(firefly::Route::Diagnostics), "reject stack overflow");
    nav.lock();
    expect_true(nav.current() == firefly::Route::Lock && nav.depth() == 1,
                "lock resets navigation");
}

static void test_overlay_priority_policy() {
    expect_true(firefly::SystemOverlayHost::acceptsPriority(2, 4),
                "alarm can cover charging");
    expect_true(!firefly::SystemOverlayHost::acceptsPriority(4, 2),
                "charging cannot cover alarm");
    expect_true(!firefly::SystemOverlayHost::acceptsPriority(0, 0),
                "priority zero is invalid");
}

static void test_alarm_next_trigger() {
    firefly::AlarmService service;
    firefly::Alarm alarm{};
    alarm.configured = true;
    alarm.enabled = true;
    alarm.hour = 7;
    alarm.minute = 30;
    alarm.days_mask = 0x7F;
    service.set(0, alarm);
    const int64_t now = 1767221940;  // 2026-01-01 07:19:00 +08
    const auto next = service.nextTrigger(now);
    expect_true(next.valid, "next alarm exists");
    expect_true(next.slot == 0, "next alarm slot");
    expect_true(next.epoch_seconds > now, "next alarm is future");
}

static void test_time_service_invalid_rtc() {
    FakeClockDevice clock;
    clock.read_ok = false;
    firefly::TimeService time(clock);
    expect_true(!time.begin(), "invalid rtc begin fails");
    const firefly::TimeSnapshot snapshot = time.now();
    expect_true(!snapshot.valid, "invalid rtc snapshot marked invalid");
    expect_true(snapshot.epoch_seconds == 0, "invalid rtc does not fake date");
    time.tick();
    expect_true(!time.now().valid, "invalid rtc tick stays invalid");
}

static void test_time_service_reload_set_and_tick() {
    FakeClockDevice clock;
    clock.epoch = 1000;
    firefly::TimeService time(clock);
    expect_true(time.begin(), "valid rtc begin succeeds");
    expect_true(time.now().valid && time.now().epoch_seconds == 1000,
                "time service reads rtc");
    time.tick();
    expect_true(time.now().epoch_seconds == 1001, "tick advances one second");
    expect_true(time.setLocalTime(2000), "set local time succeeds");
    expect_true(clock.wrote && clock.last_written == 2000,
                "set local time writes rtc");
    time.tick();
    expect_true(time.now().epoch_seconds == 2001,
                "tick advances manually set time");
    clock.epoch = 3000;
    const firefly::TimeSnapshot reloaded = time.reloadRtc();
    expect_true(reloaded.valid && reloaded.epoch_seconds == 3000,
                "reload rtc refreshes snapshot");
}

static void test_countdown_timer_uses_target_time() {
    firefly::CountdownTimer timer;
    timer.start(10 * 60 * 1000UL, 1000);
    expect_true(timer.running(), "countdown timer starts running");
    expect_true(timer.remainingMs(1000) == 600000UL,
                "countdown stores full duration");
    expect_true(timer.remainingMs(601000) == 0,
                "countdown reaches zero at target");
    expect_true(timer.expired(601001), "countdown reports expired after target");
}

static void test_countdown_pause_resume_and_one_shot_expiry() {
    firefly::CountdownTimer timer;
    timer.start(60000, 1000);
    timer.pause(11000);
    expect_true(!timer.running() && timer.remainingMs(50000) == 50000,
                "paused countdown preserves remaining time");
    timer.resume(20000);
    expect_true(timer.remainingMs(69999) == 1,
                "resumed countdown uses a new target");
    expect_true(timer.consumeExpired(70000),
                "countdown publishes expiry once");
    expect_true(!timer.consumeExpired(70001),
                "countdown expiry is one shot");
}

static void test_stopwatch_survives_page_visibility_changes() {
    firefly::StopwatchSession stopwatch;
    stopwatch.start(1000000);
    expect_true(stopwatch.elapsedUs(4000000) == 3000000,
                "stopwatch follows monotonic time without page state");
    stopwatch.pause(5000000);
    stopwatch.start(9000000);
    expect_true(stopwatch.elapsedUs(10000000) == 5000000,
                "stopwatch resumes accumulated time");
}

static void test_stopwatch_uses_monotonic_time() {
    firefly::StopwatchSession stopwatch;
    stopwatch.start(1000000LL);
    expect_true(stopwatch.running(), "stopwatch starts running");
    expect_true(stopwatch.elapsedUs(2500000LL) == 1500000LL,
                "stopwatch elapsed follows monotonic time");
    stopwatch.pause(3000000LL);
    expect_true(!stopwatch.running(), "stopwatch pauses");
    expect_true(stopwatch.elapsedUs(5000000LL) == 2000000LL,
                "paused stopwatch holds elapsed");
    stopwatch.start(7000000LL);
    expect_true(stopwatch.elapsedUs(8000000LL) == 3000000LL,
                "stopwatch resumes from accumulated elapsed");
    stopwatch.reset();
    expect_true(stopwatch.elapsedUs(9000000LL) == 0,
                "stopwatch reset clears elapsed");
}

static void test_settings_app_command_queue() {
    firefly::SettingsCommandQueue queue;
    expect_true(queue.post({firefly::SettingsCommandType::SetBrightness, 72}),
                "settings command queues brightness");
    firefly::SettingsCommand command{};
    expect_true(queue.take(command) &&
                command.type == firefly::SettingsCommandType::SetBrightness &&
                command.value == 72,
                "settings command preserves payload");
    expect_true(!queue.take(command), "settings command queue drains");
}

static void test_settings_commands_preserve_time_and_alarm_payloads() {
    firefly::SettingsCommandQueue queue;
    firefly::SettingsCommand time{};
    time.type = firefly::SettingsCommandType::SetLocalTime;
    time.value = 1783008000LL;
    expect_true(queue.post(time), "settings accepts epoch command");
    firefly::SettingsCommand actual{};
    expect_true(queue.take(actual) && actual.value == 1783008000LL,
                "settings preserves 64 bit epoch");

    firefly::SettingsCommand alarm{};
    alarm.type = firefly::SettingsCommandType::SaveAlarm;
    alarm.slot = 1;
    alarm.alarm.configured = true;
    alarm.alarm.hour = 7;
    alarm.alarm.minute = 30;
    expect_true(queue.post(alarm), "settings accepts alarm command");
    expect_true(queue.take(actual) && actual.slot == 1 &&
                    actual.alarm.minute == 30,
                "settings preserves fixed alarm payload");
}

static void test_alarm_service_publishes_trigger_event_once() {
    firefly::AlarmService service;
    firefly::EventBus events;
    const int64_t now = 1767222600;
    const time_t raw = static_cast<time_t>(now);
    struct tm local{};
    localtime_r(&raw, &local);
    firefly::Alarm alarm{};
    alarm.configured = true;
    alarm.enabled = true;
    alarm.hour = static_cast<uint8_t>(local.tm_hour);
    alarm.minute = static_cast<uint8_t>(local.tm_min);
    alarm.days_mask = 0x7F;
    service.set(1, alarm);

    expect_true(service.publishTrigger(now, 1234, events),
                "alarm publishes due trigger");
    firefly::SystemEvent event{};
    expect_true(events.take(event) &&
                    event.type == firefly::EventType::AlarmTriggered &&
                    event.value == 1 && event.timestamp_ms == 1234,
                "alarm trigger event carries slot");
    expect_true(!service.publishTrigger(now, 1235, events),
                "alarm trigger publishes once per minute");
}

static void test_sd_paths_stay_inside_managed_root() {
    expect_true(firefly::SdCardDevice::isSafeRelativePath("Music/track.wav"),
                "SD accepts managed relative file");
    expect_true(firefly::SdCardDevice::isSafeRelativePath("Themes/Firefly/theme.json"),
                "SD accepts nested managed relative file");
    expect_true(!firefly::SdCardDevice::isSafeRelativePath("/Music/track.wav"),
                "SD rejects absolute path");
    expect_true(!firefly::SdCardDevice::isSafeRelativePath("Music/../Backups/file"),
                "SD rejects parent traversal");
    expect_true(!firefly::SdCardDevice::isSafeRelativePath("Music\\track.wav"),
                "SD rejects backslash traversal");
    expect_true(firefly::StorageService::isManagedPath(
                    "/FireflyOS/Themes/Firefly/theme.json"),
                "storage accepts a managed absolute path");
    expect_true(!firefly::StorageService::isManagedPath(
                    "/FireflyOS/Other/file.bin"),
                "storage rejects an unregistered top-level directory");
    expect_true(!firefly::StorageService::isManagedPath(
                    "/FireflyOS/Music/../Backups/file.bin"),
                "storage rejects parent traversal");
}

static void test_sd_removal_requires_two_consecutive_failures() {
    firefly::SdFailureMonitor monitor;
    expect_true(!monitor.noteResult(false), "first SD failure is tolerated");
    expect_true(monitor.noteResult(true) == false,
                "successful SD access clears failure count");
    expect_true(!monitor.noteResult(false), "failure count restarts after success");
    expect_true(monitor.noteResult(false),
                "second consecutive SD failure reports removal");
}

static void test_theme_manifest_validation() {
    static const char valid[] =
        "{\"schema\":1,\"id\":\"firefly-night\",\"name\":\"Firefly Night\","
        "\"author\":\"FireflyOS\",\"palette\":{"
        "\"bg_base\":\"#05090C\",\"bg_surface\":\"#0C1820\","
        "\"primary\":\"#5FE7C7\",\"secondary\":\"#6EC4D6\","
        "\"critical\":\"#FF5A5F\"},"
        "\"wallpaper\":\"wallpaper.rgb565\","
        "\"wallpaper_width\":410,\"wallpaper_height\":502,"
        "\"glance\":\"glance.png\","
        "\"icon_pack\":\"icons\"}";
    firefly::ThemePackageService service;
    firefly::ThemeManifest manifest{};
    firefly::ThemeValidationError error = firefly::ThemeValidationError::None;
    expect_true(service.parseManifest(valid, sizeof(valid) - 1, manifest, error),
                "valid theme manifest parses");
    expect_true(strcmp(manifest.id, "firefly-night") == 0,
                "theme manifest preserves id");
    expect_true(manifest.palette[2] == 0x5FE7C7,
                "theme manifest parses primary color");

    static const char traversal[] =
        "{\"schema\":1,\"id\":\"bad-theme\",\"name\":\"Bad\","
        "\"author\":\"Test\",\"palette\":{"
        "\"bg_base\":\"#05090C\",\"bg_surface\":\"#0C1820\","
        "\"primary\":\"#5FE7C7\",\"secondary\":\"#6EC4D6\","
        "\"critical\":\"#FF5A5F\"},"
        "\"wallpaper\":\"../wallpaper.rgb565\","
        "\"wallpaper_width\":410,\"wallpaper_height\":502,"
        "\"glance\":\"glance.png\","
        "\"icon_pack\":\"icons\"}";
    expect_true(!service.parseManifest(traversal, sizeof(traversal) - 1,
                                       manifest, error),
                "theme manifest rejects parent traversal");
}

static void test_audio_session_priority() {
    firefly::AudioSessionArbiter arbiter;
    expect_true(arbiter.acquire(firefly::AudioUse::Music),
                "music acquires audio");
    expect_true(arbiter.acquire(firefly::AudioUse::System),
                "system sound preempts music");
    expect_true(arbiter.acquire(firefly::AudioUse::Alarm),
                "alarm preempts system sound");
    expect_true(arbiter.current() == firefly::AudioUse::Alarm,
                "alarm owns audio");
    expect_true(!arbiter.acquire(firefly::AudioUse::Recorder),
                "recorder cannot interrupt alarm");
    arbiter.release(firefly::AudioUse::Music);
    expect_true(arbiter.current() == firefly::AudioUse::Alarm,
                "non-owner cannot release audio");
    arbiter.release(firefly::AudioUse::Alarm);
    expect_true(arbiter.current() == firefly::AudioUse::None,
                "owner releases audio");
}

static void test_pcm_wav_header_round_trip() {
    uint8_t header[44]{};
    expect_true(firefly::AudioService::buildWavHeader(
                    header, sizeof(header), 32000, 16000, 1),
                "build mono WAV header");
    firefly::WavInfo info{};
    expect_true(firefly::AudioService::parseWavHeader(
                    header, sizeof(header), info),
                "parse generated WAV header");
    expect_true(info.sample_rate == 16000 && info.channels == 1 &&
                    info.bits_per_sample == 16 && info.data_bytes == 32000,
                "WAV metadata round trips");
    header[20] = 3;
    expect_true(!firefly::AudioService::parseWavHeader(
                    header, sizeof(header), info),
                "reject non-PCM WAV");
}

static void test_alarm_ringtone_resources() {
    const char * expected[] = {
        "Trailblaze", "Starglow", "Night Sky", "Classic Bell"
    };
    for(uint8_t i = 0; i < firefly::AlarmService::kRingtoneCount; ++i) {
        const firefly::AlarmToneResource & resource =
            firefly::AlarmService::ringtoneResource(i);
        expect_true(strcmp(resource.name, expected[i]) == 0,
                    "ringtone index remains stable");
        expect_true(resource.samples != nullptr && resource.frames > 0,
                    "ringtone has built-in PCM");
        expect_true(resource.sample_rate == 16000 && resource.loop,
                    "ringtone is looping 16 kHz mono PCM");
        expect_true(resource.frames <=
                        firefly::AlarmService::kMaximumRingtoneFrames,
                    "ringtone stays within twenty-second budget");
    }
}

static void test_media_app_fixed_boundaries() {
    firefly::FileListItem music{};
    strlcpy(music.name, "track.wav", sizeof(music.name));
    expect_true(firefly::FilesApp::canDeleteFile("Music", music),
                "ordinary music file can be deleted");
    expect_true(!firefly::FilesApp::canDeleteFile("Logs", music),
                "log files cannot be deleted from FilesApp");
    strlcpy(music.name, "../escape.wav", sizeof(music.name));
    expect_true(!firefly::FilesApp::canDeleteFile("Music", music),
                "delete rejects path escape");
    expect_true(firefly::FilesApp::kPageSize == 32 &&
                    firefly::MusicApp::kMaxTracks == 128,
                "media indexes use fixed capacities");

    char name[48]{};
    expect_true(firefly::RecorderApp::makeRecordingName(
                    0, 42, name, sizeof(name)),
                "fallback recording name formats");
    expect_true(strcmp(name, "REC_000042.wav") == 0,
                "fallback recording name is monotonic");
}

static void test_file_scan_page_is_fixed_and_bounded() {
    firefly::FileScanPage page{};
    expect_true(firefly::FileScanService::kPageSize == 32,
                "file scanner page holds 32 items");
    expect_true(firefly::FileScanService::kEntriesPerTick == 4,
                "file scanner work per tick is bounded");
    expect_true(sizeof(page.items[0].name) == 48,
                "file scanner names stay fixed");
}

static void test_storage_settings_defaults_and_namespaces() {
    firefly::SystemSettings settings{};
    expect_true(settings.schema_version == 1,
                "settings schema version");
    expect_true(settings.volume == 50,
                "default volume");
    expect_true(settings.brightness == 128,
                "default brightness");
    expect_true(settings.auto_sleep_seconds == 30,
                "default sleep timeout");
    expect_true(settings.hide_notification_content,
                "notification content hidden by default");
    expect_true(settings.wrist_raise_enabled,
                "wrist raise enabled by default");
    expect_true(strcmp(settings.theme_id, "firefly-default") == 0,
                "default theme id");
    expect_true(strcmp(firefly::StorageService::kSystemNamespace,
                       "ff_sys") == 0 &&
                    strcmp(firefly::StorageService::kAlarmNamespace,
                           "ff_alarm") == 0 &&
                    strcmp(firefly::StorageService::kPairNamespace,
                           "ff_pair") == 0 &&
                    strcmp(firefly::StorageService::kStatsNamespace,
                           "ff_stats") == 0,
                "storage namespaces are fixed");
}

static void test_storage_legacy_migration_preserves_alarm_fields() {
    firefly::LegacyStorageSnapshot legacy{};
    legacy.has_volume = true;
    legacy.volume = 73;
    for(uint8_t slot = 0; slot < firefly::AlarmService::kSlots; ++slot) {
        legacy.has_alarm[slot] = true;
        legacy.alarms[slot].configured = true;
        legacy.alarms[slot].enabled = slot == 0;
        legacy.alarms[slot].hour = static_cast<uint8_t>(6 + slot);
        legacy.alarms[slot].minute = static_cast<uint8_t>(15 + slot);
        legacy.alarms[slot].days_mask = static_cast<uint8_t>(0x3E + slot);
        legacy.alarms[slot].ringtone = static_cast<uint8_t>(slot + 1);
        snprintf(legacy.alarms[slot].name,
                 sizeof(legacy.alarms[slot].name),
                 "Legacy %u", static_cast<unsigned>(slot));
    }

    firefly::SystemSettings settings{};
    firefly::Alarm migrated[firefly::AlarmService::kSlots]{};
    bool present[firefly::AlarmService::kSlots]{};
    firefly::StorageService::applyLegacySnapshot(
        legacy, settings, migrated, present);

    expect_true(settings.volume == 73,
                "legacy volume migrates");
    for(uint8_t slot = 0; slot < firefly::AlarmService::kSlots; ++slot) {
        expect_true(present[slot], "legacy alarm remains present");
        expect_true(migrated[slot].configured == legacy.alarms[slot].configured &&
                        migrated[slot].enabled == legacy.alarms[slot].enabled &&
                        migrated[slot].hour == legacy.alarms[slot].hour &&
                        migrated[slot].minute == legacy.alarms[slot].minute &&
                        migrated[slot].days_mask == legacy.alarms[slot].days_mask &&
                        migrated[slot].ringtone == legacy.alarms[slot].ringtone &&
                        strcmp(migrated[slot].name,
                               legacy.alarms[slot].name) == 0,
                    "legacy alarm fields migrate exactly");
    }
}

static void test_calendar_month_boundaries() {
    const firefly::CalendarMonth feb_2028 =
        firefly::CalendarModel::buildMonth(2028, 2, 15);
    expect_true(feb_2028.year == 2028 && feb_2028.month == 2,
                "calendar keeps requested month");
    expect_true(feb_2028.days_in_month == 29, "calendar handles leap february");
    expect_true(feb_2028.first_weekday == 2, "calendar maps Tuesday start");
    expect_true(feb_2028.cells[2].day == 1 && feb_2028.cells[2].in_current_month,
                "calendar places first day after sunday slots");

    const firefly::CalendarMonth sunday_start =
        firefly::CalendarModel::buildMonth(2026, 2, 1);
    expect_true(sunday_start.first_weekday == 0,
                "calendar uses sunday as first column");
    expect_true(sunday_start.cells[0].day == 1 && sunday_start.cells[0].today,
                "calendar marks today in sunday slot");

    const firefly::CalendarMonth next_year =
        firefly::CalendarModel::shiftMonth(2027, 12, 1);
    expect_true(next_year.year == 2028 && next_year.month == 1,
                "calendar rolls december to january");
    const firefly::CalendarMonth previous_year =
        firefly::CalendarModel::shiftMonth(2028, 1, -1);
    expect_true(previous_year.year == 2027 && previous_year.month == 12,
                "calendar rolls january to december");
}

static void test_calendar_agenda_truncates_to_eight() {
    firefly::CalendarAgendaCache agenda;
    firefly::CalendarSummary summaries[10]{};
    for(uint8_t i = 0; i < 10; ++i) {
        summaries[i].valid = true;
        summaries[i].start_epoch = 1800000000LL + i;
        snprintf(summaries[i].title, sizeof(summaries[i].title),
                 "Event %u", static_cast<unsigned>(i + 1U));
    }

    agenda.setSummaries(summaries, 10, 1800001234LL);
    expect_true(agenda.count() == firefly::CalendarAgendaCache::kMaxSummaries,
                "calendar agenda caps summaries");
    expect_true(strcmp(agenda.at(7).title, "Event 8") == 0,
                "calendar agenda preserves first eight summaries");
    expect_true(agenda.lastUpdatedEpoch() == 1800001234LL,
                "calendar agenda stores sync timestamp");
}

static void test_calculator_engine_basic_operations() {
    firefly::CalculatorEngine calculator;
    expect_true(calculator.setExpression("12.5+3.5"),
                "calculator accepts decimal expression");
    expect_true(calculator.evaluate(), "calculator evaluates addition");
    expect_true(strcmp(calculator.display(), "16") == 0,
                "calculator trims trailing zeroes");

    calculator.clear();
    expect_true(calculator.setExpression("-6*2"),
                "calculator accepts negative first operand");
    expect_true(calculator.evaluate(), "calculator evaluates multiplication");
    expect_true(strcmp(calculator.display(), "-12") == 0,
                "calculator shows negative result");

    calculator.clear();
    expect_true(calculator.setExpression("7/2"), "calculator accepts division");
    expect_true(calculator.evaluate(), "calculator evaluates division");
    expect_true(strcmp(calculator.display(), "3.5") == 0,
                "calculator keeps decimal result");
}

static void test_calculator_engine_limits_and_errors() {
    firefly::CalculatorEngine calculator;
    expect_true(!calculator.setExpression("1234567890123456789012345"),
                "calculator rejects expressions over 24 chars");
    expect_true(calculator.setExpression("4/0"), "calculator accepts divide expression");
    expect_true(!calculator.evaluate(), "calculator rejects divide by zero");
    expect_true(strcmp(calculator.display(), "Divide by zero") == 0,
                "calculator reports divide by zero");
    calculator.clear();
    expect_true(strcmp(calculator.display(), "0") == 0,
                "calculator clear resets display");

    expect_true(calculator.setExpression("1000000000*9"),
                "calculator accepts large visible result");
    expect_true(calculator.evaluate(), "calculator evaluates large result");
    expect_true(strlen(calculator.display()) <= firefly::CalculatorEngine::kMaxDisplayChars,
                "calculator display fits visible limit");
}

static void test_calculator_key_input_flow() {
    firefly::CalculatorEngine calculator;
    expect_true(calculator.inputKey("1"), "calculator accepts first key");
    expect_true(calculator.inputKey("2"), "calculator appends digit key");
    expect_true(calculator.inputKey("."), "calculator accepts decimal key");
    expect_true(calculator.inputKey("5"), "calculator appends decimal digit");
    expect_true(calculator.inputKey("+"), "calculator accepts operator key");
    expect_true(calculator.inputKey("3"), "calculator accepts rhs key");
    expect_true(strcmp(calculator.display(), "12.5+3") == 0,
                "calculator displays expression while editing");
    expect_true(calculator.inputKey("="), "calculator evaluates equals key");
    expect_true(strcmp(calculator.display(), "15.5") == 0,
                "calculator displays key-entered result");
    expect_true(calculator.inputKey("C"), "calculator clear key succeeds");
    expect_true(strcmp(calculator.display(), "0") == 0,
                "calculator clear key resets display");
}

static void test_tools_command_queue_is_fixed_fifo() {
    firefly::ToolsCommandQueue queue;
    for(uint8_t i = 0; i < firefly::ToolsCommandQueue::kCapacity; ++i) {
        expect_true(queue.post({firefly::ToolsCommandType::SetBrightness,
                                static_cast<uint8_t>(20U + i)}),
                    "tools queue accepts command within capacity");
    }
    expect_true(!queue.post({firefly::ToolsCommandType::SetBrightness, 255}),
                "tools queue rejects overflow");

    firefly::ToolsCommand command{};
    expect_true(queue.take(command), "tools queue returns first command");
    expect_true(command.type == firefly::ToolsCommandType::SetBrightness &&
                    command.value == 20,
                "tools queue preserves FIFO order");
    for(uint8_t i = 1; i < firefly::ToolsCommandQueue::kCapacity; ++i) {
        expect_true(queue.take(command), "tools queue drains queued command");
    }
    expect_true(!queue.take(command), "tools queue reports empty state");
}

static void test_flashlight_session_policy() {
    firefly::FlashlightSession flashlight;
    const firefly::FlashlightPowerState ok{50, 30, true};
    expect_true(flashlight.start(ok, 1000, 72), "flashlight starts when safe");
    expect_true(flashlight.active(60999), "flashlight remains active before limit");
    expect_true(!flashlight.active(61001), "flashlight stops after sixty seconds");
    expect_true(flashlight.originalBrightness() == 72,
                "flashlight remembers original brightness");

    flashlight.stop();
    const firefly::FlashlightPowerState low{14, 30, true};
    expect_true(!flashlight.start(low, 2000, 90),
                "flashlight rejects low battery");
    const firefly::FlashlightPowerState hot{80, 50, true};
    expect_true(!flashlight.start(hot, 2000, 90),
                "flashlight rejects unsafe temperature");

    expect_true(flashlight.start(ok, 3000, 128),
                "flashlight restarts after safe state");
    flashlight.closeFromUser();
    expect_true(!flashlight.active(3001), "flashlight closes from input");
}

static void test_flashlight_controller_posts_brightness_commands() {
    firefly::FlashlightController controller;
    const firefly::FlashlightPowerState safe{80, 28, true};
    expect_true(controller.start(safe, 1000, 96),
                "flashlight controller starts safe session");

    firefly::ToolsCommand command{};
    expect_true(controller.takeCommand(command),
                "flashlight start posts brightness command");
    expect_true(command.type == firefly::ToolsCommandType::SetBrightness &&
                    command.value == 255,
                "flashlight start requests maximum brightness");
    expect_true(!controller.tick(60999),
                "flashlight controller stays active before timeout");
    expect_true(controller.tick(61000),
                "flashlight controller closes at timeout");
    expect_true(controller.takeCommand(command),
                "flashlight timeout posts restore command");
    expect_true(command.value == 96,
                "flashlight restores original brightness");
    expect_true(!controller.stop(),
                "flashlight controller does not restore twice");
}

static void test_power_state_machine_timing() {
    firefly::PowerService power;
    power.configure({30000, 5000, 3000});
    power.onActivity(1000);
    expect_true(power.evaluateIdle(30999) == firefly::PowerMode::Active,
                "power remains active before idle timeout");
    expect_true(power.evaluateIdle(31001) == firefly::PowerMode::IdleDim,
                "power dims after idle timeout");
    expect_true(power.evaluateIdle(36001) == firefly::PowerMode::Glance,
                "power enters glance after dim interval");
    expect_true(power.evaluateIdle(39001) == firefly::PowerMode::ScreenOff,
                "power turns screen off after glance interval");

    power.onActivity(40000);
    expect_true(power.evaluateIdle(40001) == firefly::PowerMode::Active,
                "activity resets power timing");
}

static void test_power_battery_priority_and_thresholds() {
    firefly::PowerService power;
    power.configure({30000, 5000, 3000});
    power.onActivity(1000);

    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    battery.temperature_c = 50;
    battery.charging = true;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::ThermalProtection,
                "thermal protection overrides charging");

    battery.temperature_c = 30;
    battery.percent = 4;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::Charging,
                "safe charging overrides low battery modes");

    battery.charging = false;
    battery.vbus_present = false;
    battery.percent = 25;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::Saver,
                "power saver begins at twenty five percent");
    battery.percent = 15;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::LowBattery,
                "low battery begins at fifteen percent");
    battery.percent = 5;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::CriticalBattery,
                "critical battery begins at five percent");
}

static void test_debounced_button_short_and_long_press() {
    firefly::DebouncedButton button;
    expect_true(button.update(false, 0) == firefly::ButtonAction::None,
                "button starts released");
    expect_true(button.update(true, 10) == firefly::ButtonAction::None,
                "button press waits for debounce");
    expect_true(button.update(true, 40) == firefly::ButtonAction::None,
                "button accepts stable press without early action");
    expect_true(button.update(false, 50) == firefly::ButtonAction::None,
                "button release waits for debounce");
    expect_true(button.update(false, 80) == firefly::ButtonAction::ShortPress,
                "button emits short press on stable release");

    expect_true(button.update(true, 100) == firefly::ButtonAction::None,
                "second press begins");
    expect_true(button.update(true, 130) == firefly::ButtonAction::None,
                "second press debounces");
    expect_true(button.update(true, 1129) == firefly::ButtonAction::None,
                "long press waits for threshold");
    expect_true(button.update(true, 1130) == firefly::ButtonAction::LongPress,
                "long press fires at one second");
    expect_true(button.update(true, 1200) == firefly::ButtonAction::None,
                "long press fires only once");
    expect_true(button.update(false, 1210) == firefly::ButtonAction::None,
                "long press release waits for debounce");
    expect_true(button.update(false, 1240) == firefly::ButtonAction::None,
                "long press release does not emit short press");
}

static void test_debounced_button_rejects_jitter() {
    firefly::DebouncedButton button;
    expect_true(button.update(false, 0) == firefly::ButtonAction::None,
                "jitter test starts released");
    expect_true(button.update(true, 10) == firefly::ButtonAction::None,
                "jitter press begins");
    expect_true(button.update(false, 29) == firefly::ButtonAction::None,
                "sub debounce jitter release is ignored");
    expect_true(button.update(false, 60) == firefly::ButtonAction::None,
                "jitter never becomes stable press");
}

static uint8_t sleep_prepare_calls = 0;
static uint8_t sleep_restore_calls = 0;

static bool fake_sleep_prepare() {
    ++sleep_prepare_calls;
    return true;
}

static void fake_sleep_restore() {
    ++sleep_restore_calls;
}

static void test_light_sleep_requires_verified_wake_matrix() {
    firefly::PowerService power;
    power.setSleepHooks({fake_sleep_prepare, fake_sleep_restore});
    expect_true(!power.canEnterLightSleep(),
                "light sleep is disabled without wake verification");
    expect_true(!power.prepareForLightSleep(),
                "sleep prepare hook is gated before verification");

    power.recordWakeVerification(firefly::WakeSource::Boot, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::PowerButton, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::RtcAlarm, 100, 99);
    expect_true(!power.canEnterLightSleep(),
                "one failed wake attempt keeps light sleep disabled");

    power.recordWakeVerification(firefly::WakeSource::RtcAlarm, 100, 100);
    expect_true(power.canEnterLightSleep(),
                "required wake sources enable gate only at one hundred percent");
    expect_true(power.prepareForLightSleep(),
                "verified sleep runs prepare hook");
    power.restoreFromLightSleep();
    expect_true(sleep_prepare_calls == 1 && sleep_restore_calls == 1,
                "sleep hooks run once around verified lifecycle");
}

static bool fake_sleep_enter_result = true;
static uint8_t fake_sleep_enter_calls = 0;

static bool fake_sleep_enter() {
    ++fake_sleep_enter_calls;
    return fake_sleep_enter_result;
}

static void test_light_sleep_attempt_restores_on_success_and_failure() {
    sleep_prepare_calls = 0;
    sleep_restore_calls = 0;
    fake_sleep_enter_calls = 0;
    firefly::PowerService power;
    power.setSleepHooks({fake_sleep_prepare, fake_sleep_restore});
    expect_true(power.attemptLightSleep(fake_sleep_enter) ==
                    firefly::SleepAttemptResult::GateClosed,
                "sleep attempt rejects unverified wake matrix");
    expect_true(fake_sleep_enter_calls == 0,
                "closed sleep gate never enters platform sleep");

    power.recordWakeVerification(firefly::WakeSource::Boot, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::PowerButton, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::RtcAlarm, 100, 100);
    fake_sleep_enter_result = false;
    expect_true(power.attemptLightSleep(fake_sleep_enter) ==
                    firefly::SleepAttemptResult::EnterFailed,
                "failed platform entry is reported");
    expect_true(sleep_prepare_calls == 1 && sleep_restore_calls == 1,
                "failed entry restores prepared resources");

    fake_sleep_enter_result = true;
    expect_true(power.attemptLightSleep(fake_sleep_enter) ==
                    firefly::SleepAttemptResult::Entered,
                "verified platform sleep completes lifecycle");
    expect_true(sleep_prepare_calls == 2 && sleep_restore_calls == 2,
                "successful entry restores prepared resources");
}

class FakeMotionDevice : public firefly::MotionDevice {
public:
    bool begin() override {
        began = true;
        return begin_result;
    }

    bool setLowPower(bool enabled) override {
        low_power = enabled;
        return low_power_result;
    }

    firefly::MotionSample read() override {
        return next_sample;
    }

    firefly::MotionSample next_sample{};
    bool begin_result = true;
    bool low_power_result = true;
    bool began = false;
    bool low_power = false;
};

static void test_motion_service_fixed_sample_buffer() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    expect_true(motion.begin() && device.began,
                "motion service begins device");

    device.next_sample.valid = false;
    expect_true(!motion.poll(), "motion service rejects invalid sample");
    expect_true(motion.sampleCount() == 0,
                "invalid motion sample does not enter buffer");

    for(uint32_t i = 0; i < 40; ++i) {
        device.next_sample = {};
        device.next_sample.valid = true;
        device.next_sample.ax = static_cast<float>(i);
        device.next_sample.timestamp_ms = i;
        expect_true(motion.poll(), "motion service accepts valid sample");
    }
    expect_true(motion.sampleCount() == firefly::MotionService::kSampleCapacity,
                "motion sample buffer has fixed capacity");
    expect_true(motion.sampleAt(0).timestamp_ms == 8,
                "motion ring buffer evicts oldest sample");
    expect_true(motion.latest().timestamp_ms == 39,
                "motion ring buffer retains latest sample");
}

static void test_motion_service_low_power_forwarding() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    expect_true(motion.setLowPower(true),
                "motion service enters low power mode");
    expect_true(device.low_power,
                "motion service forwards low power request");
}

static void test_motion_power_policy_keeps_gyro_for_screen_off_wrist_raise() {
    expect_true(firefly::MotionPowerPolicy::modeFor(false, false) ==
                    firefly::MotionPowerMode::Normal,
                "active mode keeps normal sampling");
    expect_true(firefly::MotionPowerPolicy::modeFor(true, false) ==
                    firefly::MotionPowerMode::Normal,
                "screen off cpu mode keeps gyro for wrist raise");
    expect_true(firefly::MotionPowerPolicy::modeFor(true, true) ==
                    firefly::MotionPowerMode::LowPower,
                "light sleep preparation uses low power sensor mode");
}

static void test_motion_service_bounded_diagnostics() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    firefly::MotionContext context{};

    firefly::MotionSample invalid{};
    expect_true(!motion.processSample(invalid, context),
                "invalid sample is rejected");

    firefly::MotionSample lowered{};
    lowered.valid = true;
    lowered.az = -0.2f;
    lowered.timestamp_ms = 1000;
    motion.processSample(lowered, context);

    firefly::MotionSample raised{};
    raised.valid = true;
    raised.ay = -0.5f;
    raised.az = 0.8f;
    raised.gx = 60.0f;
    raised.timestamp_ms = 1200;
    motion.processSample(raised, context);

    const firefly::MotionDiagnostics diagnostics = motion.diagnostics();
    expect_true(diagnostics.valid_samples == 2,
                "motion diagnostics count valid samples");
    expect_true(diagnostics.invalid_samples == 1,
                "motion diagnostics count invalid samples");
    expect_true(diagnostics.wrist_events == 1,
                "motion diagnostics count wrist events");
    expect_true(diagnostics.steps == motion.summary().steps,
                "motion diagnostics snapshot current step total");
}

static void feed_step_cycle(firefly::StepDetector & detector,
                            uint32_t & timestamp_ms,
                            uint8_t peak_samples,
                            uint8_t baseline_samples) {
    firefly::MotionSample sample{};
    sample.valid = true;
    for(uint8_t i = 0; i < peak_samples; ++i) {
        sample.az = 1.6f;
        sample.timestamp_ms = timestamp_ms;
        detector.update(sample);
        timestamp_ms += 10;
    }
    for(uint8_t i = 0; i < baseline_samples; ++i) {
        sample.az = 1.0f;
        sample.timestamp_ms = timestamp_ms;
        detector.update(sample);
        timestamp_ms += 10;
    }
}

static void test_step_detector_static_and_regular_cadence() {
    firefly::StepDetector detector;
    firefly::MotionSample sample{};
    sample.valid = true;
    sample.az = 1.0f;
    for(uint32_t i = 0; i < 1000; ++i) {
        sample.timestamp_ms = i * 10U;
        detector.update(sample);
    }
    expect_true(detector.totalSteps() == 0,
                "static acceleration produces zero steps");

    uint32_t timestamp_ms = 10000;
    for(uint8_t step = 0; step < 20; ++step) {
        feed_step_cycle(detector, timestamp_ms, 5, 45);
    }
    expect_true(detector.totalSteps() >= 18 && detector.totalSteps() <= 22,
                "regular cadence counts twenty steps within tolerance");
}

static void test_step_detector_limits_high_frequency_jitter() {
    firefly::StepDetector detector;
    uint32_t timestamp_ms = 0;
    for(uint8_t pulse = 0; pulse < 30; ++pulse) {
        feed_step_cycle(detector, timestamp_ms, 2, 8);
    }
    expect_true(detector.totalSteps() <= 12,
                "minimum step interval limits high frequency jitter");
}

static void test_wrist_raise_requires_posture_rotation_and_cooldown() {
    firefly::WristRaiseDetector detector;
    firefly::MotionContext context{};
    firefly::MotionSample lowered{};
    lowered.valid = true;
    lowered.az = -0.2f;
    lowered.timestamp_ms = 1000;
    expect_true(!detector.update(lowered, context),
                "lowered wrist establishes baseline");

    firefly::MotionSample raised{};
    raised.valid = true;
    raised.ay = -0.5f;
    raised.az = 0.8f;
    raised.gx = 60.0f;
    raised.timestamp_ms = 1200;
    expect_true(detector.update(raised, context),
                "posture change plus angular velocity triggers wrist raise");
    raised.timestamp_ms = 2000;
    expect_true(!detector.update(raised, context),
                "wrist raise respects three second cooldown");

    context.screen_on = true;
    lowered.timestamp_ms = 5000;
    detector.update(lowered, context);
    raised.timestamp_ms = 5200;
    expect_true(!detector.update(raised, context),
                "wrist raise is suppressed while screen is on");
}

static void test_motion_service_aggregates_steps_and_wrist_event() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    motion.setDayKey(2026183);
    firefly::MotionContext context{};
    firefly::MotionSample sample{};
    sample.valid = true;

    uint32_t timestamp_ms = 0;
    for(uint8_t step = 0; step < 20; ++step) {
        for(uint8_t i = 0; i < 5; ++i) {
            sample.az = 1.6f;
            sample.timestamp_ms = timestamp_ms;
            motion.processSample(sample, context);
            timestamp_ms += 10;
        }
        for(uint8_t i = 0; i < 45; ++i) {
            sample.az = 1.0f;
            sample.timestamp_ms = timestamp_ms;
            motion.processSample(sample, context);
            timestamp_ms += 10;
        }
    }
    const firefly::MotionSummary summary = motion.summary();
    expect_true(summary.steps >= 18 && summary.steps <= 22,
                "motion service aggregates detected steps");
    expect_true(summary.active_minutes == 1,
                "motion service counts unique active minute");

    sample = {};
    sample.valid = true;
    sample.az = -0.2f;
    sample.timestamp_ms = 20000;
    motion.processSample(sample, context);
    sample.ay = -0.5f;
    sample.az = 0.8f;
    sample.gx = 60.0f;
    sample.timestamp_ms = 20200;
    motion.processSample(sample, context);
    expect_true(motion.consumeWristRaise(),
                "motion service publishes wrist raise once");
    expect_true(!motion.consumeWristRaise(),
                "motion service wrist event is consumed");

    motion.setDayKey(2026184);
    expect_true(motion.summary().steps == 0,
                "motion service resets daily aggregate on day change");
}

static void test_motion_service_restores_same_day_aggregate() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    motion.restoreDailySummary(2026183, 4321, 27);

    const firefly::MotionSummary restored = motion.summary();
    expect_true(restored.steps == 4321,
                "motion service restores persisted step total");
    expect_true(restored.active_minutes == 27,
                "motion service restores persisted active minutes");

    motion.setDayKey(2026183);
    expect_true(motion.summary().steps == 4321,
                "same day key preserves restored aggregate");
    motion.setDayKey(2026184);
    expect_true(motion.summary().steps == 0 &&
                    motion.summary().active_minutes == 0,
                "new day clears restored aggregate");
}

void setup() {
    Serial.begin(115200);
    delay(200);
    expect_true(FIREFLYOS_VERSION_MAJOR == 0, "version major");
    expect_true(FIREFLYOS_VERSION_MINOR == 1, "version minor");
    test_event_bus_fifo();
    test_event_bus_full_policy();
    test_event_bus_preserves_full_critical_queue();
    test_state_store_revision();
    test_capability_registry();
    test_sd_paths_stay_inside_managed_root();
    test_sd_removal_requires_two_consecutive_failures();
    test_theme_manifest_validation();
    test_audio_session_priority();
    test_pcm_wav_header_round_trip();
    test_alarm_ringtone_resources();
    test_media_app_fixed_boundaries();
    test_file_scan_page_is_fixed_and_bounded();
    test_app_registry();
    test_lifecycle_and_resource_governor();
    test_app_manager_publishes_requests();
    test_hardware_abstraction();
    test_default_theme_tokens();
    test_navigation_stack();
    test_overlay_priority_policy();
    test_alarm_next_trigger();
    test_alarm_service_publishes_trigger_event_once();
    test_time_service_invalid_rtc();
    test_time_service_reload_set_and_tick();
    test_countdown_timer_uses_target_time();
    test_countdown_pause_resume_and_one_shot_expiry();
    test_stopwatch_uses_monotonic_time();
    test_stopwatch_survives_page_visibility_changes();
    test_settings_app_command_queue();
    test_settings_commands_preserve_time_and_alarm_payloads();
    test_storage_settings_defaults_and_namespaces();
    test_storage_legacy_migration_preserves_alarm_fields();
    test_calendar_month_boundaries();
    test_calendar_agenda_truncates_to_eight();
    test_calculator_engine_basic_operations();
    test_calculator_engine_limits_and_errors();
    test_calculator_key_input_flow();
    test_tools_command_queue_is_fixed_fifo();
    test_flashlight_session_policy();
    test_flashlight_controller_posts_brightness_commands();
    test_power_state_machine_timing();
    test_power_battery_priority_and_thresholds();
    test_debounced_button_short_and_long_press();
    test_debounced_button_rejects_jitter();
    test_light_sleep_requires_verified_wake_matrix();
    test_light_sleep_attempt_restores_on_success_and_failure();
    test_motion_service_fixed_sample_buffer();
    test_motion_service_low_power_forwarding();
    test_motion_power_policy_keeps_gyro_for_screen_off_wrist_raise();
    test_motion_service_bounded_diagnostics();
    test_step_detector_static_and_regular_cadence();
    test_step_detector_limits_high_frequency_jitter();
    test_wrist_raise_requires_posture_rotation_and_cooldown();
    test_motion_service_aggregates_steps_and_wrist_event();
    test_motion_service_restores_same_day_aggregate();
    Serial.printf("FIREFLY_TEST_RESULT failures=%u\n", failures);
}

void loop() {
    delay(1000);
}
