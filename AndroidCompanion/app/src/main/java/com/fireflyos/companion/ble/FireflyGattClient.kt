package com.fireflyos.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import com.fireflyos.companion.data.ConnectionStatus
import com.fireflyos.companion.data.DeviceState

class FireflyGattClient(
    private val context: Context,
    private val bluetoothAdapter: BluetoothAdapter,
    private val onStateChanged: (DeviceState) -> Unit,
    private val onFrameReceived: (Frame, Boolean) -> Unit = { _, _ -> },
) {
    companion object {
        private const val SERVICE_UUID_TEXT =
            "7b7f0001-4f53-4653-8000-ff1e00000001"
        val SERVICE_UUID = FireflyProtocol.SERVICE_UUID
        const val SCAN_TIMEOUT_MS = 15_000L
        const val MAX_GATT_OPERATIONS = FrameCodec.MAX_FRAGMENTS
        private const val MAIN_SYNC_TIMEOUT_MS = 2_000L
        private const val MTU_CALLBACK_TIMEOUT_MS = 2_000L
        private const val GATT_OPERATION_TIMEOUT_MS = 2_000L
    }

    private data class ScanSession(
        val scanGeneration: Int,
        val scanner: BluetoothLeScanner,
        val callback: ScanCallback,
        val timeout: Runnable,
    )

    private val handler = Handler(Looper.getMainLooper())
    private val operationQueue =
        FixedGattBatchQueue<GattOperation>(MAX_GATT_OPERATIONS)
    private val streamReassembler = FrameStreamReassembler()
    private val notificationSetup = GattNotificationSetupStateMachine()
    private val scanSessions = ScanSessionTracker()
    private val operationTimeout = Runnable {
        if (operationQueue.timeoutCurrent()) {
            drainOperationQueue()
        }
    }
    private var activeScan: ScanSession? = null
    private var bluetoothGatt: BluetoothGatt? = null
    private var commandCharacteristic: BluetoothGattCharacteristic? = null
    private var eventCharacteristic: BluetoothGattCharacteristic? = null
    @Volatile
    private var negotiatedMtu = GattMtuPolicy.DEFAULT_MTU
    private var mtuDiscoveryPending: BluetoothGatt? = null
    private var mtuTimeout: Runnable? = null

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(
            gatt: BluetoothGatt,
            status: Int,
            newState: Int,
        ) {
            handler.post {
                if (!claimCurrentGatt(gatt)) return@post
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    clearConnection("gatt status: $status")
                    return@post
                }
                when (newState) {
                    BluetoothProfile.STATE_CONNECTED -> {
                        onStateChanged(
                            DeviceState(
                                status = ConnectionStatus.Connecting,
                                deviceName = gatt.device.name,
                                address = gatt.device.address,
                            ),
                        )
                        requestMtuBeforeServiceDiscovery(gatt)
                    }
                    BluetoothProfile.STATE_DISCONNECTED ->
                        clearConnection(null)
                }
            }
        }

        override fun onServicesDiscovered(
            gatt: BluetoothGatt,
            status: Int,
        ) {
            handler.post {
                if (!claimCurrentGatt(gatt)) return@post
                handleServicesDiscovered(gatt, status)
            }
        }

        override fun onMtuChanged(
            gatt: BluetoothGatt,
            mtu: Int,
            status: Int,
        ) {
            handler.post {
                if (!claimCurrentGatt(gatt)) return@post
                negotiatedMtu = if (status == BluetoothGatt.GATT_SUCCESS) {
                    mtu
                } else {
                    GattMtuPolicy.DEFAULT_MTU
                }
                finishMtuRequestAndDiscover(gatt)
            }
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            val value = (characteristic.value ?: ByteArray(0)).copyOf()
            val encrypted =
                gatt.device.bondState == BluetoothDevice.BOND_BONDED
            handler.post {
                if (!claimCurrentGatt(gatt)) return@post
                onCharacteristicValue(value, encrypted)
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            val copied = value.copyOf()
            val encrypted =
                gatt.device.bondState == BluetoothDevice.BOND_BONDED
            handler.post {
                if (!claimCurrentGatt(gatt)) return@post
                onCharacteristicValue(copied, encrypted)
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            handler.post {
                if (!claimCurrentGatt(gatt)) return@post
                completeOperation(status)
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            handler.post {
                if (!claimCurrentGatt(gatt)) return@post
                completeOperation(status)
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun scanAndConnect() {
        handler.post { scanAndConnectOnMain() }
    }

    @SuppressLint("MissingPermission")
    private fun scanAndConnectOnMain() {
        stopScanOnMain()
        val scanner = bluetoothAdapter.bluetoothLeScanner
        if (scanner == null) {
            onStateChanged(
                DeviceState(
                    status = ConnectionStatus.Error,
                    errorMessage = "BLE scanner unavailable",
                ),
            )
            return
        }

        val filters = listOf(
            ScanFilter.Builder()
                .setServiceUuid(ParcelUuid(FireflyProtocol.SERVICE_UUID))
                .build(),
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        val scanGeneration = scanSessions.begin()
        lateinit var session: ScanSession
        val scanCallback = object : ScanCallback() {
            override fun onScanResult(
                callbackType: Int,
                result: ScanResult,
            ) {
                handler.post {
                    if (!scanSessions.claimResult(scanGeneration)) {
                        return@post
                    }
                    finishScanSession(session)
                    connect(result.device)
                }
            }

            override fun onScanFailed(errorCode: Int) {
                handler.post {
                    if (!scanSessions.fail(scanGeneration)) {
                        return@post
                    }
                    finishScanSession(session)
                    onStateChanged(
                        DeviceState(
                            status = ConnectionStatus.Error,
                            errorMessage = "scan failed: $errorCode",
                        ),
                    )
                }
            }
        }
        val scanTimeout = Runnable {
            if (!scanSessions.fail(scanGeneration)) return@Runnable
            finishScanSession(session)
            if (bluetoothGatt == null) {
                onStateChanged(
                    DeviceState(
                        status = ConnectionStatus.Disconnected,
                        errorMessage = "scan timeout",
                    ),
                )
            }
        }
        session = ScanSession(
            scanGeneration,
            scanner,
            scanCallback,
            scanTimeout,
        )
        activeScan = session
        onStateChanged(DeviceState(status = ConnectionStatus.Scanning))
        handler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS)
        try {
            scanner.startScan(filters, settings, scanCallback)
        } catch (error: RuntimeException) {
            if (scanSessions.fail(scanGeneration)) {
                finishScanSession(session)
                onStateChanged(
                    DeviceState(
                        status = ConnectionStatus.Error,
                        errorMessage =
                            "scan start failed: ${error.message}",
                    ),
                )
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        handler.post { stopScanOnMain() }
    }

    @SuppressLint("MissingPermission")
    private fun stopScanOnMain() {
        scanSessions.cancel()
        activeScan?.let(::finishScanSession)
    }

    @SuppressLint("MissingPermission")
    private fun finishScanSession(session: ScanSession) {
        handler.removeCallbacks(session.timeout)
        runCatching { session.scanner.stopScan(session.callback) }
        if (activeScan === session) activeScan = null
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        handler.post {
            stopScanOnMain()
            bluetoothGatt?.disconnect()
            bluetoothGatt?.close()
            clearLocalConnection()
            onStateChanged(DeviceState(status = ConnectionStatus.Disconnected))
        }
    }

    fun writeCommand(value: ByteArray): Boolean =
        writeCommandBatch(listOf(value)) {}

    fun maxAttWriteBytes(): Int =
        GattMtuPolicy.payloadLimit(negotiatedMtu)

    fun writeCommandBatch(
        values: List<ByteArray>,
        onComplete: (Boolean) -> Unit,
    ): Boolean {
        if (values.isEmpty() ||
            values.size > FrameCodec.MAX_FRAGMENTS ||
            values.any {
                it.isEmpty() ||
                    it.size > maxAttWriteBytes()
            }
        ) {
            return false
        }
        val snapshots = values.map { it.copyOf() }
        return runOnMainSynchronously {
            val characteristic = commandCharacteristic
                ?: return@runOnMainSynchronously false
            val operations = snapshots.map {
                GattOperation.CharacteristicWrite(
                    characteristic,
                    it.copyOf(),
                )
            }
            enqueueOperation(operations, onComplete)
        }
    }

    @SuppressLint("MissingPermission")
    private fun connect(device: BluetoothDevice) {
        bluetoothGatt?.close()
        clearLocalConnection()
        onStateChanged(
            DeviceState(
                status = ConnectionStatus.Connecting,
                deviceName = device.name,
                address = device.address,
            ),
        )
        bluetoothGatt =
            device.connectGatt(context, false, gattCallback)
        if (bluetoothGatt == null) {
            onStateChanged(
                DeviceState(
                    status = ConnectionStatus.Error,
                    errorMessage = "GATT connection unavailable",
                ),
            )
        }
    }

    private fun enqueueOperation(
        operations: List<GattOperation>,
        completion: (Boolean) -> Unit,
    ): Boolean {
        if (!operationQueue.enqueueBatch(operations, completion)) {
            return false
        }
        drainOperationQueue()
        return true
    }

    @SuppressLint("MissingPermission")
    private fun drainOperationQueue() {
        val gatt = bluetoothGatt ?: return
        val queued = operationQueue.takeNext() ?: return
        val accepted = when (val operation = queued.value) {
            is GattOperation.CharacteristicWrite ->
                writeCharacteristic(
                    gatt,
                    operation.characteristic,
                    operation.value,
                )
            is GattOperation.DescriptorWrite ->
                writeDescriptor(
                    gatt,
                    operation.descriptor,
                    operation.value,
                )
        }
        if (!accepted) {
            operationQueue.completeCurrent(false)
            drainOperationQueue()
        } else {
            handler.postDelayed(operationTimeout, GATT_OPERATION_TIMEOUT_MS)
        }
    }

    @SuppressLint("MissingPermission")
    private fun writeCharacteristic(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
    ): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(
                characteristic,
                value,
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
            ) == BluetoothStatusCodes.SUCCESS
        } else {
            characteristic.writeType =
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            characteristic.value = value
            gatt.writeCharacteristic(characteristic)
        }

    @SuppressLint("MissingPermission")
    private fun writeDescriptor(
        gatt: BluetoothGatt,
        descriptor: BluetoothGattDescriptor,
        value: ByteArray,
    ): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeDescriptor(descriptor, value) ==
                BluetoothStatusCodes.SUCCESS
        } else {
            descriptor.value = value
            gatt.writeDescriptor(descriptor)
        }

    private fun completeOperation(status: Int) {
        handler.removeCallbacks(operationTimeout)
        operationQueue.completeCurrent(
            status == BluetoothGatt.GATT_SUCCESS,
        )
        drainOperationQueue()
    }

    private fun onCharacteristicValue(
        value: ByteArray,
        encrypted: Boolean,
    ) {
        when (val decoded = FrameCodec.decode(value)) {
            is DecodeResult.Ok -> {
                when (val streamed = streamReassembler.accept(decoded.frame)) {
                    is StreamFrameResult.Complete ->
                        onFrameReceived(streamed.frame, encrypted)
                    StreamFrameResult.Incomplete -> Unit
                    is StreamFrameResult.Error ->
                        onStateChanged(
                            DeviceState(
                                status = ConnectionStatus.Error,
                                errorMessage =
                                    "fragment failed: ${streamed.error}",
                            ),
                        )
                }
            }
            is DecodeResult.Error -> {
                streamReassembler.reset()
                onStateChanged(
                    DeviceState(
                        status = ConnectionStatus.Error,
                        errorMessage = "decode failed: ${decoded.error}",
                    ),
                )
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun clearConnection(reason: String?) {
        bluetoothGatt?.close()
        clearLocalConnection()
        onStateChanged(
            DeviceState(
                status = ConnectionStatus.Disconnected,
                errorMessage = reason,
            ),
        )
    }

    private fun clearLocalConnection() {
        mtuTimeout?.let(handler::removeCallbacks)
        mtuTimeout = null
        mtuDiscoveryPending = null
        negotiatedMtu = GattMtuPolicy.DEFAULT_MTU
        bluetoothGatt = null
        commandCharacteristic = null
        eventCharacteristic = null
        handler.removeCallbacks(operationTimeout)
        operationQueue.cancelAll()
        streamReassembler.reset()
        notificationSetup.reset()
    }

    @SuppressLint("MissingPermission")
    private fun requestMtuBeforeServiceDiscovery(gatt: BluetoothGatt) {
        negotiatedMtu = GattMtuPolicy.DEFAULT_MTU
        mtuDiscoveryPending = gatt
        if (!gatt.requestMtu(GattMtuPolicy.DESIRED_MTU)) {
            finishMtuRequestAndDiscover(gatt)
            return
        }
        val timeout = Runnable {
            if (bluetoothGatt === gatt && mtuDiscoveryPending === gatt) {
                finishMtuRequestAndDiscover(gatt)
            }
        }
        mtuTimeout = timeout
        handler.postDelayed(timeout, MTU_CALLBACK_TIMEOUT_MS)
    }

    @SuppressLint("MissingPermission")
    private fun finishMtuRequestAndDiscover(gatt: BluetoothGatt) {
        if (mtuDiscoveryPending !== gatt) return
        mtuTimeout?.let(handler::removeCallbacks)
        mtuTimeout = null
        mtuDiscoveryPending = null
        if (!gatt.discoverServices()) {
            failCurrentGatt(
                gatt,
                "service discovery rejected",
            )
        }
    }

    @SuppressLint("MissingPermission")
    private fun handleServicesDiscovered(
        gatt: BluetoothGatt,
        status: Int,
    ) {
        if (status != BluetoothGatt.GATT_SUCCESS) {
            failCurrentGatt(
                gatt,
                "service discovery failed: $status",
            )
            return
        }
        val service = gatt.getService(FireflyProtocol.SERVICE_UUID)
        commandCharacteristic =
            service?.getCharacteristic(FireflyProtocol.RX_UUID)
        eventCharacteristic =
            service?.getCharacteristic(FireflyProtocol.TX_UUID)
        val events = eventCharacteristic
        if (service == null ||
            commandCharacteristic == null ||
            events == null
        ) {
            failCurrentGatt(gatt, "FireflyOS service missing")
            return
        }

        val descriptor =
            events.getDescriptor(FireflyProtocol.CCCD_UUID)
        if (descriptor == null) {
            notificationSetup.begin(false, false, false)
            failCurrentGatt(gatt, "FireflyOS CCCD missing")
            return
        }
        if (!gatt.setCharacteristicNotification(events, true)) {
            notificationSetup.begin(false, true, false)
            failCurrentGatt(gatt, "notification enable rejected")
            return
        }

        notificationSetup.begin(true, true, true)
        val descriptorQueued = enqueueOperation(
            listOf(
                GattOperation.DescriptorWrite(
                    descriptor,
                    BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE,
                ),
            ),
        ) { success ->
            if (bluetoothGatt !== gatt) return@enqueueOperation
            when (notificationSetup.onDescriptorWrite(success)) {
                GattNotificationSetupState.Connected ->
                    publishConnected(gatt)
                GattNotificationSetupState.Error ->
                    failCurrentGatt(
                        gatt,
                        "CCCD write failed",
                    )
                else -> Unit
            }
        }
        if (!descriptorQueued) {
            notificationSetup.onDescriptorWrite(false)
            failCurrentGatt(gatt, "CCCD write was not queued")
        }
    }

    @SuppressLint("MissingPermission")
    private fun failCurrentGatt(
        gatt: BluetoothGatt,
        message: String,
    ) {
        if (bluetoothGatt !== gatt) {
            gatt.close()
            return
        }
        gatt.disconnect()
        gatt.close()
        clearLocalConnection()
        onStateChanged(
            DeviceState(
                status = ConnectionStatus.Error,
                errorMessage = message,
            ),
        )
    }

    @SuppressLint("MissingPermission")
    private fun claimCurrentGatt(gatt: BluetoothGatt): Boolean {
        if (bluetoothGatt === gatt) return true
        gatt.close()
        return false
    }

    private fun publishConnected(gatt: BluetoothGatt) {
        onStateChanged(
            DeviceState(
                status = ConnectionStatus.Connected,
                deviceName = gatt.device.name,
                address = gatt.device.address,
                connectedAtMillis = System.currentTimeMillis(),
            ),
        )
    }

    private fun runOnMainSynchronously(action: () -> Boolean): Boolean {
        if (Looper.myLooper() == handler.looper) return action()
        val mainAction = CancellableMainAction(action)
        if (!handler.post { mainAction.run() }) return false
        return when (
            val outcome =
                mainAction.await(MAIN_SYNC_TIMEOUT_MS)
        ) {
            is MainActionOutcome.Completed -> outcome.value
            MainActionOutcome.Cancelled,
            MainActionOutcome.Failed,
            -> false
        }
    }

    private sealed interface GattOperation {
        data class CharacteristicWrite(
            val characteristic: BluetoothGattCharacteristic,
            val value: ByteArray,
        ) : GattOperation

        data class DescriptorWrite(
            val descriptor: BluetoothGattDescriptor,
            val value: ByteArray,
        ) : GattOperation
    }
}
