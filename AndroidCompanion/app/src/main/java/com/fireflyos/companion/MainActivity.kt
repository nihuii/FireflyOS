package com.fireflyos.companion

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.View
import com.fireflyos.companion.ble.ConnectionRepository
import com.fireflyos.companion.ble.Frame
import com.fireflyos.companion.ble.MessageType
import com.fireflyos.companion.ble.ReliableSendFailure
import com.fireflyos.companion.data.ConnectionStatus
import com.fireflyos.companion.data.Base64SettingsSnapshotPersistence
import com.fireflyos.companion.data.DeviceState
import com.fireflyos.companion.data.PrivateSharedPreferencesSettingsSnapshotStore
import com.fireflyos.companion.data.SettingKind
import com.fireflyos.companion.data.SettingsSnapshot
import com.fireflyos.companion.data.SettingsStateStore
import com.fireflyos.companion.databinding.ActivityMainBinding
import com.fireflyos.companion.find.AndroidFindPhoneController
import com.fireflyos.companion.find.FindPhoneRoute
import com.fireflyos.companion.media.AndroidMediaSessionGateway
import com.fireflyos.companion.media.MediaCommand
import com.fireflyos.companion.media.MediaDispatchError
import com.fireflyos.companion.media.MediaDispatchResult
import com.fireflyos.companion.sync.AndroidCalendarDataSource
import com.fireflyos.companion.sync.CalendarPayload
import com.fireflyos.companion.sync.CalendarSyncPlanner
import com.fireflyos.companion.sync.CompanionController
import com.fireflyos.companion.sync.CompanionErrorCodec
import com.fireflyos.companion.sync.PhoneWeather
import kotlin.math.roundToInt

class MainActivity : Activity() {
    private lateinit var binding: ActivityMainBinding
    private var connectionRepository: ConnectionRepository? = null
    private var companionController: CompanionController? = null
    private lateinit var mediaGateway: AndroidMediaSessionGateway
    private lateinit var findPhoneController: AndroidFindPhoneController
    private lateinit var calendarDataSource: AndroidCalendarDataSource
    private var appInForeground = false
    private var calendarSwitchMutation = false
    private var pendingRepairPairing = false
    private var connectionStatus = ConnectionStatus.Idle
    private lateinit var settingsStore: SettingsStateStore

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        val persistence = Base64SettingsSnapshotPersistence(
            PrivateSharedPreferencesSettingsSnapshotStore(applicationContext),
        )
        settingsStore = SettingsStateStore(
            initial = persistence.load(),
            persistence = persistence,
        )

        mediaGateway = AndroidMediaSessionGateway(applicationContext)
        calendarDataSource = AndroidCalendarDataSource(applicationContext)
        findPhoneController = AndroidFindPhoneController(applicationContext) {
            appInForeground
        }
        initializeConnection()
        wireConnectionActions()
        wireNotificationPermissionAction()
        wireNotificationListenerAccessAction()
        wireSettingsActions()
        wireWeatherAction()
        wireCalendarActions()
        wireMediaActions()
        wireFindActions()
    }

    override fun onStart() {
        super.onStart()
        appInForeground = true
    }

    override fun onStop() {
        appInForeground = false
        super.onStop()
    }

    override fun onDestroy() {
        findPhoneController.stop()
        connectionRepository?.setStateListener(null)
        connectionRepository?.setBusinessFrameListener(null)
        connectionRepository?.close()
        companionController = null
        connectionRepository = null
        super.onDestroy()
    }

    @Deprecated("Android permission callback API")
    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        when (requestCode) {
            REQUEST_BLE_PERMISSION -> {
                if (hasBlePermission()) {
                    startConnection(repairPairing = pendingRepairPairing)
                } else {
                    binding.statusText.text =
                        getString(R.string.bluetooth_permission_required)
                }
            }
            REQUEST_CALENDAR_PERMISSION -> {
                if (hasCalendarPermission()) {
                    syncCalendar()
                } else {
                    setCalendarEnabledWithoutCallback(false)
                    sendCalendarDisabled()
                    binding.statusText.text =
                        "Calendar permission denied; calendar sync remains disabled."
                }
            }
            REQUEST_NOTIFICATION_PERMISSION -> {
                binding.statusText.text =
                    if (hasNotificationPermission()) {
                        "Find-phone notification permission granted."
                    } else {
                        "Notification permission denied. Other companion features remain available."
                    }
            }
        }
    }

    private fun initializeConnection() {
        val adapter = runCatching {
            getSystemService(BluetoothManager::class.java)?.adapter
        }.getOrNull()
        if (adapter == null) {
            binding.connectionStatusText.text = getString(R.string.status_disconnected)
            binding.statusText.text = getString(R.string.bluetooth_unavailable)
            binding.scanPairButton.isEnabled = false
            binding.repairPairButton.isEnabled = false
            setRemoteActionsEnabled(false)
            return
        }

        val repository = ConnectionRepository(applicationContext, adapter)
        connectionRepository = repository
        companionController = CompanionController(
            sendBusiness = repository::sendBusinessFrame,
            mediaDispatcher = mediaGateway.dispatcher(),
            triggerFindPhone = findPhoneController::trigger,
            settingsStore = settingsStore,
            onSettingsResolved = ::applyResolvedSettings,
        )
        repository.setStateListener { state ->
            runOnUiThread { renderConnection(state) }
        }
        repository.setBusinessFrameListener { frame ->
            runOnUiThread { handleBusinessFrame(frame) }
        }
        repository.setDeliveryFailureListener { failure ->
            runOnUiThread {
                binding.statusText.text = when (failure) {
                    is ReliableSendFailure.QueueFull ->
                        "Reliable queue is full (${failure.capacity}); reconnect to replay current notifications."
                    is ReliableSendFailure.WriteRejected ->
                        "BLE rejected sequence ${failure.sequence}; it remains pending for retry."
                    is ReliableSendFailure.RetriesExhausted ->
                        "Sequence ${failure.sequence} failed after three retries. Reconnect to replay notifications."
                }
            }
        }
    }

    private fun wireConnectionActions() {
        binding.scanPairButton.setOnClickListener {
            startConnection(repairPairing = false)
        }
        binding.repairPairButton.setOnClickListener {
            startConnection(repairPairing = true)
        }
        binding.requestUnpairButton.setOnClickListener {
            binding.statusText.text =
                if (connectionRepository?.requestUnpair() == true) {
                    "Unpair request sent. Confirm removal on the watch."
                } else {
                    "Unpair request was not queued; connect with the current pairing first."
                }
        }
    }

    private fun wireNotificationPermissionAction() {
        binding.notificationPermissionButton.setOnClickListener {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                binding.statusText.text =
                    "Notification runtime permission is not required on this Android version."
            } else if (hasNotificationPermission()) {
                binding.statusText.text =
                    "Find-phone notification permission is already granted."
            } else {
                requestPermissions(
                    arrayOf(Manifest.permission.POST_NOTIFICATIONS),
                    REQUEST_NOTIFICATION_PERMISSION,
                )
            }
        }
    }

    private fun wireNotificationListenerAccessAction() {
        binding.notificationListenerAccessButton.setOnClickListener {
            runCatching {
                startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
            }.onFailure {
                binding.statusText.text =
                    "Unable to open notification access settings on this device."
            }
        }
    }

    private fun wireSettingsActions() {
        binding.syncBrightnessButton.setOnClickListener {
            val brightness = binding.brightnessInput.text.toString().toIntOrNull()
            if (brightness !in 20..255) {
                binding.statusText.text = "Brightness must be between 20 and 255."
            } else {
                syncSetting(SettingKind.Brightness, byteArrayOf(brightness!!.toByte()))
            }
        }
        binding.syncVolumeButton.setOnClickListener {
            val volume = binding.volumeInput.text.toString().toIntOrNull()
            if (volume !in 0..100) {
                binding.statusText.text = "Volume must be between 0 and 100."
            } else {
                syncSetting(SettingKind.Volume, byteArrayOf(volume!!.toByte()))
            }
        }
        binding.syncAlarmButton.setOnClickListener {
            val match = ALARM_PATTERN.matchEntire(binding.alarmInput.text.toString())
            val hour = match?.groupValues?.get(1)?.toIntOrNull()
            val minute = match?.groupValues?.get(2)?.toIntOrNull()
            if (hour !in 0..23 || minute !in 0..59) {
                binding.statusText.text = "Alarm must use HH:mm in the 24-hour clock."
            } else {
                val name = "Phone alarm".toByteArray(Charsets.UTF_8)
                val value = byteArrayOf(
                    0,
                    1,
                    1,
                    hour!!.toByte(),
                    minute!!.toByte(),
                    0x7F,
                    0,
                    name.size.toByte(),
                ) + name
                syncSetting(SettingKind.Alarm, value)
            }
        }
        binding.syncThemeButton.setOnClickListener {
            val theme = binding.themeInput.text.toString().toByteArray(Charsets.UTF_8)
            if (theme.isEmpty() || theme.size > MAX_THEME_BYTES) {
                binding.statusText.text =
                    "Theme ID must contain between 1 and $MAX_THEME_BYTES UTF-8 bytes."
            } else {
                syncSetting(SettingKind.Theme, theme)
            }
        }
    }

    private fun wireWeatherAction() {
        binding.syncWeatherButton.setOnClickListener {
            val city = binding.weatherCityInput.text.toString().trim()
            val temperature = parseTenths(binding.weatherTemperatureInput.text.toString())
            val code = binding.weatherCodeInput.text.toString().toIntOrNull()
            val high = parseTenths(binding.weatherHighInput.text.toString())
            val low = parseTenths(binding.weatherLowInput.text.toString())
            if (city.isEmpty() || city.toByteArray(Charsets.UTF_8).size > 47 ||
                temperature == null || high == null || low == null ||
                code !in 0..0xFFFF
            ) {
                binding.statusText.text =
                    "Enter a city, valid temperatures, and a weather code from 0 to 65535."
                return@setOnClickListener
            }
            val queued = companionController?.syncWeather(
                PhoneWeather(
                    city = city,
                    temperatureTenthsC = temperature,
                    weatherCode = code!!,
                    highTenthsC = high,
                    lowTenthsC = low,
                    updatedAtEpochSeconds = System.currentTimeMillis() / 1000L,
                ),
            ) == true
            showQueueResult(queued, "Weather")
        }
    }

    private fun wireCalendarActions() {
        binding.calendarSyncSwitch.setOnCheckedChangeListener { _, enabled ->
            if (calendarSwitchMutation) return@setOnCheckedChangeListener
            if (!enabled) {
                sendCalendarDisabled()
            } else if (!hasCalendarPermission()) {
                requestPermissions(
                    arrayOf(Manifest.permission.READ_CALENDAR),
                    REQUEST_CALENDAR_PERMISSION,
                )
            } else {
                syncCalendar()
            }
        }
        binding.syncCalendarButton.setOnClickListener {
            when {
                !binding.calendarSyncSwitch.isChecked ->
                    binding.statusText.text =
                        "Enable calendar summary before requesting a sync."
                !hasCalendarPermission() ->
                    requestPermissions(
                        arrayOf(Manifest.permission.READ_CALENDAR),
                        REQUEST_CALENDAR_PERMISSION,
                    )
                else -> syncCalendar()
            }
        }
    }

    private fun wireMediaActions() {
        binding.mediaPreviousButton.setOnClickListener {
            dispatchMedia(MediaCommand.Previous)
        }
        binding.mediaPlayPauseButton.setOnClickListener {
            dispatchMedia(MediaCommand.PlayPause)
        }
        binding.mediaNextButton.setOnClickListener {
            dispatchMedia(MediaCommand.Next)
        }
        binding.mediaVolumeButton.setOnClickListener {
            val volume = binding.mediaVolumeInput.text.toString().toIntOrNull()
            if (volume !in 0..100) {
                binding.statusText.text = "Media volume must be between 0 and 100."
            } else {
                dispatchMedia(MediaCommand.Volume(volume!!))
            }
        }
    }

    private fun wireFindActions() {
        binding.findPhoneButton.setOnClickListener {
            binding.statusText.text = when (findPhoneController.trigger()) {
                FindPhoneRoute.Foreground ->
                    "Phone alert is sounding for up to 30 seconds."
                FindPhoneRoute.Notification ->
                    "Find-phone notification posted."
                FindPhoneRoute.Unavailable ->
                    "Find phone is unavailable without foreground or notification access."
            }
        }
        binding.findWatchButton.setOnClickListener {
            showQueueResult(
                companionController?.setFindWatch(active = true) == true,
                "Find-watch alert",
            )
        }
        binding.cancelFindWatchButton.setOnClickListener {
            showQueueResult(
                companionController?.setFindWatch(active = false) == true,
                "Find-watch cancellation",
            )
        }
    }

    private fun startConnection(repairPairing: Boolean) {
        pendingRepairPairing = repairPairing
        val repository = connectionRepository
        if (repository == null) {
            binding.statusText.text = getString(R.string.bluetooth_unavailable)
        } else if (!hasBlePermission()) {
            requestPermissions(requiredBlePermissions(), REQUEST_BLE_PERMISSION)
        } else {
            runCatching {
                if (repairPairing) repository.repairPairingAndConnect()
                else repository.scanAndConnect()
            }.onSuccess {
                binding.statusText.text = getString(
                    if (repairPairing) R.string.bluetooth_repairing
                    else R.string.bluetooth_scanning,
                )
            }.onFailure {
                binding.statusText.text = getString(R.string.bluetooth_start_failed)
            }
        }
    }

    private fun syncSetting(kind: SettingKind, value: ByteArray) {
        val setting = settingsStore.recordLocal(
            kind,
            value,
            System.currentTimeMillis(),
        ) ?: run {
            binding.statusText.text =
                "${kind.name} setting was not changed because private persistence failed."
            return
        }
        val delta = SettingsSnapshot(
            mapOf(
                kind to setting,
            ),
        )
        showQueueResult(
            companionController?.syncSettings(delta) == true,
            "${kind.name} setting",
        )
    }

    private fun applyResolvedSettings(snapshot: SettingsSnapshot) {
        snapshot[SettingKind.Brightness]?.value?.singleOrNull()?.let {
            binding.brightnessInput.setText((it.toInt() and 0xFF).toString())
        }
        snapshot[SettingKind.Volume]?.value?.singleOrNull()?.let {
            binding.volumeInput.setText((it.toInt() and 0xFF).toString())
        }
        snapshot[SettingKind.Theme]?.value?.let {
            binding.themeInput.setText(it.toString(Charsets.UTF_8))
        }
        snapshot[SettingKind.Alarm]?.value?.let { value ->
            if (value.size >= 8) {
                val hour = value[3].toInt() and 0xFF
                val minute = value[4].toInt() and 0xFF
                if (hour in 0..23 && minute in 0..59) {
                    binding.alarmInput.setText("%02d:%02d".format(hour, minute))
                }
            }
        }
        binding.statusText.text =
            "Settings reconciled with the watch and the winning snapshot was queued."
    }

    private fun syncCalendar() {
        val now = System.currentTimeMillis()
        val candidates = calendarDataSource.readNextSevenDays(now)
        if (candidates == null) {
            setCalendarEnabledWithoutCallback(false)
            sendCalendarDisabled()
            binding.statusText.text =
                "Calendar access was revoked; calendar sync has been disabled."
            return
        }
        val plan = CalendarSyncPlanner.plan(
            userEnabled = true,
            permissionGranted = true,
            nowEpochMillis = now,
            candidates = candidates,
        )
        binding.calendarText.text =
            "${plan.payload.entries.size} upcoming event summaries selected."
        showQueueResult(
            companionController?.syncCalendar(plan.payload) == true,
            "Calendar summary",
        )
    }

    private fun sendCalendarDisabled() {
        val payload = CalendarPayload(
            enabled = false,
            updatedAtEpochMillis = System.currentTimeMillis(),
            entries = emptyList(),
        )
        showQueueResult(
            companionController?.syncCalendar(payload) == true,
            "Calendar disable",
        )
    }

    private fun dispatchMedia(command: MediaCommand) {
        binding.statusText.text = when (val result = mediaGateway.dispatcher().dispatch(command)) {
            MediaDispatchResult.Success -> "Media command applied to the active session."
            is MediaDispatchResult.Error -> when (result.reason) {
                MediaDispatchError.NotificationAccessRequired ->
                    "Enable notification-listener access to control active media."
                MediaDispatchError.NoActiveSession ->
                    "No active phone media session is available."
                MediaDispatchError.SecurityDenied ->
                    "Android denied access to the active media session."
                MediaDispatchError.InvalidCommand ->
                    "The media command is invalid."
            }
        }
    }

    private fun handleBusinessFrame(frame: Frame) {
        companionController?.handleInbound(frame)
        if (frame.type == MessageType.Error) {
            val error = CompanionErrorCodec.decode(frame.payload)
            binding.statusText.text = if (error == null) {
                "Watch returned an invalid error frame."
            } else {
                "Watch rejected ${error.failedType.name}: ${error.code.name}."
            }
        }
    }

    private fun renderConnection(state: DeviceState) {
        connectionStatus = state.status
        val remoteActionsEnabled = state.status == ConnectionStatus.Connected
        setRemoteActionsEnabled(remoteActionsEnabled)
        binding.connectionStatusText.text = when (state.status) {
            ConnectionStatus.Idle,
            ConnectionStatus.Disconnected,
            -> getString(R.string.status_disconnected)
            ConnectionStatus.Scanning -> "Scanning"
            ConnectionStatus.Connecting -> "Connecting"
            ConnectionStatus.Connected ->
                "Connected${state.deviceName?.let { " to $it" }.orEmpty()}"
            ConnectionStatus.Error -> "Connection error"
        }
        binding.statusText.text = if (remoteActionsEnabled) {
            "Connected. Commands are authenticated before they are sent."
        } else {
            unavailableReason(state)
        }
    }

    private fun unavailableReason(state: DeviceState): String = when (state.status) {
        ConnectionStatus.Scanning -> getString(R.string.bluetooth_scanning)
        ConnectionStatus.Connecting -> "Connecting to the selected FireflyOS watch…"
        ConnectionStatus.Error ->
            state.errorMessage ?: getString(R.string.bluetooth_start_failed)
        else -> getString(R.string.offline_remote_reason)
    }

    private fun setRemoteActionsEnabled(enabled: Boolean) {
        remoteActionViews().forEach { it.isEnabled = enabled }
    }

    private fun remoteActionViews(): List<View> = listOf(
        binding.syncBrightnessButton,
        binding.syncVolumeButton,
        binding.syncAlarmButton,
        binding.syncThemeButton,
        binding.syncWeatherButton,
        binding.calendarSyncSwitch,
        binding.syncCalendarButton,
        binding.mediaPreviousButton,
        binding.mediaPlayPauseButton,
        binding.mediaNextButton,
        binding.mediaVolumeButton,
        binding.findPhoneButton,
        binding.findWatchButton,
        binding.cancelFindWatchButton,
        binding.requestUnpairButton,
    )

    private fun showQueueResult(queued: Boolean, operation: String) {
        binding.statusText.text = if (queued) {
            "$operation queued. The watch will report any validation or persistence error."
        } else {
            "$operation was not queued; check the connection and pairing state."
        }
    }

    private fun setCalendarEnabledWithoutCallback(enabled: Boolean) {
        calendarSwitchMutation = true
        binding.calendarSyncSwitch.isChecked = enabled
        calendarSwitchMutation = false
    }

    private fun parseTenths(value: String): Int? {
        val parsed = value.toDoubleOrNull() ?: return null
        if (!parsed.isFinite()) return null
        val tenths = (parsed * 10.0).roundToInt()
        return tenths.takeIf { it in Short.MIN_VALUE..Short.MAX_VALUE }
    }

    private fun hasBlePermission(): Boolean =
        requiredBlePermissions().all {
            checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
        }

    private fun requiredBlePermissions(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
            )
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    private fun hasCalendarPermission(): Boolean =
        checkSelfPermission(Manifest.permission.READ_CALENDAR) ==
            PackageManager.PERMISSION_GRANTED

    private fun hasNotificationPermission(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) ==
            PackageManager.PERMISSION_GRANTED

    companion object {
        private const val REQUEST_BLE_PERMISSION = 100
        private const val REQUEST_CALENDAR_PERMISSION = 101
        private const val REQUEST_NOTIFICATION_PERMISSION = 102
        private const val MAX_THEME_BYTES = 23
        private val ALARM_PATTERN = Regex("""(\d{1,2}):(\d{2})""")
    }
}
