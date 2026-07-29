package com.fireflyos.companion.ble

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertNotNull
import org.junit.Test

class FrameAuthenticatorTest {
    @Test
    fun settingsSetMatchesFirmwareHmacGoldenTag() {
        val token = ByteArray(16) { it.toByte() }
        val frame = Frame(
            type = MessageType.SettingsSet,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 0x1234,
            payload = byteArrayOf(0x10, 0x20, 0x30),
        )

        val authenticated = FrameAuthenticator.authenticate(frame, token)

        assertEquals(11, authenticated!!.payload.size)
        assertArrayEquals(
            hex("10203020d18d970ef31336"),
            authenticated.payload,
        )
    }

    @Test
    fun notificationWithoutPairingTokenIsRejected() {
        val frame = Frame(
            type = MessageType.NotificationPush,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 7,
            payload = "notification".toByteArray(),
        )

        assertNull(FrameAuthenticator.authenticate(frame, null))
    }

    @Test
    fun nonSensitiveHelloDoesNotRequirePairingToken() {
        val frame = Frame(type = MessageType.Hello, sequence = 1)

        assertEquals(frame, FrameAuthenticator.authenticate(frame, null))
    }

    @Test
    fun inboundSensitiveFrameVerifiesAndStripsTagBeforeBusinessDispatch() {
        val token = ByteArray(16) { (it + 1).toByte() }
        val original = Frame(
            type = MessageType.MediaCommand,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 42,
            payload = byteArrayOf(1),
        )
        val authenticated = FrameAuthenticator.authenticate(original, token)!!

        val verified = FrameAuthenticator.verify(authenticated, token)

        assertNotNull(verified)
        assertArrayEquals(original.payload, verified!!.payload)
        val tampered = authenticated.copy(payload = authenticated.payload.copyOf())
        tampered.payload[tampered.payload.lastIndex] =
            (tampered.payload.last().toInt() xor 1).toByte()
        assertNull(FrameAuthenticator.verify(tampered, token))
    }

    private fun hex(value: String): ByteArray =
        ByteArray(value.length / 2) { index ->
            value.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
}
