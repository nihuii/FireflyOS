#include <Arduino.h>
#include <FireflyOS.h>
#include <firefly/apps/clock/ClockApp.h>
#include <firefly/apps/settings/SettingsApp.h>
#include <firefly/services/AlarmService.h>
#include <firefly/services/TimeService.h>

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

    uint8_t brightness = 0;
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
    test_app_registry();
    test_lifecycle_and_resource_governor();
    test_app_manager_publishes_requests();
    test_hardware_abstraction();
    test_default_theme_tokens();
    test_navigation_stack();
    test_overlay_priority_policy();
    test_alarm_next_trigger();
    test_time_service_invalid_rtc();
    test_time_service_reload_set_and_tick();
    test_countdown_timer_uses_target_time();
    test_stopwatch_uses_monotonic_time();
    test_settings_app_command_queue();
    Serial.printf("FIREFLY_TEST_RESULT failures=%u\n", failures);
}

void loop() {
    delay(1000);
}
