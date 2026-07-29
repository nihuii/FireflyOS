import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ANDROID_SRC = ROOT / "AndroidCompanion" / "app" / "src" / "main" / "java" / "com" / "fireflyos" / "companion"


def read_required(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing Android BLE artifact: {path}")
    return path.read_text(encoding="utf-8")


class AndroidBleClientContractTests(unittest.TestCase):
    def test_frame_codec_is_bounded_and_uses_little_endian(self):
        text = read_required(ANDROID_SRC / "ble" / "FrameCodec.kt")
        for token in (
            "object FrameCodec",
            "ByteBuffer",
            "ByteOrder.LITTLE_ENDIAN",
            "MAX_PAYLOAD = 1024",
            "sealed interface DecodeResult",
            "DecodeError.CrcMismatch",
            "fun reassemble",
        ):
            self.assertIn(token, text)
        self.assertNotIn("throw", text)

    def test_gatt_client_scans_for_firefly_service_once_and_stops(self):
        text = read_required(ANDROID_SRC / "ble" / "FireflyGattClient.kt")
        for token in (
            "SERVICE_UUID",
            "7b7f0001-4f53-4653-8000-ff1e00000001",
            "SCAN_TIMEOUT_MS = 15_000L",
            "ScanFilter.Builder()",
            "setServiceUuid(ParcelUuid(FireflyProtocol.SERVICE_UUID))",
            "stopScan",
            "connectGatt(context, false, gattCallback)",
        ):
            self.assertIn(token, text)
        self.assertNotIn("while (true)", text)
        self.assertEqual(1, text.count("startScan(filters, settings, scanCallback)"))

    def test_gatt_writes_are_serialized_and_stateflow_is_exposed(self):
        gatt = read_required(ANDROID_SRC / "ble" / "FireflyGattClient.kt")
        repo = read_required(ANDROID_SRC / "ble" / "ConnectionRepository.kt")
        state = read_required(ANDROID_SRC / "data" / "DeviceState.kt")

        for token in (
            "FixedGattBatchQueue<GattOperation>",
            "MAX_GATT_OPERATIONS",
            "writeCommandBatch",
            "enqueueOperation",
            "drainOperationQueue",
            "onCharacteristicWrite",
            "onDescriptorWrite",
        ):
            self.assertIn(token, gatt)
        self.assertNotIn("ArrayDeque<GattOperation>", gatt)
        self.assertNotIn("operationInFlight", gatt)
        for token in (
            "MutableStateFlow",
            "StateFlow<DeviceState>",
            "scanAndConnect",
            "sendFrame",
        ):
            self.assertIn(token, repo)
        self.assertIn("data class DeviceState", state)
        self.assertIn("enum class ConnectionStatus", state)

    def test_gatt_callbacks_stream_and_cccd_are_main_looper_ordered(self):
        gatt = read_required(ANDROID_SRC / "ble" / "FireflyGattClient.kt")
        codec = read_required(ANDROID_SRC / "ble" / "FrameCodec.kt")
        setup = read_required(
            ANDROID_SRC / "ble" / "GattNotificationSetupStateMachine.kt"
        )
        for token in (
            "FrameStreamReassembler",
            "StreamFrameResult.Complete",
            "streamReassembler.reset()",
            "MAX_FRAGMENTS",
            "handler.post",
            "runOnMainSynchronously",
            "MAX_GATT_OPERATIONS",
            "GattNotificationSetupStateMachine",
            "setCharacteristicNotification",
            "onDescriptorWrite",
            "AwaitingCccd",
        ):
            self.assertIn(token, gatt + codec + setup)
        service_start = gatt.index("private fun handleServicesDiscovered")
        service_end = gatt.index("private fun publishConnected", service_start)
        self.assertNotIn(
            "ConnectionStatus.Connected",
            gatt[service_start:service_end],
        )

    def test_gatt_negotiates_mtu_and_business_sender_uses_live_limit(self):
        gatt = read_required(ANDROID_SRC / "ble" / "FireflyGattClient.kt")
        sender = read_required(
            ANDROID_SRC / "ble" / "AuthenticatedBusinessSender.kt"
        )
        repository = read_required(
            ANDROID_SRC / "ble" / "ConnectionRepository.kt"
        )
        for token in (
            "requestMtu(GattMtuPolicy.DESIRED_MTU)",
            "override fun onMtuChanged",
            "GattMtuPolicy.payloadLimit",
            "fun maxAttWriteBytes",
        ):
            self.assertIn(token, gatt)
        self.assertIn("maxAttBytes: () -> Int", sender)
        self.assertIn("FrameCodec.fragment(authenticated, maxAttBytes())", sender)
        self.assertIn("maxAttBytes = client::maxAttWriteBytes", repository)

    def test_repository_uses_authenticated_session_replay_gate(self):
        repository = read_required(
            ANDROID_SRC / "ble" / "ConnectionRepository.kt"
        )
        gate = read_required(ANDROID_SRC / "ble" / "InboundFrameGate.kt")
        combined = repository + gate
        for token in (
            "InboundFrameGate",
            "FrameAuthenticator.verify",
            "InboundSequenceTracker",
            "InboundFrameDisposition.Duplicate",
            "inboundFrameGate.reset()",
            "FixedBusinessFrameSink",
            "MAX_RECEIVED_BUSINESS_FRAMES",
        ):
            self.assertIn(token, combined)
        self.assertLess(
            gate.index("FrameAuthenticator.verify"),
            gate.index("sequenceTracker.isFresh"),
        )

    def test_repository_waits_for_secure_hello_before_business_replay(self):
        repository = read_required(
            ANDROID_SRC / "ble" / "ConnectionRepository.kt"
        )
        connected_start = repository.index("if (connected) {")
        connected_end = repository.index("} else if", connected_start)
        connected_block = repository[connected_start:connected_end]
        for token in (
            "queueHello",
            "secureSessionReady",
            "pendingSettingsReplay",
            "MessageType.Hello",
        ):
            self.assertIn(token, repository)
        self.assertNotIn("queueSettingsGet()", connected_block)

    def test_scan_sessions_cleanup_and_atomic_batches_are_production_wired(self):
        gatt = read_required(ANDROID_SRC / "ble" / "FireflyGattClient.kt")
        sender = read_required(
            ANDROID_SRC / "ble" / "AuthenticatedBusinessSender.kt"
        )
        reliable = read_required(
            ANDROID_SRC / "ble" / "ReliableFrameSender.kt"
        )
        session = read_required(ANDROID_SRC / "ble" / "GattSessionState.kt")
        combined = gatt + sender + reliable + session
        for token in (
            "ScanSessionTracker",
            "scanGeneration",
            "claimResult",
            "gatt.close()",
            "failCurrentGatt",
            "CancellableMainAction",
            "writeCommandBatch",
            "FixedGattBatchQueue",
            "enqueueBatch",
            "completeCurrent",
            "onComplete",
            "WriteRejected",
        ):
            self.assertIn(token, combined)
        self.assertNotIn("encoded.all(writeEncoded)", sender)

    def test_gatt_current_operation_has_timeout_recovery(self):
        gatt = read_required(ANDROID_SRC / "ble" / "FireflyGattClient.kt")
        queue = read_required(ANDROID_SRC / "ble" / "GattSessionState.kt")
        combined = gatt + queue
        for token in (
            "GATT_OPERATION_TIMEOUT_MS = 2_000L",
            "operationTimeout",
            "timeoutCurrent",
            "handler.postDelayed(operationTimeout",
            "handler.removeCallbacks(operationTimeout",
        ):
            self.assertIn(token, combined)


if __name__ == "__main__":
    unittest.main()
