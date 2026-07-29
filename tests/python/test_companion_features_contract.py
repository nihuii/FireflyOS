import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ANDROID = ROOT / "AndroidCompanion" / "app" / "src" / "main"
JAVA = ANDROID / "java" / "com" / "fireflyos" / "companion"
FIREFLY = ROOT / "libraries" / "FireflyOS" / "src" / "firefly"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing Task9 artifact: {path}")
    return path.read_text(encoding="utf-8")


class CompanionFeaturesContractTests(unittest.TestCase):
    def test_watch_models_are_bounded_ui_free_and_main_loop_dispatched(self):
        header = read(FIREFLY / "services" / "CompanionSyncService.h")
        source = read(FIREFLY / "services" / "CompanionSyncService.cpp")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        combined = header + source
        for token in (
            "VersionedCompanionSetting",
            "uint8_t value[kMaxValueBytes]",
            "CompanionCalendarEntry entries[kCapacity]",
            "kCapacity = 8",
            "kWeatherStaleSeconds = 3 * 60 * 60",
            "kWeatherExpirySeconds = 24 * 60 * 60",
            "CompanionFrameDispatcher",
            "FindDevicePolicy",
            "kNormalDurationMs = 30000",
            "kLowBatteryDurationMs = 5000",
        ):
            self.assertIn(token, combined)
        for forbidden in ("std::vector", "std::string", "new ", "lv_"):
            self.assertNotIn(forbidden, combined)
        for token in (
            "companion_frame_dispatcher.dispatch",
            "notification_service",
            "companion_sync_service",
            "calendar_app.setAgenda",
            "cancelFindWatch",
        ):
            self.assertIn(token, interaction)

    def test_connectivity_has_fixed_dispatch_queue_not_single_notification_mailbox(self):
        header = read(FIREFLY / "services" / "ConnectivityService.h")
        source = read(FIREFLY / "services" / "ConnectivityService.cpp")
        combined = header + source
        for token in (
            "kDispatchQueueCapacity = 4",
            "dispatch_queue_[kDispatchQueueCapacity]",
            "dispatch_head_",
            "dispatch_tail_",
            "dispatch_count_",
            "EventType::BleMessageReceived",
        ):
            self.assertIn(token, combined)
        self.assertNotIn("received_frame_valid_", combined)

    def test_settings_persistence_is_authoritative_before_live_apply(self):
        storage = read(FIREFLY / "services" / "StorageService.cpp")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        self.assertIn("saveCompanionSettingsSnapshot", storage)
        self.assertIn("loadCompanionSettingsSnapshot", storage)
        self.assertLess(
            interaction.index("saveCompanionSettingsSnapshot"),
            interaction.index("set_screen_brightness_level"),
        )

    def test_android_calendar_adapter_uses_only_whitelisted_projection(self):
        calendar = read(JAVA / "sync" / "AndroidCalendarDataSource.kt")
        for token in (
            "CalendarContract.Instances.TITLE",
            "CalendarContract.Instances.BEGIN",
            "CalendarContract.Instances.END",
            "CalendarContract.Instances.ALL_DAY",
            "CalendarSyncPolicy.WINDOW_MILLIS",
            "take(CalendarSyncPolicy.MAX_ENTRIES)",
        ):
            self.assertIn(token, calendar)
        for forbidden in ("ATTENDEE", "EVENT_LOCATION", "DESCRIPTION"):
            self.assertNotIn(forbidden, calendar)

    def test_android_media_adapter_handles_access_empty_and_security_failures(self):
        media = read(JAVA / "media" / "AndroidMediaSessionGateway.kt")
        for token in (
            "MediaSessionManager",
            "MediaController",
            "getActiveSessions",
            "SecurityException",
            "NotificationAccessRequired",
            "NoActiveSession",
        ):
            self.assertIn(token, media)

    def test_android_controller_and_repository_use_authenticated_business_path(self):
        controller = read(JAVA / "sync" / "CompanionController.kt")
        repository = read(JAVA / "ble" / "ConnectionRepository.kt")
        gate = read(JAVA / "ble" / "InboundFrameGate.kt")
        combined = controller + repository + gate
        for token in (
            "SettingsSyncCodec.encode",
            "WeatherPayloadCodec.encodeOrNull",
            "CalendarPayloadCodec.encode",
            "MediaCommandCodec.decode",
            "MessageType.Error",
            "AuthenticatedBusinessSender",
            "setBusinessFrameListener",
            "FrameAuthenticator.verify",
        ):
            self.assertIn(token, combined)

    def test_views_expose_real_task9_actions_and_offline_disable_reason(self):
        layout = read(ANDROID / "res" / "layout" / "activity_main.xml")
        activity = read(JAVA / "MainActivity.kt")
        manifest = read(ANDROID / "AndroidManifest.xml")
        for view_id in (
            "connectionStatusText",
            "syncBrightnessButton",
            "syncVolumeButton",
            "syncAlarmButton",
            "syncThemeButton",
            "syncWeatherButton",
            "calendarSyncSwitch",
            "mediaPlayPauseButton",
            "mediaPreviousButton",
            "mediaNextButton",
            "findPhoneButton",
            "findWatchButton",
        ):
            self.assertIn(view_id, layout + activity)
        self.assertGreaterEqual(layout.count('android:minHeight="48dp"'), 10)
        self.assertIn("remoteActionsEnabled", activity)
        self.assertIn("unavailableReason", activity)
        self.assertIn("android.permission.READ_CALENDAR", manifest)
        self.assertIn("android.permission.POST_NOTIFICATIONS", manifest)

    def test_settings_frame_is_two_phase_and_uses_one_snapshot_blob(self):
        header = read(FIREFLY / "services" / "CompanionSyncService.h")
        source = read(FIREFLY / "services" / "CompanionSyncService.cpp")
        storage = read(FIREFLY / "services" / "StorageService.cpp")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        for token in (
            "CompanionSettingsSnapshot",
            "saveSnapshot",
            "staged",
            "candidate_valid",
            "saveCompanionSettingsSnapshot",
            '"sync_all"',
        ):
            self.assertIn(token, header + source + storage + interaction)

    def test_watch_music_and_find_phone_have_real_authenticated_call_sites(self):
        music = read(FIREFLY / "apps" / "music" / "MusicApp.cpp")
        music_header = read(FIREFLY / "apps" / "music" / "MusicApp.h")
        control_center = read(
            FIREFLY / "ui" / "screens" / "ControlCenter.cpp"
        )
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        sketch = read(ROOT / "Firefly" / "Firefly.ino")
        combined = music + music_header + control_center + interaction + sketch
        for token in (
            "MusicControlTarget::PhoneRemote",
            "MusicControlSelector",
            "phone_media_callback_",
            "buildMediaCommand",
            "buildFindPhone",
            "allocateOutgoingSequence",
            "LV_EVENT_LONG_PRESSED",
            "Hold BLE to find phone",
            "connectivity_service.send",
        ):
            self.assertIn(token, combined)
        self.assertIn(
            "ble_quick_action_cb,\n        LV_EVENT_SHORT_CLICKED",
            sketch,
        )
        self.assertIn(
            '24, LV_SYMBOL_BLUETOOTH, "BLE", false, NULL',
            sketch,
        )
        self.assertIn(
            "ble_find_phone_action_cb,\n        LV_EVENT_LONG_PRESSED",
            sketch,
        )
        self.assertIn("refreshEvent, LV_EVENT_SHORT_CLICKED", music)
        self.assertIn("targetToggleEvent, LV_EVENT_LONG_PRESSED", music)
        self.assertIn("noteLocalTrackSelected", music)
        self.assertNotIn("controlTarget(app->track_count_)", music)

    def test_bidirectional_settings_get_and_local_operations_are_wired(self):
        service = read(FIREFLY / "services" / "CompanionSyncService.cpp")
        service_header = read(FIREFLY / "services" / "CompanionSyncService.h")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        sketch = read(ROOT / "Firefly" / "Firefly.ino")
        themes = read(FIREFLY / "apps" / "themes" / "ThemesApp.cpp")
        controller = read(JAVA / "sync" / "CompanionController.kt")
        settings = read(JAVA / "data" / "SettingsSync.kt")
        repository = read(JAVA / "ble" / "ConnectionRepository.kt")
        combined = service + service_header + interaction + sketch
        for token in (
            "recordLocalSetting",
            "buildSettingsSnapshot",
            "MessageType::SettingsGet",
            "record_local_brightness",
            "record_local_volume",
            "record_local_alarm",
            "record_local_theme",
            "CompanionSyncService::decodeAlarm",
        ):
            self.assertIn(token, combined + themes)
        for token in (
            "SettingsStateStore",
            "requestSettings",
            "SettingsConflictResolver.resolve",
            "onSettingsResolved",
            "MessageType.SettingsGet",
            "queueSettingsGet",
        ):
            self.assertIn(token, controller + settings + repository)

    def test_android_settings_snapshot_is_private_durable_and_atomic(self):
        settings = read(JAVA / "data" / "SettingsSync.kt")
        persistence = read(
            JAVA / "data" / "PrivateSettingsSnapshotPersistence.kt"
        )
        activity = read(JAVA / "MainActivity.kt")
        combined = settings + persistence + activity
        for token in (
            "SettingsSnapshotBlobStore",
            "Base64SettingsSnapshotPersistence",
            "PrivateSharedPreferencesSettingsSnapshotStore",
            "SettingsSyncCodec.encode",
            "SettingsSyncCodec.decode",
            "Base64.getEncoder",
            "Base64.getDecoder",
            "Context.MODE_PRIVATE",
            "settings_snapshot_v1",
            "persistence.load()",
            "persistence.save",
        ):
            self.assertIn(token, combined)
        self.assertNotIn("android.content.Context", settings)

    def test_local_music_transport_is_empty_safe_and_volume_is_authoritative(self):
        music = read(FIREFLY / "apps" / "music" / "MusicApp.cpp")
        music_header = read(FIREFLY / "apps" / "music" / "MusicApp.h")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        sketch = read(ROOT / "Firefly" / "Firefly.ino")
        combined = music + music_header + interaction + sketch
        for token in (
            "MusicQueueNavigator",
            "track_count_ == 0",
            "LocalVolumeCallback",
            "applyLocalVolume",
            "firefly_apply_local_music_volume",
            "record_local_volume",
            "setLocalVolumeCallback",
            "Hold Refresh: Phone remote",
        ):
            self.assertIn(token, combined)
        local_volume_branch = music.index("bool MusicApp::applyLocalVolume")
        remote_volume_branch = music.index("void MusicApp::volumeEvent")
        self.assertLess(local_volume_branch, remote_volume_branch)

    def test_find_phone_tone_has_cancellable_bounded_release(self):
        controller = read(JAVA / "find" / "AndroidFindPhoneController.kt")
        for token in (
            "Handler(Looper.getMainLooper())",
            "releaseTone",
            "removeCallbacks(releaseTone)",
            "postDelayed(releaseTone, TONE_DURATION_MILLIS)",
            "stopTone()",
            "release()",
            "tone = null",
        ):
            self.assertIn(token, controller)

    def test_firmware_authenticates_sensitive_duplicate_before_ack(self):
        source = read(FIREFLY / "services" / "ConnectivityService.cpp")
        start = source.index("void ConnectivityService::processCompleteFrame")
        end = source.index("bool ConnectivityService::processFragment", start)
        body = source[start:end]
        self.assertLess(
            body.index("authenticateAndStrip"),
            body.index("sequenceIsFresh"),
        )

    def test_weather_route_reuses_app_shell_and_refreshes_on_main_loop(self):
        app_shell = read(FIREFLY / "ui" / "screens" / "AppShellScreen.cpp")
        app_shell_header = read(FIREFLY / "ui" / "screens" / "AppShellScreen.h")
        service = read(FIREFLY / "services" / "CompanionSyncService.cpp")
        service_header = read(FIREFLY / "services" / "CompanionSyncService.h")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        sketch = read(ROOT / "Firefly" / "Firefly.ino")
        combined = app_shell + app_shell_header + service + service_header
        for token in (
            "CompanionWeatherView",
            "CompanionWeatherPresenter",
            "showWeather",
            "weather_city_",
            "weather_temperature_",
            "weather_range_",
            "weather_code_",
            "weather_status_",
        ):
            self.assertIn(token, combined)
        for token in (
            "firefly_refresh_companion_weather_ui",
            "companion_sync_service.weatherAt",
            "connectivity_service.connected()",
            "ui_shell.navigation().current() == firefly::Route::Weather",
        ):
            self.assertIn(token, interaction + sketch)

    def test_android_reliable_sender_and_notification_permission_are_wired(self):
        sender = read(JAVA / "ble" / "ReliableFrameSender.kt")
        repository = read(JAVA / "ble" / "ConnectionRepository.kt")
        notifications = read(
            JAVA / "notifications" / "PhoneNotificationListener.kt"
        )
        activity = read(JAVA / "MainActivity.kt")
        layout = read(ANDROID / "res" / "layout" / "activity_main.xml")
        for token in (
            "ACK_TIMEOUT_MILLIS = 2_000",
            "MAX_RETRIES = 3",
            "CAPACITY = 32",
            "ReliableSendFailure",
            "onAck",
            "reliableSender.service",
        ):
            self.assertIn(token, sender + repository)
        self.assertIn("snapshotReset", notifications)
        self.assertIn("notificationPermissionButton", layout + activity)
        self.assertIn("POST_NOTIFICATIONS", activity)
        self.assertIn("onRequestPermissionsResult", activity)
        self.assertIn('android:minHeight="48dp"', layout)

    def test_notification_listener_special_access_has_explicit_system_entry(self):
        activity = read(JAVA / "MainActivity.kt")
        layout = read(ANDROID / "res" / "layout" / "activity_main.xml")
        combined = activity + layout
        for token in (
            "notificationListenerAccessButton",
            "Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS",
            "startActivity",
        ):
            self.assertIn(token, combined)

    def test_remote_theme_is_cache_validated_and_applied_on_ui_loop(self):
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        for token in (
            "resolve_remote_theme_palette",
            "apply_runtime_theme_palette",
            "storage_service.loadThemeCache",
            "ThemePackageService::fireflyDefault",
            "UiComponents::applyThemeTree",
            "UiTheme::setRuntime",
        ):
            self.assertIn(token, interaction)
        persistence_start = interaction.index(
            "bool FireflyCompanionSettingsPersistence::saveSnapshot"
        )
        persistence_end = interaction.index(
            "bool FireflyCompanionSettingsPersistence::restoreSnapshot",
            persistence_start,
        )
        body = interaction[persistence_start:persistence_end]
        self.assertLess(
            body.index("resolve_remote_theme_palette"),
            body.index("saveCompanionSettingsSnapshot"),
        )
        self.assertIn("apply_runtime_theme_palette", body)

    def test_watch_consumes_phone_error_frames_without_error_loop(self):
        header = read(FIREFLY / "services" / "CompanionSyncService.h")
        source = read(FIREFLY / "services" / "CompanionSyncService.cpp")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        combined = header + source + interaction
        for token in (
            "CompanionRemoteError",
            "decodeError",
            "remoteErrorText",
            "MessageType::Error",
            "companion_error_status",
        ):
            self.assertIn(token, combined)
        event_start = interaction.index(
            "case firefly::EventType::BleMessageReceived"
        )
        event_end = interaction.index(
            "case firefly::EventType::PhoneConnectionChanged",
            event_start,
        )
        event_body = interaction[event_start:event_end]
        self.assertLess(
            event_body.index("MessageType::Error"),
            event_body.index("companion_frame_dispatcher.dispatch"),
        )

    def test_authentication_failure_returns_unauthorized_wire_error(self):
        protocol = read(FIREFLY / "protocol" / "ProtocolTypes.h")
        header = read(FIREFLY / "services" / "ConnectivityService.h")
        source = read(FIREFLY / "services" / "ConnectivityService.cpp")
        combined = protocol + header + source
        for token in (
            "Unauthorized = 7",
            "sendProtocolError",
            "WireErrorCode::Unauthorized",
        ):
            self.assertIn(token, combined)
        auth_start = source.index(
            "bool ConnectivityService::authenticateAndStrip"
        )
        auth_end = source.index(
            "bool ConnectivityService::handlePairRequest", auth_start
        )
        self.assertIn(
            "sendProtocolError",
            source[auth_start:auth_end],
        )

    def test_watch_only_advances_inbound_sequence_and_acks_after_publish(self):
        source = read(FIREFLY / "services" / "ConnectivityService.cpp")
        start = source.index("void ConnectivityService::processCompleteFrame")
        end = source.index("bool ConnectivityService::processFragment", start)
        body = source[start:end]
        publish = body.index("publishFrame(frame, now_ms)")
        update = body.index("latest_received_sequence_ = frame.sequence", publish)
        ack = body.index("sendAck(frame.sequence, now_ms)", update)
        self.assertLess(publish, update)
        self.assertLess(update, ack)


if __name__ == "__main__":
    unittest.main()
