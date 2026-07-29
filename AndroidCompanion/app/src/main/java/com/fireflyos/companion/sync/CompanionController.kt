package com.fireflyos.companion.sync

import com.fireflyos.companion.ble.Frame
import com.fireflyos.companion.ble.MessageType
import com.fireflyos.companion.data.SettingsSnapshot
import com.fireflyos.companion.data.SettingsStateStore
import com.fireflyos.companion.data.SettingsSyncCodec
import com.fireflyos.companion.find.FindPhoneRoute
import com.fireflyos.companion.media.MediaCommandCodec
import com.fireflyos.companion.media.MediaCommandDispatcher
import com.fireflyos.companion.media.MediaDispatchError
import com.fireflyos.companion.media.MediaDispatchResult

enum class CompanionErrorCode(val wireId: Int) {
    InvalidPayload(1),
    NoActiveMediaSession(2),
    MediaAccessRequired(3),
    SecurityDenied(4),
    FindPhoneUnavailable(5),
    PersistenceFailure(6),
    Unauthorized(7);

    companion object {
        fun fromWireId(value: Int): CompanionErrorCode? =
            entries.firstOrNull { it.wireId == value }
    }
}

data class CompanionError(
    val failedType: MessageType,
    val code: CompanionErrorCode,
)

object CompanionErrorCodec {
    private const val SCHEMA = 1

    fun encode(error: CompanionError): ByteArray = byteArrayOf(
        SCHEMA.toByte(),
        error.failedType.value.toByte(),
        error.code.wireId.toByte(),
    )

    fun decode(payload: ByteArray): CompanionError? {
        if (payload.size != 3 || (payload[0].toInt() and 0xFF) != SCHEMA) return null
        val failedType = MessageType.fromValue(payload[1].toInt() and 0xFF)
            ?: return null
        val code = CompanionErrorCode.fromWireId(payload[2].toInt() and 0xFF)
            ?: return null
        return CompanionError(failedType, code)
    }
}

class CompanionController(
    private val sendBusiness: (MessageType, ByteArray) -> Boolean,
    private val mediaDispatcher: MediaCommandDispatcher,
    private val triggerFindPhone: () -> FindPhoneRoute,
    private val settingsStore: SettingsStateStore = SettingsStateStore(),
    private val onSettingsResolved: (SettingsSnapshot) -> Unit = {},
) {
    fun syncSettings(snapshot: SettingsSnapshot): Boolean =
        sendBusiness(MessageType.SettingsSet, SettingsSyncCodec.encode(snapshot))

    fun requestSettings(): Boolean =
        sendBusiness(MessageType.SettingsGet, byteArrayOf(1))

    fun syncWeather(weather: PhoneWeather): Boolean {
        val payload = WeatherPayloadCodec.encodeOrNull(weather) ?: return false
        return sendBusiness(MessageType.WeatherUpdate, payload)
    }

    fun syncCalendar(payload: CalendarPayload): Boolean =
        sendBusiness(MessageType.CalendarUpdate, CalendarPayloadCodec.encode(payload))

    fun setFindWatch(active: Boolean): Boolean =
        sendBusiness(
            MessageType.FindWatch,
            byteArrayOf(1, if (active) 1 else 0),
        )

    fun handleInbound(frame: Frame) {
        when (frame.type) {
            MessageType.SettingsSet -> handleSettings(frame)
            MessageType.MediaCommand -> handleMedia(frame)
            MessageType.FindPhone -> handleFindPhone(frame)
            else -> Unit
        }
    }

    private fun handleSettings(frame: Frame) {
        val remote = SettingsSyncCodec.decode(frame.payload)
        if (remote == null) {
            sendError(MessageType.SettingsSet, CompanionErrorCode.InvalidPayload)
            return
        }
        val resolved = settingsStore.reconcile(remote)
        if (resolved == null) {
            sendError(MessageType.SettingsSet, CompanionErrorCode.PersistenceFailure)
            return
        }
        onSettingsResolved(resolved)
        syncSettings(resolved)
    }

    private fun handleMedia(frame: Frame) {
        val command = MediaCommandCodec.decode(frame.payload)
        if (command == null) {
            sendError(MessageType.MediaCommand, CompanionErrorCode.InvalidPayload)
            return
        }
        when (val result = mediaDispatcher.dispatch(command)) {
            MediaDispatchResult.Success -> Unit
            is MediaDispatchResult.Error -> sendError(
                MessageType.MediaCommand,
                when (result.reason) {
                    MediaDispatchError.NoActiveSession ->
                        CompanionErrorCode.NoActiveMediaSession
                    MediaDispatchError.NotificationAccessRequired ->
                        CompanionErrorCode.MediaAccessRequired
                    MediaDispatchError.SecurityDenied ->
                        CompanionErrorCode.SecurityDenied
                    MediaDispatchError.InvalidCommand ->
                        CompanionErrorCode.InvalidPayload
                },
            )
        }
    }

    private fun handleFindPhone(frame: Frame) {
        if (!frame.payload.contentEquals(byteArrayOf(1, 1))) {
            sendError(MessageType.FindPhone, CompanionErrorCode.InvalidPayload)
        } else if (triggerFindPhone() == FindPhoneRoute.Unavailable) {
            sendError(MessageType.FindPhone, CompanionErrorCode.FindPhoneUnavailable)
        }
    }

    private fun sendError(type: MessageType, code: CompanionErrorCode) {
        sendBusiness(
            MessageType.Error,
            CompanionErrorCodec.encode(CompanionError(type, code)),
        )
    }
}
