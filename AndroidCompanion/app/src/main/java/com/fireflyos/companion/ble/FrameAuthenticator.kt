package com.fireflyos.companion.ble

import android.content.Context
import android.util.Base64
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

interface PairingTokenStore {
    fun loadToken(): ByteArray?
    fun saveToken(token: ByteArray): Boolean
    fun retireToken(): Boolean
    fun hasRetiringToken(): Boolean
    fun clearToken(): Boolean
}

/**
 * Application-private persistence boundary for the pairing token.
 *
 * MODE_PRIVATE keeps the token out of shared files and UI state. A future
 * hardening pass can replace this implementation with Android Keystore without
 * changing ConnectionRepository.
 */
class PrivateSharedPreferencesPairingTokenStore(context: Context) : PairingTokenStore {
    companion object {
        private const val PREFERENCES_NAME = "firefly_pairing_private"
        private const val TOKEN_KEY = "app_token"
        private const val RETIRING_TOKEN_KEY = "retiring_app_token"
    }

    private val preferences = context.applicationContext.getSharedPreferences(
        PREFERENCES_NAME,
        Context.MODE_PRIVATE,
    )

    override fun loadToken(): ByteArray? =
        loadToken(TOKEN_KEY) ?: loadToken(RETIRING_TOKEN_KEY)

    private fun loadToken(key: String): ByteArray? {
        val encoded = preferences.getString(key, null) ?: return null
        val decoded = runCatching { Base64.decode(encoded, Base64.NO_WRAP) }
            .getOrNull() ?: return null
        return decoded.takeIf { it.size == FrameAuthenticator.APP_TOKEN_BYTES }
    }

    override fun saveToken(token: ByteArray): Boolean {
        if (token.size != FrameAuthenticator.APP_TOKEN_BYTES) return false
        val encoded = Base64.encodeToString(token.copyOf(), Base64.NO_WRAP)
        return preferences.edit()
            .putString(TOKEN_KEY, encoded)
            .remove(RETIRING_TOKEN_KEY)
            .commit()
    }

    override fun retireToken(): Boolean {
        if (hasRetiringToken()) return true
        val encoded = preferences.getString(TOKEN_KEY, null) ?: return false
        if (loadToken(TOKEN_KEY) == null) return false
        return preferences.edit()
            .putString(RETIRING_TOKEN_KEY, encoded)
            .remove(TOKEN_KEY)
            .commit()
    }

    override fun hasRetiringToken(): Boolean =
        loadToken(RETIRING_TOKEN_KEY) != null

    override fun clearToken(): Boolean =
        preferences.edit()
            .remove(TOKEN_KEY)
            .remove(RETIRING_TOKEN_KEY)
            .commit()
}

object FrameAuthenticator {
    const val APP_TOKEN_BYTES = 16
    const val AUTH_TAG_BYTES = 8
    private const val PROTOCOL_VERSION = 1

    fun authenticate(frame: Frame, token: ByteArray?): Frame? {
        if (!isSensitive(frame.type)) return frame
        if (token == null || token.size != APP_TOKEN_BYTES ||
            frame.payload.size > FrameCodec.MAX_PAYLOAD - AUTH_TAG_BYTES
        ) {
            return null
        }
        val tag = calculateTag(frame, frame.payload, token) ?: return null
        return frame.copy(payload = frame.payload + tag)
    }

    fun verify(frame: Frame, token: ByteArray?): Frame? {
        if (!isSensitive(frame.type)) return frame
        if (token == null || token.size != APP_TOKEN_BYTES ||
            frame.payload.size < AUTH_TAG_BYTES
        ) {
            return null
        }
        val body = frame.payload.copyOf(frame.payload.size - AUTH_TAG_BYTES)
        val expected = calculateTag(frame, body, token) ?: return null
        var difference = 0
        expected.indices.forEach { index ->
            difference = difference or
                ((expected[index].toInt() and 0xFF) xor
                    (frame.payload[body.size + index].toInt() and 0xFF))
        }
        expected.fill(0)
        return if (difference == 0) frame.copy(payload = body) else null
    }

    private fun calculateTag(
        frame: Frame,
        body: ByteArray,
        token: ByteArray,
    ): ByteArray? {
        val key = token.copyOf()
        return try {
            val header = ByteBuffer.allocate(7)
                .order(ByteOrder.LITTLE_ENDIAN)
                .put(PROTOCOL_VERSION.toByte())
                .put(frame.type.value.toByte())
                .put((frame.flags and 0xFF).toByte())
                .putShort((frame.sequence and 0xFFFF).toShort())
                .putShort(body.size.toShort())
                .array()
            val mac = Mac.getInstance("HmacSHA256")
            mac.init(SecretKeySpec(key, "HmacSHA256"))
            mac.update(header)
            mac.doFinal(body).copyOf(AUTH_TAG_BYTES)
        } catch (_: Exception) {
            null
        } finally {
            key.fill(0)
        }
    }

    fun isSensitive(type: MessageType): Boolean = when (type) {
        MessageType.DeviceState,
        MessageType.SettingsGet,
        MessageType.SettingsSet,
        MessageType.NotificationPush,
        MessageType.NotificationDismiss,
        MessageType.WeatherUpdate,
        MessageType.CalendarUpdate,
        MessageType.MediaCommand,
        MessageType.FindPhone,
        MessageType.FindWatch,
        MessageType.WifiProvision,
        MessageType.BulkTransfer,
        MessageType.OtaControl,
        MessageType.UnpairRequest,
        MessageType.UnpairConfirm,
        -> true
        else -> false
    }
}
