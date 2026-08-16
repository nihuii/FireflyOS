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
    def test_https_ota_is_runtime_wired_and_background_owned(self):
        state = read(ROOT / "Firefly" / "FireflyState.cpp")
        app = read(ROOT / "Firefly" / "FireflyApp.h")
        sketch = read(ROOT / "Firefly" / "Firefly.ino")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        sources = read(FIREFLY / "services" / "UpdateSources.h")
        coordinator = read(FIREFLY / "services" / "UpdateCoordinator.h")

        self.assertIn("HttpsManifestSource https_manifest_source", state)
        self.assertIn("HttpsUpdateSource https_update_source", state)
        self.assertIn("UpdateCoordinator update_coordinator", state)
        self.assertIn("update_coordinator.postCheck()", sketch)
        self.assertIn("update_coordinator.postStart()", sketch)
        self.assertIn("update_coordinator.postCancel()", sketch)
        self.assertIn("firefly_update_task", app + interaction)
        self.assertIn("update_coordinator.runOnce", interaction)
        self.assertIn("class HttpsManifestSource", sources)
        self.assertNotIn("update_service.tick(millis())", sketch)
        self.assertNotIn("lv_", coordinator)

    def test_update_ui_is_snapshot_only_bounded_and_main_loop_owned(self):
        header = read(FIREFLY / "apps" / "update" / "UpdateApp.h")
        source = read(FIREFLY / "apps" / "update" / "UpdateApp.cpp")
        navigation = read(FIREFLY / "ui" / "NavigationController.h")
        sketch = read(ROOT / "Firefly" / "Firefly.ino")
        preview = read(ROOT / "docs" / "UI预览" / "05-天气与更新" / "系统更新.html")
        combined = header + source
        for token in (
            "void refresh(const UpdateSnapshot & snapshot)",
            "UpdateState::Available",
            "UpdateState::Blocked",
            "UpdateState::Downloading",
            "UpdateState::Verifying",
            "UpdateState::Writing",
            "UpdateState::RebootPending",
            "UpdateState::BootChecking",
            "UpdateState::Completed",
            "UpdateState::Failed",
            "UpdateState::RollbackRequested",
            "UpdateState::RolledBack",
            "lv_anim_del",
            "lv_obj_set_size(primary_button_, 350, 52)",
            "lv_obj_set_size(secondary_button_, 350, 48)",
        ):
            self.assertIn(token, combined)
        for forbidden in (
            "WiFiClientSecure",
            "StorageService",
            "esp_ota_",
            "UpdateSource",
        ):
            self.assertNotIn(forbidden, combined)
        self.assertIn("Update", navigation)
        self.assertIn("update_service.snapshot()", sketch)
        self.assertIn("410", preview)
        self.assertIn("502", preview)
        self.assertIn("状态聚焦", preview)

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

    def test_weather_route_uses_weather_service_and_refreshes_on_main_loop(self):
        app = read(FIREFLY / "apps" / "weather" / "WeatherApp.cpp")
        app_header = read(FIREFLY / "apps" / "weather" / "WeatherApp.h")
        service = read(FIREFLY / "services" / "WeatherService.cpp")
        service_header = read(FIREFLY / "services" / "WeatherService.h")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        sketch = read(ROOT / "Firefly" / "Firefly.ino")
        combined = app + app_header + service + service_header
        for token in (
            "WeatherApp",
            "WeatherSnapshot",
            "WeatherServiceState",
            "requestRefresh",
            "snapshot",
            "freshness",
        ):
            self.assertIn(token, combined)
        for token in (
            "firefly_refresh_companion_weather_ui",
            "weather_service.snapshot",
            "weather_service.state",
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

    def test_wifi_critical_gate_precedes_charging_and_softap_has_hard_limit(self):
        power = read(FIREFLY / "services" / "PowerService.cpp")
        wifi = read(FIREFLY / "services" / "WifiService.cpp")
        gate_start = power.index("bool PowerService::allowsWifiSession")
        gate_end = power.index("void PowerService::recordWakeVerification", gate_start)
        gate = power[gate_start:gate_end]
        self.assertIn("battery_.percent >= 0", gate)
        self.assertIn("battery_.percent <= 100", gate)
        self.assertLess(
            gate.index("battery_.percent <= kCriticalBatteryPercent"),
            gate.index("battery_.charging || battery_.vbus_present"),
        )
        tick_start = wifi.index("void WifiService::tick")
        tick_end = wifi.index("void WifiService::onConnected", tick_start)
        tick = wifi[tick_start:tick_end]
        self.assertLess(
            tick.index("hasHighPowerSession()"),
            tick.index("mode_ != WifiMode::Connected"),
        )
        self.assertIn("void WifiService::normalizeInactiveError()", wifi)
        self.assertGreaterEqual(wifi.count("normalizeInactiveError();"), 3)
        normalize = wifi[wifi.index(
            "void WifiService::normalizeInactiveError()"
        ):wifi.index(
            "bool WifiService::request", wifi.index(
                "void WifiService::normalizeInactiveError()"
            )
        )]
        self.assertIn("mode_ == WifiMode::Error", normalize)
        self.assertIn("active_purposes_ == 0", normalize)

    def test_time_service_is_locked_and_sntp_cleanup_is_centralized(self):
        header = read(FIREFLY / "services" / "TimeService.h")
        source = read(FIREFLY / "services" / "TimeService.cpp")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        for token in (
            "StaticSemaphore_t mutex_storage_",
            "SemaphoreHandle_t mutex_",
            "xSemaphoreCreateRecursiveMutexStatic",
            "TimeRecursiveLock lock(mutex_)",
            "bool TimeService::networkSyncPending() const",
        ):
            self.assertIn(token, header + source)
        self.assertIn("void stop_network_time_request()", interaction)
        service_start = interaction.index("void service_network_time")
        service_end = interaction.index(
            "void refresh_notification_center_from_service", service_start
        )
        service = interaction[service_start:service_end]
        self.assertGreaterEqual(service.count("stop_network_time_request();"), 3)
        self.assertIn("ntp_retry_after_alarm = true", service)
        self.assertIn("wifi_service.release(firefly::WifiPurpose::Ntp", service)
        self.assertIn("wifi_service.request(firefly::WifiPurpose::Ntp", service)
        app_header = read(ROOT / "Firefly" / "FireflyApp.h")
        state = read(ROOT / "Firefly" / "FireflyState.cpp")
        self.assertIn("extern std::atomic<bool> alarm_ringing", app_header)
        self.assertIn("std::atomic<bool> alarm_ringing{false}", state)
        self.assertIn(
            "alarm_ringing.load(std::memory_order_acquire)", service
        )
        self.assertNotIn("flushDeferredNetworkTime(alarm_ringing)", service)
        self.assertNotIn("applyNetworkTime(network_epoch, alarm_ringing)", service)

        provision = interaction[interaction.index(
            "if(frame.type == firefly::protocol::MessageType::WifiProvision"
        ):]
        self.assertIn("const firefly::TimeSnapshot local_time = time_service.now()", provision)
        self.assertIn("local_time.valid", provision)

    def test_weather_https_uses_one_total_request_budget(self):
        header = read(FIREFLY / "services" / "WeatherService.h")
        source = read(FIREFLY / "services" / "WeatherService.cpp")
        self.assertIn("remainingWeatherBudget", source)
        get_start = source.index("bool get(const char * url")
        get_end = source.index("EspWeatherHttpClient default_http", get_start)
        body = source[get_start:get_end]
        self.assertLess(
            body.index("const uint32_t started_ms = millis()"),
            body.index("weather_dns.resolve(host, clock, dns_deadline_ms, address)"),
        )
        self.assertGreaterEqual(body.count("remainingWeatherBudget(started_ms)"), 2)
        self.assertIn("absolute_deadline_ms", body)
        self.assertIn("kResponseAndOverheadReserveMs = 3000UL", body)
        self.assertIn("kBlockingStageCount = 3UL", body)
        self.assertIn(
            "(remaining_ms - kResponseAndOverheadReserveMs) /\n"
            "            kBlockingStageCount", body
        )
        self.assertIn("secure.setHandshakeTimeout(stage_timeout_seconds)", body)
        self.assertIn("stage_timeout_ms + kWriteOverrunGuardMs", body)
        self.assertNotIn("secure.setTimeout(1)", body)
        self.assertIn(
            "secure.connect(address, 443, host, kIsrgRootX1, nullptr, nullptr)",
            body,
        )
        self.assertNotIn("secure.connect(host", body)
        self.assertIn("WeatherHttpResponseReader::read", body)
        self.assertNotIn("HTTPClient", source)
        resolver_start = source.index("class EspWeatherDnsResolver")
        resolver_end = source.index("class EspWeatherHttpClient", resolver_start)
        resolver = source[resolver_start:resolver_end]
        self.assertIn("dns_gethostbyname", resolver)
        self.assertIn("weatherDeadlineOpen(clock, deadline_ms)", resolver)
        self.assertIn("std::atomic<bool> pending_", resolver)
        self.assertIn("EspWeatherDnsResolver weather_dns", resolver)
        for token in (
            "class WeatherResponseStream",
            "class WeatherDeadlineClock",
            "class WeatherHttpResponseReader",
        ):
            self.assertIn(token, header)
        reader_start = source.index("bool WeatherHttpResponseReader::read")
        reader_end = source.index("bool LittleFsWeatherCacheStore::load", reader_start)
        reader = source[reader_start:reader_end]
        self.assertGreaterEqual(reader.count("deadline_ms"), 5)
        self.assertIn("weatherDeadlineOpen(clock, deadline_ms)", reader)
        self.assertIn("readWeatherByte(stream, clock, deadline_ms", reader)
        self.assertIn("header_bytes > kMaxHeaderBytes", reader)
        self.assertIn("chunked", reader)
        refresh_start = source.index("bool WeatherService::requestRefresh")
        refresh_end = source.index("void WeatherService::finishNetworkRequest", refresh_start)
        refresh = source[refresh_start:refresh_end]
        self.assertIn("state_ == WeatherServiceState::WaitingForWifi", refresh)
        self.assertIn("state_ == WeatherServiceState::Updating", refresh)

    def test_weather_app_uses_twelve_fixed_a8_icon_resources(self):
        weather_dir = FIREFLY / "apps" / "weather"
        icon_header = weather_dir / "WeatherIcons.h"
        icon_source = weather_dir / "WeatherIcons.cpp"
        self.assertTrue(icon_header.exists())
        self.assertTrue(icon_source.exists())
        icon_declarations = read(icon_header)
        icons = read(icon_source)
        app = read(weather_dir / "WeatherApp.cpp")
        self.assertEqual(12, icon_declarations.count("extern const lv_img_dsc_t"))
        self.assertEqual(12, icons.count("const lv_img_dsc_t weather_icon_"))
        self.assertEqual(12, icons.count("LV_IMG_CF_ALPHA_8BIT"))
        self.assertEqual(12, icons.count("{ LV_IMG_CF_ALPHA_8BIT, 0, 0, 48, 48 }"))
        self.assertEqual(12, icons.count("  2304,"))
        self.assertIn("lv_img_create", app)
        self.assertIn("lv_img_set_src", app)
        for placeholder in ("SUN", "PART", "RAIN", "STORM", "SNOW", "FOG"):
            self.assertNotIn(f'"{placeholder}"', app)

    def test_bulk_transfer_startup_cleanup_is_bounded_to_managed_roots(self):
        header = read(FIREFLY / "services" / "StorageService.h")
        source = read(FIREFLY / "services" / "StorageService.cpp")
        sketch = read(ROOT / "Firefly" / "Firefly.ino")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        self.assertIn("uint16_t cleanupBulkPartFiles();", header)
        start = source.index("uint16_t StorageService::cleanupBulkPartFiles()")
        body = source[start:]
        for root in ("Themes", "Pictures", "Music", "Updates"):
            self.assertIn(f'"/FireflyOS/{root}"', body)
        for token in (
            'strcmp(path + length - 5, ".part") == 0',
            "managedFileIsDirectory(entry, directory)",
            "removeManaged(path)",
        ):
            self.assertIn(token, body)
        self.assertIn("strnlen(path, out_size) < out_size", source)
        self.assertIn("storage_service.cleanupBulkPartFiles()", sketch)
        self.assertIn("storage_service.cleanupBulkPartFiles()", interaction)

    def test_bulk_transfer_exclusive_lease_waits_for_normal_sd_handles(self):
        header = read(FIREFLY / "services" / "StorageService.h")
        storage = read(FIREFLY / "services" / "StorageService.cpp")
        bulk_header = read(FIREFLY / "services" / "BulkTransferService.h")
        bulk = read(FIREFLY / "services" / "BulkTransferService.cpp")
        self.assertIn("uint16_t normal_sd_handles_ = 0", header)
        self.assertGreaterEqual(storage.count("++normal_sd_handles_"), 2)
        self.assertIn("normal_sd_handles_ == 0", storage)
        close_start = storage.index("void StorageService::closeManaged")
        close_end = storage.index("uint16_t StorageService::cleanupBulkPartFiles", close_start)
        close_body = storage[close_start:close_end]
        self.assertIn("takeSdLock(portMAX_DELAY, true)", close_body)
        self.assertIn("--normal_sd_handles_", close_body)
        self.assertIn("virtual bool cardPresent() const = 0", bulk_header)
        start = bulk.index("bool BulkTransferService::startSession")
        token = bulk.index("uint8_t token[16]", start)
        body = bulk[start:token]
        self.assertLess(body.index("storage_.cardPresent()"),
                        body.index("storage_.beginSession()"))
        self.assertIn("reject(BulkTransferFailure::Busy)", body)

    def test_bulk_transfer_v2_binds_preflight_metadata_and_preserves_failures(self):
        header = read(FIREFLY / "services" / "BulkTransferService.h")
        source = read(FIREFLY / "services" / "BulkTransferService.cpp")
        interaction = read(ROOT / "Firefly" / "FireflyInteraction.cpp")
        for token in (
            "uint16_t request_id",
            "uint64_t declared_size",
            "uint8_t expected_sha256[32]",
            "char managed_path[192]",
            "result_generation",
            "kSessionLimitMs",
            "cancelSession",
        ):
            self.assertIn(token, header)
        start = source.index("bool BulkTransferService::startSession")
        token_creation = source.index("uint8_t token[16]", start)
        start_body = source[start:token_creation]
        self.assertIn("completed_cleanup_pending", start_body)
        self.assertIn("snapshot_.token_hex[0] != '\\0'", start_body)
        for preflight in (
            "normalizeManagedPath",
            "power_.allowsWifiSession",
            "storage_.beginSession",
            "storage_.freeBytes",
        ):
            self.assertLess(source.index(preflight, start), token_creation)
        self.assertIn("now_ms - session_started_ms_ >= kSessionLimitMs", source)
        self.assertIn("strchr(file_name, '/')", source)
        self.assertIn('strcmp(file_name + file_name_length - 5, ".part")', source)
        self.assertIn("BulkTransferFailure::NetworkUnavailable", source)
        self.assertIn("sink_->failure() == BulkTransferFailure::None", source)
        self.assertIn("fail(BulkTransferFailure::SizeMismatch, now_ms)", source)
        self.assertIn("if(!upload_started_) server_.client().stop()", source)
        self.assertIn("sink_->reject(BulkTransferFailure::SizeMismatch, now_ms)", source)
        self.assertIn("sink_->reject(BulkTransferFailure::HashMismatch, now_ms)", source)
        terminal_cancel = source[source.index(
            "bool BulkTransferService::cancelSession"
        ):source.index(
            "BulkTransferFailure BulkTransferService::failure", source.index(
                "bool BulkTransferService::cancelSession"
            )
        )]
        self.assertIn(
            "recordResult(request_id, snapshot_.state, snapshot_.failure)",
            terminal_cancel,
        )
        self.assertIn("frame.payload[0] == 2", interaction)
        self.assertIn("bulk.result_generation", interaction)


if __name__ == "__main__":
    unittest.main()
