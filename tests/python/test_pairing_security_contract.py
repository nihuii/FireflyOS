import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LIB = ROOT / "libraries" / "FireflyOS" / "src" / "firefly"


class PairingSecurityContractTests(unittest.TestCase):
    def test_pairing_record_is_fixed_and_owned_by_ff_pair(self):
        header = (LIB / "services" / "StorageService.h").read_text(encoding="utf-8")
        source = (LIB / "services" / "StorageService.cpp").read_text(encoding="utf-8")
        combined = header + source
        for token in (
            "struct PairingRecord",
            "uint8_t app_token[16]",
            "char phone_name[33]",
            "bool confirmed = false",
            "class PairingStore",
            "loadPairing",
            "savePairing",
            "clearPairing",
            'kPairNamespace = "ff_pair"',
            '"token"',
            '"phone"',
            '"phase"',
        ):
            self.assertIn(token, combined)

    def test_pairing_record_is_provisional_until_pair_confirm_ack(self):
        source = (LIB / "services" / "ConnectivityService.cpp").read_text(
            encoding="utf-8"
        )
        for token in (
            "stored.valid && !stored.confirmed",
            "record.confirmed = false",
            "confirmed_record.confirmed = true",
            "pairing_store_.savePairing(confirmed_record)",
        ):
            self.assertIn(token, source)

    def test_sensitive_frames_use_truncated_sha256_and_five_strikes(self):
        header = (LIB / "services" / "ConnectivityService.h").read_text(encoding="utf-8")
        source = (LIB / "services" / "ConnectivityService.cpp").read_text(encoding="utf-8")
        combined = header + source
        for token in (
            "kAppTokenSize = 16",
            "kAuthTagSize = 8",
            "kMaxAuthenticationFailures = 5",
            "class MessageAuthenticator",
            "appendTag",
            "verifyAndStrip",
            "mbedtls_sha256_starts_ret",
            "mbedtls_sha256_update_ret",
            "authentication_failures_",
            "transport_.disconnect()",
        ):
            self.assertIn(token, combined)
        self.assertNotIn("memcmp(expected", source)

    def test_ble_security_is_sc_mitm_bonded_and_can_be_cleared(self):
        header = (LIB / "hal" / "BlePeripheralDevice.h").read_text(encoding="utf-8")
        source = (LIB / "hal" / "BlePeripheralDevice.cpp").read_text(encoding="utf-8")
        combined = header + source
        for token in (
            "SecurityCallback",
            "requestSecureBond",
            "encrypted() const",
            "clearBonds",
            "ESP_LE_AUTH_REQ_SC_MITM_BOND",
            "esp_ble_set_encryption",
            "esp_ble_remove_bond_device",
        ):
            self.assertIn(token, combined)

    def test_ble_tx_characteristic_is_notify_only_to_avoid_token_reads(self):
        source = (LIB / "hal" / "BlePeripheralDevice.cpp").read_text(encoding="utf-8")
        tx_start = source.index("protocol::kEventTxUuid")
        tx_end = source.index(");", tx_start)
        tx_block = source[tx_start:tx_end]
        self.assertIn("BLECharacteristic::PROPERTY_NOTIFY", tx_block)
        self.assertNotIn("BLECharacteristic::PROPERTY_READ", tx_block)

    def test_clear_bonds_drains_all_bond_records(self):
        source = (LIB / "hal" / "BlePeripheralDevice.cpp").read_text(encoding="utf-8")
        clear_start = source.index("bool BlePeripheralDevice::clearBonds")
        clear_end = source.index("void BlePeripheralDevice::handleConnected", clear_start)
        clear_block = source[clear_start:clear_end]
        self.assertIn("while", clear_block)
        self.assertIn("esp_ble_get_bond_device_num", clear_block)
        self.assertNotIn("if(count > 4) count = 4", clear_block)

    def test_unpair_keeps_token_if_bonds_cannot_be_cleared(self):
        source = (LIB / "services" / "ConnectivityService.cpp").read_text(
            encoding="utf-8"
        )
        unpair_start = source.index("bool ConnectivityService::finalizeUnpair")
        unpair_end = source.index(
            "void ConnectivityService::postPairingEvent", unpair_start
        )
        unpair_block = source[unpair_start:unpair_end]
        self.assertLess(
            unpair_block.index("transport_.clearBonds()"),
            unpair_block.index("pairing_store_.clearPairing()"),
        )
        bonds_failure = unpair_block[
            unpair_block.index("if(!bonds_cleared)"):
            unpair_block.index("if(!pairing_store_.clearPairing()")
        ]
        self.assertNotIn("clearPairing", bonds_failure)

    def test_ack_frames_are_strict_type_flag_and_empty_payload(self):
        header = (LIB / "services" / "ConnectivityService.h").read_text(
            encoding="utf-8"
        )
        source = (LIB / "services" / "ConnectivityService.cpp").read_text(
            encoding="utf-8"
        )
        combined = header + source
        for token in (
            "isStrictAckFrame",
            "isMalformedAckFrame",
            "frame.type == protocol::MessageType::Ack",
            "frame.flags == protocol::FrameFlag::IsAck",
            "frame.payload_length == 0",
        ):
            self.assertIn(token, combined)
        complete_start = source.index("void ConnectivityService::processCompleteFrame")
        complete_end = source.index("bool ConnectivityService::processFragment", complete_start)
        complete_block = source[complete_start:complete_end]
        self.assertNotIn(
            "frame.type == protocol::MessageType::Ack ||",
            complete_block,
        )

    def test_pairing_and_unpairing_are_confirmed_on_ui_event_boundary(self):
        overlay_header = (LIB / "ui" / "screens" / "SystemOverlayHost.h").read_text(encoding="utf-8")
        overlay_source = (LIB / "ui" / "screens" / "SystemOverlayHost.cpp").read_text(encoding="utf-8")
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(encoding="utf-8")
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(encoding="utf-8")
        combined = overlay_header + overlay_source + interaction + sketch
        for token in (
            "class PairingOverlay",
            "PairingRequested",
            "PairingResult",
            "UnpairConfirmationRequested",
            "PairingUnbound",
            "confirmPairing",
            "confirmUnpair",
            "notification_center.clear()",
        ):
            self.assertIn(token, combined)
        self.assertIn("52", overlay_source)

    def test_pairing_and_unpair_commit_only_after_confirmation_ack(self):
        header = (LIB / "services" / "ConnectivityService.h").read_text(
            encoding="utf-8"
        )
        source = (LIB / "services" / "ConnectivityService.cpp").read_text(
            encoding="utf-8"
        )
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8"
        )
        combined = header + source
        for token in (
            "AwaitingPairConfirmAck",
            "AwaitingUnpairAck",
            "PendingAckPurpose",
            "finalizePairing",
            "rollbackPendingPairing",
            "finalizeUnpair",
            "handleAcknowledgement",
        ):
            self.assertIn(token, combined)
        security_start = source.index(
            "void ConnectivityService::handleSecurityResult"
        )
        security_end = source.index(
            "void ConnectivityService::finalizePairing", security_start
        )
        security_block = source[security_start:security_end]
        self.assertNotIn("paired_ = true", security_block)
        self.assertIn("AwaitingPairConfirmAck", security_block)
        event_start = interaction.index(
            "case firefly::EventType::PairingUnbound"
        )
        event_end = interaction.index(
            "case firefly::EventType::SdRemoved", event_start
        )
        unpair_event = interaction[event_start:event_end]
        self.assertLess(
            unpair_event.index("event.value == 1"),
            unpair_event.index("notification_service.clearLocal()"),
        )

    def test_android_exposes_authenticated_unpair_without_early_token_clear(self):
        android = (
            ROOT
            / "AndroidCompanion"
            / "app"
            / "src"
            / "main"
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
        )
        coordinator = (android / "ble" / "PairingProtocolCoordinator.kt").read_text(
            encoding="utf-8"
        )
        repository = (android / "ble" / "ConnectionRepository.kt").read_text(
            encoding="utf-8"
        )
        activity = (android / "MainActivity.kt").read_text(encoding="utf-8")
        layout = (
            ROOT
            / "AndroidCompanion"
            / "app"
            / "src"
            / "main"
            / "res"
            / "layout"
            / "activity_main.xml"
        ).read_text(encoding="utf-8")
        self.assertNotIn("fun beginRepairPairing() {\n        tokenStore.clearToken()", coordinator)
        for token in (
            "sessionToken",
            "retireToken",
            "hasRetiringToken",
            "MessageType.UnpairRequest",
            "unpaired",
            "accepted",
        ):
            self.assertIn(token, coordinator)
        self.assertIn("fun requestUnpair()", repository)
        self.assertIn("requestUnpairButton", activity + layout)

    def test_android_unpair_token_is_durable_across_process_restart(self):
        authenticator = (
            ROOT
            / "AndroidCompanion"
            / "app"
            / "src"
            / "main"
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "ble"
            / "FrameAuthenticator.kt"
        ).read_text(encoding="utf-8")
        for token in (
            "fun retireToken(): Boolean",
            "fun hasRetiringToken(): Boolean",
            'RETIRING_TOKEN_KEY = "retiring_app_token"',
            ".putString(RETIRING_TOKEN_KEY",
            ".remove(TOKEN_KEY)",
            ".commit()",
        ):
            self.assertIn(token, authenticator)

    def test_secrets_are_not_logged(self):
        sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (
                LIB / "services" / "ConnectivityService.cpp",
                LIB / "services" / "StorageService.cpp",
                LIB / "hal" / "BlePeripheralDevice.cpp",
            )
        )
        for forbidden in (
            "Serial.printf(\"%s\", app_token",
            "Serial.printf(\"%u\", passkey",
            "log_i(\"token",
            "log_d(\"passkey",
        ):
            self.assertNotIn(forbidden, sources)

    def test_factory_reset_uses_owner_cleanup_and_two_step_sd_confirmation(self):
        services = LIB / "services"
        reset_header = (services / "FactoryResetService.h").read_text(encoding="utf-8")
        reset_source = (services / "FactoryResetService.cpp").read_text(encoding="utf-8")
        storage = (
            (services / "StorageService.h").read_text(encoding="utf-8")
            + (services / "StorageService.cpp").read_text(encoding="utf-8")
        )
        connectivity = (
            (services / "ConnectivityService.h").read_text(encoding="utf-8")
            + (services / "ConnectivityService.cpp").read_text(encoding="utf-8")
        )
        runtime = (
            (ROOT / "Firefly" / "FireflyApp.h").read_text(encoding="utf-8")
            + (ROOT / "Firefly" / "FireflyState.cpp").read_text(encoding="utf-8")
            + (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(encoding="utf-8")
            + (ROOT / "Firefly" / "Firefly.ino").read_text(encoding="utf-8")
        )

        for token in (
            "FactoryResetState::Preview",
            "FactoryResetState::InternalConfirmed",
            "FactoryResetState::SdConfirmed",
            "generation == snapshot_.generation",
            "!erase_sd || owners_.clearManagedSdRoot()",
        ):
            self.assertIn(token, reset_header + reset_source)
        for token in (
            "clearInternalUserData",
            "clearManagedSdRoot",
            '"Music", "Recordings", "Pictures", "Themes"',
            '"Updates", "Backups", "Logs"',
        ):
            self.assertIn(token, storage)
        self.assertIn("clearSensitiveState", connectivity)
        for token in (
            "FireflyFactoryResetOwners",
            "factory_reset_service",
            "Keep SD media",
            "Delete managed FireflyOS data",
            "Factory Reset",
        ):
            self.assertIn(token, runtime)

    def test_factory_reset_cleanup_runs_off_the_lvgl_main_loop(self):
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8"
        )
        process_start = interaction.index("void firefly_process_factory_reset()")
        process_end = interaction.index("void firefly_process_sd_card()", process_start)
        process = interaction[process_start:process_end]
        self.assertIn("factory_reset_worker", interaction)
        self.assertIn("xTaskCreateStaticPinnedToCore", process)
        self.assertNotIn("factory_reset_service.execute", process)
        worker_start = interaction.index("void factory_reset_worker")
        worker_end = interaction.index("void firefly_process_factory_reset()", worker_start)
        worker = interaction[worker_start:worker_end]
        self.assertIn("factory_reset_service.execute", worker)
        self.assertNotIn("lv_", worker)
        self.assertNotIn("notification_center", worker)

    def test_privacy_defaults_and_android_permissions_remain_minimal(self):
        notification_header = (LIB / "services" / "NotificationService.h").read_text(
            encoding="utf-8"
        )
        storage_header = (LIB / "services" / "StorageService.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("lock_screen_body_hidden_ = true", notification_header)
        self.assertIn("hide_notification_content = true", storage_header)

        manifest = (
            ROOT / "AndroidCompanion" / "app" / "src" / "main" /
            "AndroidManifest.xml"
        ).read_text(encoding="utf-8")
        for forbidden in (
            "ACCESS_BACKGROUND_LOCATION",
            "READ_CONTACTS",
            "WRITE_CONTACTS",
            "RECORD_AUDIO",
            "READ_EXTERNAL_STORAGE",
            "WRITE_EXTERNAL_STORAGE",
            "MANAGE_EXTERNAL_STORAGE",
        ):
            self.assertNotIn(forbidden, manifest)

        sources = "\n".join(
            path.read_text(encoding="utf-8")
            for root in (ROOT / "Firefly", LIB / "services", LIB / "hal")
            for path in root.rglob("*.cpp")
        )
        for line in sources.splitlines():
            lowered = line.lower()
            if not any(logger in lowered for logger in ("serial.", "log_")):
                continue
            for secret in (
                "password", "ssid", "app_token", "token", "credential",
                "session_secret", "hmac", "notification.body",
                "recording_content",
            ):
                self.assertNotIn(secret, lowered, line)

        android_root = (
            ROOT / "AndroidCompanion" / "app" / "src" / "main" / "java"
        )
        for path in android_root.rglob("*.kt"):
            for line in path.read_text(encoding="utf-8").splitlines():
                lowered = line.lower()
                if not any(logger in lowered for logger in (
                    "log.v(", "log.d(", "log.i(", "log.w(", "log.e(",
                    "println(", "print(",
                )):
                    continue
                for secret in (
                    "password", "ssid", "token", "credential", "session",
                    "notification.body",
                ):
                    self.assertNotIn(secret, lowered, f"{path}: {line}")


if __name__ == "__main__":
    unittest.main()
