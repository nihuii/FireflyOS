#include <Arduino.h>
#include <FireflyOS.h>

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
    Serial.printf("FIREFLY_TEST_RESULT failures=%u\n", failures);
}

void loop() {
    delay(1000);
}
