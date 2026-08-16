package com.fireflyos.companion.wifi

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class WifiProvisioningCodecTest {
    @Test
    fun payloadRoundTripsNonceTtlAndBoundedCredentials() {
        val request = WifiProvisioningRequest(
            ssid = "Firefly-Lab",
            password = "secret",
            ttlSeconds = 60,
            nonce = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8),
        )

        val encoded = WifiProvisioningCodec.encodeOrNull(request)
        val decoded = WifiProvisioningCodec.decode(encoded!!)

        assertEquals(2, encoded[0].toInt())
        assertEquals(60, encoded[1].toInt())
        assertEquals(request.ssid, decoded!!.ssid)
        assertEquals(request.password, decoded.password)
        assertEquals(request.ttlSeconds, decoded.ttlSeconds)
        assertArrayEquals(request.nonce, decoded.nonce)
        assertTrue(encoded.size <= WifiProvisioningCodec.MAX_PAYLOAD_BYTES)
    }

    @Test
    fun rejectsInvalidTtlAndOversizedCredentials() {
        val nonce = ByteArray(WifiProvisioningCodec.NONCE_BYTES) { it.toByte() }
        fun encoded(ttl: Int, ssid: String = "Lab", password: String = "pw") =
            WifiProvisioningCodec.encodeOrNull(
                WifiProvisioningRequest(ssid, password, ttl, nonce),
            )

        assertNull(encoded(0))
        assertNull(encoded(61))
        assertNull(encoded(60, "x".repeat(33)))
        assertNull(encoded(60, password = "x".repeat(65)))
    }

    @Test
    fun connectionFeedbackUsesExplicitFiniteStates() {
        WifiProvisioningResult.entries.forEach { result ->
            assertEquals(
                result,
                WifiProvisioningResultCodec.decode(
                    WifiProvisioningResultCodec.encode(result),
                ),
            )
        }
        assertNull(WifiProvisioningResultCodec.decode(byteArrayOf(2, 99)))
    }

    @Test
    fun forgetUsesASeparateBoundedCommand() {
        assertArrayEquals(byteArrayOf(2, 0), WifiProvisioningCodec.encodeForget())
        assertNull(WifiProvisioningCodec.decode(WifiProvisioningCodec.encodeForget()))
    }
}
