import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ANDROID = (
    ROOT
    / "AndroidCompanion"
    / "app"
    / "src"
    / "main"
)
LIB = ROOT / "libraries" / "FireflyOS" / "src" / "firefly"


def read_required(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing notification artifact: {path}")
    return path.read_text(encoding="utf-8")


class NotificationSyncContractTests(unittest.TestCase):
    def test_android_listener_is_declared_with_platform_binding_permission(self):
        manifest = read_required(ANDROID / "AndroidManifest.xml")
        for token in (
            '.notifications.PhoneNotificationListener',
            'android:exported="true"',
            'android:label="FireflyOS 通知同步"',
            'android:permission="android.permission.BIND_NOTIFICATION_LISTENER_SERVICE"',
            'android.service.notification.NotificationListenerService',
        ):
            self.assertIn(token, manifest)

    def test_android_notification_wire_format_is_utf8_bounded_and_summary_only(self):
        listener = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "notifications"
            / "PhoneNotificationListener.kt"
        )
        for token in (
            "NotificationListenerService",
            "MAX_TITLE_BYTES = 128",
            "MAX_BODY_BYTES = 256",
            "MAX_ACTIVE_SUMMARIES = 20",
            "Character.codePointAt",
            "MessageType.NotificationPush",
            "MessageType.NotificationDismiss",
            "Notification.EXTRA_TITLE",
            "Notification.EXTRA_TEXT",
            "sbn.packageName",
            "sbn.postTime",
            "sbn.key",
            "applicationLabel",
        ):
            self.assertIn(token, listener)
        for forbidden in (
            "RemoteViews",
            "largeIcon",
            "contentView",
            "Notification.Action",
            "activeNotifications.toList()",
        ):
            self.assertNotIn(forbidden, listener)

    def test_watch_service_is_fixed_capacity_and_ui_free(self):
        header = read_required(LIB / "services" / "NotificationService.h")
        source = read_required(LIB / "services" / "NotificationService.cpp")
        combined = header + source
        for token in (
            "kCapacity = 20",
            "NotificationSummary summaries_[kCapacity]",
            "static_assert(sizeof(NotificationSummary) <= 480",
            "applyFrame",
            "dismiss",
            "clearLocal",
            "setPhoneConnected",
            "copyForDisplay",
        ):
            self.assertIn(token, combined)
        for forbidden in ("std::vector", "std::queue", "new ", "lv_"):
            self.assertNotIn(forbidden, combined)

    def test_authenticated_frames_are_consumed_on_the_main_event_loop(self):
        connectivity = read_required(LIB / "services" / "ConnectivityService.cpp")
        companion = read_required(
            LIB / "services" / "CompanionSyncService.cpp"
        )
        interaction = read_required(ROOT / "Firefly" / "FireflyInteraction.cpp")
        for token in (
            "MessageAuthenticator::verifyAndStrip",
            "EventType::BleMessageReceived",
        ):
            self.assertIn(token, connectivity)
        for token in (
            "companion_frame_dispatcher.dispatch(",
            "refresh_notification_center_from_service()",
            "EventType::PhoneConnectionChanged",
        ):
            self.assertIn(token, interaction)
        self.assertIn("notifications_.applyFrame(frame)", companion)

    def test_activity_owns_repository_and_handles_missing_ble_safely(self):
        activity = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "MainActivity.kt"
        )
        for token in (
            "BluetoothManager",
            "ConnectionRepository",
            "connectionRepository",
            "hasBlePermission",
            "onDestroy",
            "connectionRepository?.close()",
            "bluetooth_unavailable",
            "bluetooth_permission_required",
        ):
            self.assertIn(token, activity)

    def test_android_authenticates_sensitive_frames_before_fragmenting(self):
        authenticator = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "ble"
            / "FrameAuthenticator.kt"
        )
        repository = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "ble"
            / "ConnectionRepository.kt"
        )
        sender = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "ble"
            / "AuthenticatedBusinessSender.kt"
        )
        pairing = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "ble"
            / "PairingProtocolCoordinator.kt"
        )
        combined = authenticator + repository + pairing + sender
        for token in (
            'Mac.getInstance("HmacSHA256")',
            "SecretKeySpec",
            "APP_TOKEN_BYTES = 16",
            "AUTH_TAG_BYTES = 8",
            "Context.MODE_PRIVATE",
            "MessageType.NotificationPush",
            "MessageType.NotificationDismiss",
            "FrameAuthenticator.authenticate",
            "FrameCodec.fragment(authenticated",
            "MessageType.PairConfirm",
            "encrypted",
            "saveToken",
        ):
            self.assertIn(token, combined)
        self.assertLess(
            sender.index("FrameAuthenticator.authenticate"),
            sender.index("FrameCodec.fragment(authenticated"),
        )
        for forbidden in ("Log.", "println(token", "print(token"):
            self.assertNotIn(forbidden, combined)

    def test_pairing_is_explicit_and_ack_policy_is_centralized(self):
        coordinator = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "ble"
            / "PairingProtocolCoordinator.kt"
        )
        repository = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "ble"
            / "ConnectionRepository.kt"
        )
        for token in (
            "beginExplicitPairing",
            "beginRepairPairing",
            "MessageType.PairRequest",
            "MAX_PHONE_NAME_BYTES = 32",
            "MessageType.Ack",
            "FrameFlags.IS_ACK",
            "FrameFlags.ACK_REQUIRED",
            "tokenChanged",
            "contentEquals",
            "completedToken",
        ):
            self.assertIn(token, coordinator)
        for token in (
            "pairingCoordinator.beginExplicitPairing()",
            "fun repairPairingAndConnect()",
            "pairingCoordinator.beginRepairPairing()",
            "pairingCoordinator.onConnected",
            "pairingCoordinator.onInbound",
            "decision.ack",
            "decision.tokenChanged",
        ):
            self.assertIn(token, repository)

        activity = read_required(
            ANDROID
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "MainActivity.kt"
        )
        layout = read_required(ANDROID / "res" / "layout" / "activity_main.xml")
        strings = read_required(ANDROID / "res" / "values" / "strings.xml")
        self.assertIn("repairPairButton", activity + layout)
        self.assertIn("repairPairingAndConnect", activity)
        self.assertIn('android:minHeight="48dp"', layout)
        self.assertIn("重新配对", strings)


if __name__ == "__main__":
    unittest.main()
