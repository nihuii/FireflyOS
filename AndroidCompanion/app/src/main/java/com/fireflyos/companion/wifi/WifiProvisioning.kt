package com.fireflyos.companion.wifi

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.CodingErrorAction

data class WifiProvisioningRequest(
    val ssid: String,
    val password: String,
    val ttlSeconds: Int = 60,
    val nonce: ByteArray,
)

object WifiProvisioningCodec {
    const val SCHEMA = 2
    const val NONCE_BYTES = 8
    const val MAX_SSID_BYTES = 32
    const val MAX_PASSWORD_BYTES = 64
    const val MAX_PAYLOAD_BYTES = 12 + MAX_SSID_BYTES + MAX_PASSWORD_BYTES

    fun encodeForget(): ByteArray = byteArrayOf(SCHEMA.toByte(), 0)

    fun encodeOrNull(request: WifiProvisioningRequest): ByteArray? {
        val ssid = request.ssid.toByteArray(Charsets.UTF_8)
        val password = request.password.toByteArray(Charsets.UTF_8)
        if (ssid.isEmpty() || ssid.size > MAX_SSID_BYTES ||
            password.size > MAX_PASSWORD_BYTES ||
            request.nonce.size != NONCE_BYTES || request.ttlSeconds !in 1..60
        ) {
            return null
        }
        return ByteBuffer.allocate(12 + ssid.size + password.size)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(SCHEMA.toByte())
            .put(request.ttlSeconds.toByte())
            .put(request.nonce)
            .put(ssid.size.toByte())
            .put(ssid)
            .put(password.size.toByte())
            .put(password)
            .array()
    }

    fun decode(payload: ByteArray): WifiProvisioningRequest? {
        if (payload.size !in 12..MAX_PAYLOAD_BYTES) return null
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        if ((buffer.get().toInt() and 0xFF) != SCHEMA) return null
        val ttlSeconds = buffer.get().toInt() and 0xFF
        if (ttlSeconds !in 1..60) return null
        val nonce = ByteArray(NONCE_BYTES)
        buffer.get(nonce)
        val ssidLength = buffer.get().toInt() and 0xFF
        if (ssidLength !in 1..MAX_SSID_BYTES || buffer.remaining() < ssidLength + 1) {
            return null
        }
        val ssidBytes = ByteArray(ssidLength)
        buffer.get(ssidBytes)
        val passwordLength = buffer.get().toInt() and 0xFF
        if (passwordLength > MAX_PASSWORD_BYTES || buffer.remaining() != passwordLength) {
            return null
        }
        val passwordBytes = ByteArray(passwordLength)
        buffer.get(passwordBytes)
        return WifiProvisioningRequest(
            ssid = decodeUtf8(ssidBytes) ?: return null,
            password = decodeUtf8(passwordBytes) ?: return null,
            ttlSeconds = ttlSeconds,
            nonce = nonce,
        )
    }

    private fun decodeUtf8(bytes: ByteArray): String? = try {
        Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
            .decode(ByteBuffer.wrap(bytes))
            .toString()
    } catch (_: Exception) {
        null
    }
}

enum class WifiProvisioningResult(val wireId: Int) {
    Connecting(1),
    Success(2),
    AuthFailed(3),
    NotFound(4),
    Timeout(5),
    Forgotten(6),
    Denied(7),
    PersistenceFailed(8),
    Busy(9);

    companion object {
        fun fromWireId(value: Int): WifiProvisioningResult? =
            entries.firstOrNull { it.wireId == value }
    }
}

object WifiProvisioningResultCodec {
    fun encode(result: WifiProvisioningResult): ByteArray =
        byteArrayOf(1, result.wireId.toByte())

    fun decode(payload: ByteArray): WifiProvisioningResult? {
        if (payload.size != 2 || payload[0].toInt() != 1) return null
        return WifiProvisioningResult.fromWireId(payload[1].toInt() and 0xFF)
    }
}
