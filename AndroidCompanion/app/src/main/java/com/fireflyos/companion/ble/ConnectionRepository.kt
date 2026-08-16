package com.fireflyos.companion.ble

import android.bluetooth.BluetoothAdapter
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import com.fireflyos.companion.transfer.BulkTransferSession
import com.fireflyos.companion.transfer.BulkTransferUploader
import com.fireflyos.companion.transfer.BulkUploadResult
import java.io.InputStream
import com.fireflyos.companion.data.ConnectionStatus
import com.fireflyos.companion.data.DeviceState
import com.fireflyos.companion.notifications.NotificationSyncBridge
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class ConnectionRepository(
    context: Context,
    bluetoothAdapter: BluetoothAdapter,
    private val tokenStore: PairingTokenStore =
        PrivateSharedPreferencesPairingTokenStore(context),
    localDeviceName: String = Build.MODEL,
) {
    private val connectivityManager =
        context.getSystemService(ConnectivityManager::class.java)
    private var bulkNetworkCallback: ConnectivityManager.NetworkCallback? = null
    private var stateListener: ((DeviceState) -> Unit)? = null
    private var deliveryFailureListener: ((ReliableSendFailure) -> Unit)? = null
    private val _deviceState = MutableStateFlow(DeviceState())
    val deviceState: StateFlow<DeviceState> = _deviceState.asStateFlow()

    private val businessFrames =
        FixedBusinessFrameSink(MAX_RECEIVED_BUSINESS_FRAMES)
    private val pairingCoordinator = PairingProtocolCoordinator(
        localDeviceName,
        tokenStore,
    )
    private val inboundFrameGate = InboundFrameGate(
        token = pairingCoordinator::sessionToken,
        sendAck = { frame ->
            reliableSender.sendFrame(
                frame,
                SystemClock.elapsedRealtime(),
            ) { success ->
                pairingCoordinator.onAcknowledgementWritten(
                    frame.sequence,
                    success,
                )
            }
        },
    )
    private val client: FireflyGattClient
    private val businessSender: AuthenticatedBusinessSender
    private lateinit var reliableSender: ReliableFrameSender
    private var secureSessionReady = false
    private var pendingSettingsReplay = false
    private var pendingHelloSequence: Int? = null
    private val handler = Handler(Looper.getMainLooper())
    private val reliableTick = object : Runnable {
        override fun run() {
            reliableSender.service(SystemClock.elapsedRealtime())
            handler.postDelayed(this, RELIABLE_TICK_MILLIS)
        }
    }

    init {
        client = FireflyGattClient(
            context = context,
            bluetoothAdapter = bluetoothAdapter,
            onStateChanged = { state ->
                _deviceState.value = state
                stateListener?.invoke(state)
                val connected = state.status == ConnectionStatus.Connected
                reliableSender.setConnected(
                    connected,
                    SystemClock.elapsedRealtime(),
                )
                if (connected) {
                    pairingCoordinator.onConnected(
                        reliableSender.allocateSequence(),
                    )?.let {
                        reliableSender.sendFrame(
                            it,
                            SystemClock.elapsedRealtime(),
                        )
                    }
                    val token = tokenStore.loadToken()
                    if (token?.size == FrameAuthenticator.APP_TOKEN_BYTES) {
                        pendingSettingsReplay = true
                        queueHello()
                    }
                } else if (
                    state.status == ConnectionStatus.Disconnected ||
                    state.status == ConnectionStatus.Error
                ) {
                    secureSessionReady = false
                    pendingSettingsReplay = false
                    pendingHelloSequence = null
                    val continueRepair =
                        pairingCoordinator.onDisconnected()
                    inboundFrameGate.reset()
                    if (continueRepair) {
                        handler.post { client.scanAndConnect() }
                    }
                }
                val token = tokenStore.loadToken()
                NotificationSyncBridge.setConnected(
                    connected &&
                        secureSessionReady &&
                        token?.size == FrameAuthenticator.APP_TOKEN_BYTES,
                )
            },
            onFrameReceived = { frame, encrypted ->
                handleReceivedFrame(frame, encrypted)
            },
        )
        businessSender = AuthenticatedBusinessSender(
            connectionStatus = { _deviceState.value.status },
            token = tokenStore::loadToken,
            maxAttBytes = client::maxAttWriteBytes,
            writeCommandBatch = client::writeCommandBatch,
        )
        reliableSender = ReliableFrameSender(
            writeFrame = { frame, completion ->
                businessSender.send(
                    frame.type,
                    frame.payload,
                    frame.sequence,
                    frame.flags,
                ) { success ->
                    completion(
                        success,
                        SystemClock.elapsedRealtime(),
                    )
                }
            },
            onFailure = { failure ->
                deliveryFailureListener?.invoke(failure)
            },
        )
        NotificationSyncBridge.attach { message ->
            reliableSender.enqueue(
                message.type,
                message.payload,
                SystemClock.elapsedRealtime(),
            )
        }
        handler.post(reliableTick)
    }

    fun scanAndConnect() {
        pairingCoordinator.beginExplicitPairing()
        client.scanAndConnect()
    }

    fun repairPairingAndConnect() {
        NotificationSyncBridge.setConnected(false)
        pairingCoordinator.beginRepairPairing()
        if (_deviceState.value.status == ConnectionStatus.Connected) {
            pairingCoordinator.onConnected(
                reliableSender.allocateSequence(),
            )?.let {
                if (!reliableSender.sendFrame(
                        it,
                        SystemClock.elapsedRealtime(),
                    )
                ) {
                    pairingCoordinator.cancelUnpairRequest()
                }
            }
        } else {
            client.scanAndConnect()
        }
    }

    fun requestUnpair(): Boolean {
        val request = pairingCoordinator.requestUnpair(
            reliableSender.allocateSequence(),
        ) ?: return false
        val queued = reliableSender.sendFrame(
            request,
            SystemClock.elapsedRealtime(),
        )
        if (!queued) pairingCoordinator.cancelUnpairRequest()
        return queued
    }

    fun disconnect() {
        client.disconnect()
    }

    fun close() {
        releaseBulkNetwork()
        NotificationSyncBridge.detach()
        handler.removeCallbacks(reliableTick)
        reliableSender.setConnected(false, SystemClock.elapsedRealtime())
        stateListener = null
        businessFrames.setListener(null)
        deliveryFailureListener = null
        try {
            client.disconnect()
        } catch (_: SecurityException) {
            // Runtime Bluetooth permission can be revoked while the Activity lives.
        }
    }

    fun sendFrame(frame: Frame): Boolean {
        return reliableSender.sendFrame(frame, SystemClock.elapsedRealtime())
    }

    fun sendBusinessFrame(type: MessageType, payload: ByteArray): Boolean =
        reliableSender.enqueue(type, payload, SystemClock.elapsedRealtime())

    suspend fun uploadBulkFile(
        session: BulkTransferSession,
        managedPath: String,
        declaredSize: Long,
        sha256: String,
        input: InputStream,
        network: Network? = null,
        onProgress: (Long, Long) -> Unit = { _, _ -> },
    ): BulkUploadResult = BulkTransferUploader.upload(
        session,
        managedPath,
        declaredSize,
        sha256,
        input,
        onProgress,
        openConnection = { url -> network?.openConnection(url) ?: url.openConnection() },
    )

    fun requestBulkSoftApNetwork(
        session: BulkTransferSession,
        onAvailable: (Network) -> Unit,
        onUnavailable: () -> Unit,
    ): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q ||
            session.softApSsid.isEmpty() || session.softApPassword.length < 8 ||
            connectivityManager == null
        ) return false
        releaseBulkNetwork()
        val specifier = WifiNetworkSpecifier.Builder()
            .setSsid(session.softApSsid)
            .setWpa2Passphrase(session.softApPassword)
            .build()
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(specifier)
            .build()
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) = onAvailable(network)
            override fun onUnavailable() = onUnavailable()
        }
        return runCatching {
            connectivityManager.requestNetwork(request, callback, 30_000)
            bulkNetworkCallback = callback
            true
        }.getOrDefault(false)
    }

    fun releaseBulkNetwork() {
        val callback = bulkNetworkCallback ?: return
        runCatching { connectivityManager?.unregisterNetworkCallback(callback) }
        bulkNetworkCallback = null
    }

    fun setStateListener(listener: ((DeviceState) -> Unit)?) {
        stateListener = listener
        listener?.invoke(_deviceState.value)
    }

    fun setBusinessFrameListener(listener: ((Frame) -> Unit)?) {
        if (Looper.myLooper() == handler.looper) {
            businessFrames.setListener(listener)
        } else {
            handler.post { businessFrames.setListener(listener) }
        }
    }

    fun setDeliveryFailureListener(
        listener: ((ReliableSendFailure) -> Unit)?,
    ) {
        deliveryFailureListener = listener
    }

    fun takeReceivedFrame(): Frame? {
        if (Looper.myLooper() == handler.looper) {
            return businessFrames.take()
        }
        val action = CancellableMainAction {
            businessFrames.take()
        }
        if (!handler.post { action.run() }) {
            action.cancel()
            return null
        }
        return when (
            val outcome = action.await(MAIN_SYNC_TIMEOUT_MILLIS)
        ) {
            is MainActionOutcome.Completed -> outcome.value
            MainActionOutcome.Cancelled,
            MainActionOutcome.Failed,
            -> null
        }
    }

    private fun handleReceivedFrame(frame: Frame, encrypted: Boolean) {
        if (frame.isMalformedAck()) return
        if (frame.isStrictAck()) {
            val accepted = reliableSender.onAck(
                frame.sequence,
                SystemClock.elapsedRealtime(),
            )
            if (accepted &&
                encrypted &&
                pendingHelloSequence == (frame.sequence and 0xFFFF)
            ) {
                pendingHelloSequence = null
                secureSessionReady = true
                if (pendingSettingsReplay) {
                    pendingSettingsReplay = false
                    queueSettingsGet()
                    NotificationSyncBridge.setConnected(true)
                }
            }
            return
        }

        inboundFrameGate.receive(frame, encrypted) { verified ->
            val decision = pairingCoordinator.onInbound(
                verified,
                bonded = encrypted,
            )
            if (decision.ack != null &&
                decision.ack.sequence != verified.sequence
            ) {
                return@receive false
            }
            if (decision.tokenChanged) {
                handler.post {
                    secureSessionReady = true
                    pendingSettingsReplay = false
                    pendingHelloSequence = null
                    queueSettingsGet()
                    NotificationSyncBridge.setConnected(true)
                }
            }
            if (decision.unpaired) {
                handler.post {
                    NotificationSyncBridge.setConnected(false)
                }
            }
            if (decision.consumed) return@receive decision.accepted
            businessFrames.accept(verified)
        }
    }

    private fun queueHello(): Boolean {
        val sequence = reliableSender.allocateSequence()
        pendingHelloSequence = sequence
        val queued = reliableSender.sendFrame(
            Frame(
                type = MessageType.Hello,
                flags = FrameFlags.ACK_REQUIRED,
                sequence = sequence,
            ),
            SystemClock.elapsedRealtime(),
        )
        if (!queued) pendingHelloSequence = null
        return queued
    }

    private fun queueSettingsGet(): Boolean =
        reliableSender.enqueue(
            MessageType.SettingsGet,
            byteArrayOf(1),
            SystemClock.elapsedRealtime(),
        )

    companion object {
        private const val MAX_RECEIVED_BUSINESS_FRAMES = 8
        private const val RELIABLE_TICK_MILLIS = 250L
        private const val MAIN_SYNC_TIMEOUT_MILLIS = 2_000L
    }
}
