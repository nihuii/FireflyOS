package com.fireflyos.companion

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.database.Cursor
import android.net.Network
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.provider.OpenableColumns
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
import com.fireflyos.companion.wifi.WifiProvisioningRequest
import com.fireflyos.companion.wifi.WifiProvisioningResult
import com.fireflyos.companion.transfer.BulkTransferMode
import com.fireflyos.companion.transfer.BulkTransferLaunchGuard
import com.fireflyos.companion.transfer.BulkTransferRequest
import com.fireflyos.companion.transfer.BulkTransferSession
import com.fireflyos.companion.transfer.BulkTransferStatus
import com.fireflyos.companion.transfer.BulkUploadResult
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.security.MessageDigest
import java.security.SecureRandom
import kotlin.math.roundToInt

class MainActivity : Activity() {
    private data class PendingBulkFile(
        val uri: Uri,
        val size: Long,
        val sha256: String,
        val managedPath: String,
    )

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
    private val activityScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private var pendingBulkFile: PendingBulkFile? = null
    private var pendingBulkSession: BulkTransferSession? = null
    private var bulkTransferJob: Job? = null
    private var activeBulkRequestId = 0
    private var bulkOperationGeneration = 0
    private val bulkLaunchGuard = BulkTransferLaunchGuard()
    private var nextBulkRequestId = 1

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
        wireWifiProvisioningActions()
        wireNotificationPermissionAction()
        wireNotificationListenerAccessAction()
        wireSettingsActions()
        wireWeatherAction()
        wireBulkTransferAction()
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
        cancelBulkTransfer("Transfer stopped because the companion screen closed.", false)
        activityScope.cancel()
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
            REQUEST_WIFI_PERMISSION -> {
                val session = pendingBulkSession
                if (hasNearbyWifiPermission() && session != null) {
                    beginBulkUpload(session)
                } else {
                    cancelBulkTransfer(
                        "Nearby Wi-Fi permission was denied; SoftAP transfer was cancelled.",
                    )
                }
            }
        }
    }

    @Deprecated("Android activity result callback API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_BULK_FILE || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        if (bulkTransferJob?.isActive == true || activeBulkRequestId != 0) {
            cancelBulkTransfer("Previous transfer cancelled; preparing the new file.")
        }
        val generation = ++bulkOperationGeneration
        bulkTransferJob = activityScope.launch { prepareBulkFile(uri, generation) }
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
            onWifiProvisioningResult = ::renderWifiProvisioningResult,
            onBulkTransferReady = ::handleBulkTransferReady,
            onBulkTransferStatus = ::renderBulkTransferStatus,
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

    private fun wireWifiProvisioningActions() {
        binding.provisionWifiButton.setOnClickListener {
            val ssid = binding.wifiSsidInput.text.toString().trim()
            val password = binding.wifiPasswordInput.text.toString()
            val nonce = ByteArray(8).also(SecureRandom()::nextBytes)
            val request = WifiProvisioningRequest(
                ssid = ssid,
                password = password,
                ttlSeconds = 60,
                nonce = nonce,
            )
            val queued = companionController?.provisionWifi(request) == true
            binding.wifiProvisionStatusText.text = if (queued) {
                "已发送网络名称，请在手表上核对并确认。"
            } else {
                "配网消息未发送：请检查连接状态、SSID 或密码长度。"
            }
        }
        binding.forgetWifiButton.setOnClickListener {
            binding.wifiProvisionStatusText.text =
                if (companionController?.forgetWifi() == true) {
                    "忘记网络请求已发送，请在手表上确认。"
                } else {
                    "忘记网络请求未发送，请先连接手表。"
                }
        }
    }

    private fun renderWifiProvisioningResult(result: WifiProvisioningResult) {
        binding.wifiProvisionStatusText.text = when (result) {
            WifiProvisioningResult.Connecting -> "正在连接 Wi-Fi…"
            WifiProvisioningResult.Success -> "连接成功，凭据已安全保存。"
            WifiProvisioningResult.AuthFailed -> "认证失败，请核对密码后重新发送。"
            WifiProvisioningResult.NotFound -> "未找到该网络，请确认网络名称和覆盖范围。"
            WifiProvisioningResult.Timeout -> "连接超时；不会自动无限重试。"
            WifiProvisioningResult.Forgotten -> "手表已忘记该网络。"
            WifiProvisioningResult.Denied -> "手表端已取消本次操作。"
            WifiProvisioningResult.PersistenceFailed ->
                "凭据存储更新失败；已保存的网络可能未改变，请重试。"
            WifiProvisioningResult.Busy ->
                "手表正在使用 Wi-Fi；请等待当前网络任务结束后重新发送。"
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
            val latitude = binding.weatherLatitudeInput.text.toString().toDoubleOrNull()
            val longitude = binding.weatherLongitudeInput.text.toString().toDoubleOrNull()
            val temperature = parseTenths(binding.weatherTemperatureInput.text.toString())
            val code = binding.weatherCodeInput.text.toString().toIntOrNull()
            val high = parseTenths(binding.weatherHighInput.text.toString())
            val low = parseTenths(binding.weatherLowInput.text.toString())
            if (city.isEmpty() || city.toByteArray(Charsets.UTF_8).size > 31 ||
                temperature == null || high == null || low == null ||
                code !in 0..0xFFFF || latitude == null || longitude == null ||
                !latitude.isFinite() || !longitude.isFinite() ||
                latitude !in -90.0..90.0 || longitude !in -180.0..180.0
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
                    latitude = latitude,
                    longitude = longitude,
                ),
            ) == true
            showQueueResult(queued, "Weather")
        }
    }

    private fun wireBulkTransferAction() {
        binding.bulkSelectFileButton.setOnClickListener {
            if (bulkTransferJob?.isActive == true || activeBulkRequestId != 0) {
                cancelBulkTransfer("Previous transfer cancelled; select a new file.")
            }
            startActivityForResult(
                Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "*/*"
                },
                REQUEST_BULK_FILE,
            )
        }
        binding.bulkCancelButton.setOnClickListener {
            cancelBulkTransfer("Transfer cancelled by the user.")
        }
    }

    private suspend fun prepareBulkFile(uri: Uri, generation: Int) {
        binding.bulkTransferStatusText.text = "Reading file metadata and SHA-256..."
        val requestedPath = binding.bulkPathInput.text.toString().trim()
        val prepared = withContext(Dispatchers.IO) {
            val metadata = queryBulkMetadata(uri) ?: return@withContext null
            if (metadata.second !in 1..MAX_BULK_FILE_BYTES) return@withContext null
            val digest = MessageDigest.getInstance("SHA-256")
            val buffer = ByteArray(64 * 1024)
            var hashedBytes = 0L
            contentResolver.openInputStream(uri)?.use { input ->
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (count > 0) {
                        hashedBytes += count
                        if (hashedBytes > MAX_BULK_FILE_BYTES) {
                            return@withContext null
                        }
                        digest.update(buffer, 0, count)
                    }
                }
            } ?: return@withContext null
            if (hashedBytes != metadata.second) return@withContext null
            val safeName = metadata.first.replace(Regex("[^A-Za-z0-9._ -]"), "_")
            val path = requestedPath.ifEmpty { "/FireflyOS/Pictures/$safeName" }
            PendingBulkFile(uri, metadata.second,
                digest.digest().joinToString("") { "%02x".format(it) }, path)
        }
        if (generation != bulkOperationGeneration) return
        if (prepared == null) {
            binding.bulkTransferStatusText.text =
                "The selected file is unavailable or outside the 1 byte to 64 MB limit."
            return
        }
        pendingBulkFile = prepared
        binding.bulkPathInput.setText(prepared.managedPath)
        val requestId = allocateBulkRequestId()
        activeBulkRequestId = requestId
        bulkLaunchGuard.reset(requestId)
        val queued = companionController?.requestBulkTransfer(BulkTransferRequest(
            requestId = requestId,
            preferSharedLan = binding.bulkPreferLanCheck.isChecked,
            declaredSize = prepared.size,
            sha256Hex = prepared.sha256,
            managedPath = prepared.managedPath,
        )) == true
        binding.bulkTransferStatusText.text = if (queued) {
            "Transfer session requested; waiting for the watch endpoint."
        } else {
            activeBulkRequestId = 0
            bulkLaunchGuard.clear()
            pendingBulkFile = null
            "Transfer request was not queued; reconnect the authenticated watch."
        }
    }

    private fun queryBulkMetadata(uri: Uri): Pair<String, Long>? {
        var cursor: Cursor? = null
        return try {
            cursor = contentResolver.query(uri, arrayOf(
                OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE,
            ), null, null, null)
            if (cursor == null || !cursor.moveToFirst()) return null
            val name = cursor.getString(0) ?: return null
            val size = if (cursor.isNull(1)) -1L else cursor.getLong(1)
            name to size
        } finally {
            cursor?.close()
        }
    }

    private fun handleBulkTransferReady(session: BulkTransferSession) {
        if (session.requestId != activeBulkRequestId) return
        if (pendingBulkSession?.requestId == session.requestId) return
        if (pendingBulkFile == null) {
            cancelBulkTransfer(
                "The watch offered a transfer endpoint, but no local file is pending.",
            )
            return
        }
        pendingBulkSession = session
        if (session.mode == BulkTransferMode.SoftAp && !hasNearbyWifiPermission()) {
            requestPermissions(requiredWifiPermissions(), REQUEST_WIFI_PERMISSION)
            return
        }
        beginBulkUpload(session)
    }

    private fun renderBulkTransferStatus(status: BulkTransferStatus) {
        if (status.requestId != activeBulkRequestId) return
        val reason = when (status.failure) {
            1 -> "another transfer is active"
            2 -> "battery level is too low"
            3 -> "the SD card is unavailable"
            4 -> "free space is insufficient"
            5 -> "audio is using the resource"
            6 -> "OTA is using the resource"
            7 -> "no local network endpoint was available"
            8 -> "authorization failed"
            9 -> "the managed path was invalid"
            10 -> "the file exceeded the size limit"
            11 -> "the SD write failed"
            12 -> "the received size differed"
            13 -> "SHA-256 verification failed"
            14 -> "the transfer session expired"
            15 -> "the authenticated watch disconnected"
            16 -> "the operation was cancelled"
            else -> "no additional reason"
        }
        val message = when (status.state) {
            4 -> "Transfer complete; the watch committed the verified file."
            5 -> "Transfer cancelled: $reason."
            6 -> "Transfer failed: $reason."
            else -> "Watch transfer state ${status.state}: $reason."
        }
        binding.bulkTransferStatusText.text = message
        if (status.state in 4..6) cancelBulkTransfer(message, false)
    }

    private fun beginBulkUpload(session: BulkTransferSession) {
        if (session.requestId != activeBulkRequestId ||
            pendingBulkSession?.requestId != session.requestId
        ) return
        val file = pendingBulkFile ?: return
        val repository = connectionRepository ?: return
        pendingBulkSession = session
        if (session.mode == BulkTransferMode.SoftAp) {
            if (!bulkLaunchGuard.claimNetworkRequest(session.requestId)) return
            binding.bulkTransferStatusText.text = "Connecting to the watch SoftAP..."
            val requested = repository.requestBulkSoftApNetwork(
                session,
                onAvailable = { network -> runOnUiThread {
                    if (activeBulkRequestId == session.requestId &&
                        pendingBulkSession?.requestId == session.requestId &&
                        bulkLaunchGuard.claimNetworkAvailable(session.requestId)
                    ) uploadBulkFile(file, session, network)
                } },
                onUnavailable = { runOnUiThread {
                    if (activeBulkRequestId == session.requestId &&
                        pendingBulkSession?.requestId == session.requestId &&
                        bulkLaunchGuard.claimNetworkUnavailable(session.requestId)
                    ) {
                        cancelBulkTransfer(
                            "The temporary watch Wi-Fi network was unavailable.",
                        )
                    }
                } },
            )
            if (!requested) {
                cancelBulkTransfer(
                    "Android could not request the temporary watch Wi-Fi network.",
                )
            }
        } else {
            if (bulkLaunchGuard.claimDirectUpload(session.requestId)) {
                uploadBulkFile(file, session, null)
            }
        }
    }

    private fun uploadBulkFile(
        file: PendingBulkFile,
        session: BulkTransferSession,
        network: Network?,
    ) {
        if (session.requestId != activeBulkRequestId ||
            pendingBulkSession?.requestId != session.requestId
        ) return
        val repository = connectionRepository ?: return
        val requestId = session.requestId
        bulkTransferJob = activityScope.launch {
            binding.bulkTransferStatusText.text = "Uploading 0%"
            try {
                val result = withContext(Dispatchers.IO) {
                    contentResolver.openInputStream(file.uri)?.use { input ->
                        repository.uploadBulkFile(
                            session, file.managedPath, file.size, file.sha256, input,
                            network,
                        ) { sent, total ->
                            runOnUiThread {
                                if (activeBulkRequestId == requestId) {
                                    binding.bulkTransferStatusText.text =
                                        "Uploading ${(sent * 100L / total).coerceIn(0, 100)}%"
                                }
                            }
                        }
                    } ?: BulkUploadResult.NetworkError(
                        "The selected file can no longer be read")
                }
                if (activeBulkRequestId != requestId) return@launch
                repository.releaseBulkNetwork()
                when (result) {
                    BulkUploadResult.Success -> {
                        binding.bulkTransferStatusText.text =
                            "Upload accepted; waiting for the watch verification result."
                        pendingBulkFile = null
                        pendingBulkSession = null
                    }
                    is BulkUploadResult.Rejected -> cancelBulkTransfer(
                        "The watch rejected the transfer (HTTP ${result.httpStatus}).",
                    )
                    is BulkUploadResult.NetworkError -> cancelBulkTransfer(
                        "Transfer failed: ${result.message}",
                    )
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } finally {
                if (bulkTransferJob === currentCoroutineContext()[Job]) {
                    bulkTransferJob = null
                }
            }
        }
    }

    private fun allocateBulkRequestId(): Int {
        val allocated = nextBulkRequestId
        nextBulkRequestId = if (allocated == 0xFFFF) 1 else allocated + 1
        return allocated
    }

    private fun cancelBulkTransfer(message: String, notifyWatch: Boolean = true) {
        val requestId = activeBulkRequestId
        bulkTransferJob?.cancel()
        bulkTransferJob = null
        connectionRepository?.releaseBulkNetwork()
        pendingBulkFile = null
        pendingBulkSession = null
        activeBulkRequestId = 0
        ++bulkOperationGeneration
        bulkLaunchGuard.clear()
        if (notifyWatch && requestId != 0) {
            companionController?.cancelBulkTransfer(requestId)
        }
        binding.bulkTransferStatusText.text = message
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
        val wasConnected = connectionStatus == ConnectionStatus.Connected
        connectionStatus = state.status
        val remoteActionsEnabled = state.status == ConnectionStatus.Connected
        if (wasConnected && !remoteActionsEnabled && activeBulkRequestId != 0) {
            cancelBulkTransfer(
                "Transfer cancelled because the authenticated watch disconnected.",
                false,
            )
        }
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
        binding.bulkSelectFileButton,
        binding.provisionWifiButton,
        binding.forgetWifiButton,
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

    private fun hasNearbyWifiPermission(): Boolean =
        requiredWifiPermissions().all {
            checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
        }

    private fun requiredWifiPermissions(): Array<String> = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU ->
            arrayOf(Manifest.permission.NEARBY_WIFI_DEVICES)
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q ->
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        else -> emptyArray()
    }

    companion object {
        private const val REQUEST_BLE_PERMISSION = 100
        private const val REQUEST_CALENDAR_PERMISSION = 101
        private const val REQUEST_NOTIFICATION_PERMISSION = 102
        private const val REQUEST_WIFI_PERMISSION = 103
        private const val REQUEST_BULK_FILE = 104
        private const val MAX_THEME_BYTES = 23
        private const val MAX_BULK_FILE_BYTES = 64L * 1024L * 1024L
        private val ALARM_PATTERN = Regex("""(\d{1,2}):(\d{2})""")
    }
}
