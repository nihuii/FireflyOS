import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LIB = ROOT / "libraries" / "FireflyOS" / "src" / "firefly"


class ConnectivityServiceContractTests(unittest.TestCase):
    def test_ble_hal_exposes_required_peripheral_operations(self):
        header = (LIB / "hal" / "BlePeripheralDevice.h").read_text(encoding="utf-8")
        source = (LIB / "hal" / "BlePeripheralDevice.cpp").read_text(encoding="utf-8")
        for token in (
            "ReceiveCallback",
            "bool begin(",
            "void advertise(",
            "void stopAdvertising(",
            "bool notify(",
            "bool connected() const",
            "uint16_t negotiatedMtu() const",
            "void disconnect()",
            "kCommandRxUuid",
            "kEventTxUuid",
            "kBulkControlUuid",
        ):
            self.assertIn(token, header + source)
        self.assertIn("BLEDevice::createServer", source)
        self.assertNotIn("lv_", header + source)

    def test_connectivity_is_bounded_event_driven_and_retry_limited(self):
        header = (LIB / "services" / "ConnectivityService.h").read_text(encoding="utf-8")
        source = (LIB / "services" / "ConnectivityService.cpp").read_text(encoding="utf-8")
        combined = header + source
        for token in (
            "kAckTimeoutMs = 2000",
            "kMaxRetries = 3",
            "kUnpairedFastAdvertisingMs = 60000",
            "kPairedFastAdvertisingMs = 20000",
            "kConnectionIdleMs = 30000",
            "FrameCodec::decode",
            "EventBus",
            "BleMessageReceived",
            "state_.setPhoneConnected(is_connected)",
            "uint8_t reassembly_payload_[protocol::kMaxPayload]",
        ):
            self.assertIn(token, combined)
        for forbidden in ("std::vector", "std::queue", "new ", "lv_"):
            self.assertNotIn(forbidden, combined)

    def test_outbound_frames_fragment_to_negotiated_mtu_and_retry_as_batch(self):
        header = (LIB / "services" / "ConnectivityService.h").read_text(
            encoding="utf-8"
        )
        source = (LIB / "services" / "ConnectivityService.cpp").read_text(
            encoding="utf-8"
        )
        combined = header + source
        for token in (
            "notifyOutboundFrame",
            "pending_ack_frame_",
            "protocol::attChunkLimit(transport_.negotiatedMtu())",
        ):
            self.assertIn(token, combined)
        self.assertNotIn("pending_ack_chunks_", combined)
        retry_start = source.index("void ConnectivityService::updateAckRetry")
        retry_end = source.index(
            "void ConnectivityService::processEncoded", retry_start
        )
        self.assertIn(
            "notifyOutboundFrame",
            source[retry_start:retry_end],
        )

    def test_disconnect_only_changes_phone_connectivity(self):
        store_header = (LIB / "core" / "StateStore.h").read_text(encoding="utf-8")
        store_source = (LIB / "core" / "StateStore.cpp").read_text(encoding="utf-8")
        service = (LIB / "services" / "ConnectivityService.cpp").read_text(encoding="utf-8")
        self.assertIn("setPhoneConnected", store_header + store_source)
        self.assertNotIn("AlarmService", service)
        self.assertNotIn("AudioService", service)
        self.assertNotIn("NotificationCenter", service)

    def test_formal_firmware_starts_and_services_ble_without_touching_lvgl(self):
        app_header = (ROOT / "Firefly" / "FireflyApp.h").read_text(encoding="utf-8")
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(encoding="utf-8")
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(encoding="utf-8")
        combined = app_header + interaction + sketch
        for token in (
            "BlePeripheralDevice ble_peripheral_device",
            "ConnectivityService connectivity_service",
            'connectivity_service.begin("FireflyOS", false',
            "connectivity_service.service(now)",
            "connectivity_service.takeReceivedFrame",
            "Capability::Ble",
        ):
            self.assertIn(token, combined)


if __name__ == "__main__":
    unittest.main()
