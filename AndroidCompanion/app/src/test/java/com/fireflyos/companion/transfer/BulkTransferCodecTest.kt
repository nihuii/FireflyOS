package com.fireflyos.companion.transfer

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BulkTransferCodecTest {
    @Test
    fun requestCarriesPreflightMetadataAndCorrelationId() {
        val digest = ByteArray(32) { it.toByte() }
        val request = BulkTransferRequest(
            requestId = 0x1234,
            preferSharedLan = true,
            declaredSize = 32,
            sha256Hex = digest.joinToString("") { "%02x".format(it) },
            managedPath = "/FireflyOS/Pictures/test.bin",
        )
        val encoded = BulkTransferCodec.encodeRequestOrNull(request)!!
        val buffer = ByteBuffer.wrap(encoded).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(2, buffer.get().toInt() and 0xFF)
        assertEquals(1, buffer.get().toInt() and 0xFF)
        assertEquals(0x1234, buffer.short.toInt() and 0xFFFF)
        assertEquals(1, buffer.get().toInt() and 0xFF)
        assertEquals(32, buffer.long)
        val decodedDigest = ByteArray(32).also(buffer::get)
        assertArrayEquals(digest, decodedDigest)
        val pathLength = buffer.get().toInt() and 0xFF
        assertEquals(request.managedPath,
            ByteArray(pathLength).also(buffer::get).toString(Charsets.UTF_8))
        assertTrue(!buffer.hasRemaining())
    }

    @Test
    fun cancelIsBoundedAndCorrelated() {
        assertArrayEquals(
            byteArrayOf(2, 4, 0x34, 0x12),
            BulkTransferCodec.encodeCancelOrNull(0x1234),
        )
        assertNull(BulkTransferCodec.encodeCancelOrNull(0))
    }

    @Test
    fun decodesAuthenticatedSoftApSession() {
        val endpoint = "http://192.168.4.1/upload".toByteArray()
        val token = "0102030405060708090a0b0c0d0e0f10".toByteArray()
        val ssid = "Firefly-0102".toByteArray()
        val password = "090a0b0c0d0e".toByteArray()
        val payload = ByteBuffer.allocate(2 + 2 + 1 + 8 + 1 + endpoint.size +
            token.size + 1 + ssid.size + 1 + password.size)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(2.toByte()).put(2.toByte()).putShort(0x1234.toShort())
            .put(BulkTransferMode.SoftAp.wireId.toByte())
            .putLong(301_000)
            .put(endpoint.size.toByte()).put(endpoint).put(token)
            .put(ssid.size.toByte()).put(ssid)
            .put(password.size.toByte()).put(password)
            .array()
        val decoded = BulkTransferCodec.decodeSession(payload)!!
        assertEquals(0x1234, decoded.requestId)
        assertEquals(BulkTransferMode.SoftAp, decoded.mode)
        assertEquals("Firefly-0102", decoded.softApSsid)
        assertEquals("090a0b0c0d0e", decoded.softApPassword)
    }

    @Test
    fun rejectsMalformedOrNonHexToken() {
        assertNull(BulkTransferCodec.decodeSession(byteArrayOf(2, 2, 2)))
        assertNull(BulkTransferCodec.encodeRequestOrNull(
            BulkTransferRequest(1, true, 0, "0".repeat(64),
                "/FireflyOS/Pictures/test.bin"),
        ))
        assertNull(BulkTransferCodec.encodeRequestOrNull(
            BulkTransferRequest(2, true, 1, "0".repeat(64),
                "/FireflyOS/Pictures/album/nested.bin"),
        ))
        assertNull(BulkTransferCodec.encodeRequestOrNull(
            BulkTransferRequest(3, true, 1, "0".repeat(64),
                "/FireflyOS/Pictures/final.part"),
        ))
    }

    @Test
    fun rejectsInvalidLifetimeAndDecodesTerminalStatus() {
        val endpoint = "http://192.168.4.1/upload".toByteArray()
        val token = "0102030405060708090a0b0c0d0e0f10".toByteArray()
        val payload = ByteBuffer.allocate(2 + 2 + 1 + 8 + 1 + endpoint.size +
            token.size + 2)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(2.toByte()).put(2.toByte()).putShort(7)
            .put(BulkTransferMode.SharedLan.wireId.toByte())
            .putLong(0)
            .put(endpoint.size.toByte()).put(endpoint).put(token)
            .put(0.toByte()).put(0.toByte())
            .array()
        assertNull(BulkTransferCodec.decodeSession(payload))
        assertEquals(
            BulkTransferStatus(requestId = 7, state = 6, failure = 13),
            BulkTransferCodec.decodeStatus(byteArrayOf(2, 3, 7, 0, 6, 13)),
        )
    }
}
