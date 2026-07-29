package com.fireflyos.companion.ble

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FrameCodecTest {
    @Test
    fun encodesEmptyHelloGoldenFrame() {
        val encoded = FrameCodec.encode(
            Frame(
                type = MessageType.Hello,
                flags = 0,
                sequence = 1,
                payload = ByteArray(0),
            ),
        )

        assertArrayEquals(hex("4646010100010000004de0"), encoded)
        val decoded = FrameCodec.decode(encoded!!)
        assertTrue(decoded is DecodeResult.Ok)
        val frame = (decoded as DecodeResult.Ok).frame
        assertEquals(MessageType.Hello, frame.type)
        assertEquals(1, frame.sequence)
        assertEquals(0, frame.payload.size)
    }

    @Test
    fun decodesUtf8NotificationGoldenFrame() {
        val payload = "来电".toByteArray(Charsets.UTF_8)
        val encoded = FrameCodec.encode(
            Frame(
                type = MessageType.NotificationPush,
                flags = FrameFlags.ACK_REQUIRED,
                sequence = 0x1234,
                payload = payload,
            ),
        )

        assertArrayEquals(hex("464601200134120600e525e69da5e794b5"), encoded)
        val decoded = FrameCodec.decode(encoded!!)
        assertTrue(decoded is DecodeResult.Ok)
        assertArrayEquals(payload, (decoded as DecodeResult.Ok).frame.payload)
    }

    @Test
    fun badCrcReturnsSealedError() {
        val corrupted = hex("464601200134120600e525e69da5e794b5")
        corrupted[corrupted.lastIndex] = (corrupted.last().toInt() xor 0x01).toByte()

        val decoded = FrameCodec.decode(corrupted)

        assertEquals(DecodeResult.Error(DecodeError.CrcMismatch), decoded)
    }

    @Test
    fun unknownMessageTypeIsRejectedInsteadOfMasqueradingAsError() {
        val encoded = FrameCodec.encode(
            Frame(type = MessageType.Hello, sequence = 5),
        )!!
        encoded[3] = 0x66
        val crcInput = ByteArray(9)
        encoded.copyInto(crcInput, endIndex = 9)
        val crc = FrameCodec.crc16(crcInput)
        encoded[9] = (crc and 0xFF).toByte()
        encoded[10] = ((crc ushr 8) and 0xFF).toByte()

        val decoded = FrameCodec.decode(encoded)

        assertTrue(decoded is DecodeResult.Error)
    }

    @Test
    fun threeFragmentsReassembleInOrder() {
        val payload = "萤火虫协议分片".toByteArray(Charsets.UTF_8)
        val chunks = listOf(
            payload.copyOfRange(0, 7),
            payload.copyOfRange(7, 15),
            payload.copyOfRange(15, payload.size),
        )
        val fragments = chunks.mapIndexed { index, chunk ->
            FrameCodec.encode(
                Frame(
                    type = MessageType.NotificationPush,
                    flags = FrameFlags.FRAGMENT or if (index == 2) FrameFlags.LAST_FRAGMENT else 0,
                    sequence = 77,
                    payload = byteArrayOf(index.toByte(), 3) + chunk,
                ),
            )!!
        }

        val decoded = FrameCodec.reassemble(fragments)

        assertTrue(decoded is DecodeResult.Ok)
        val frame = (decoded as DecodeResult.Ok).frame
        assertEquals(MessageType.NotificationPush, frame.type)
        assertArrayEquals(payload, frame.payload)
    }

    @Test
    fun rejects1025BytePayloadWithoutThrowing() {
        val encoded = FrameCodec.encode(
            Frame(
                type = MessageType.NotificationPush,
                flags = 0,
                sequence = 1,
                payload = ByteArray(1025),
            ),
        )

        assertNull(encoded)
    }

    @Test
    fun notificationPayloadFragmentsWithinAttLimitAndReassembles() {
        val original = Frame(
            type = MessageType.NotificationPush,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 88,
            payload = ByteArray(561) { index -> (index and 0x7F).toByte() },
        )

        val fragments = FrameCodec.fragment(original, 180)

        assertEquals(4, fragments!!.size)
        val encoded = fragments.map { FrameCodec.encode(it)!! }
        assertTrue(encoded.all { it.size <= 180 })
        val reassembled = FrameCodec.reassemble(encoded)
        assertTrue(reassembled is DecodeResult.Ok)
        assertArrayEquals(original.payload, (reassembled as DecodeResult.Ok).frame.payload)
        assertEquals(FrameFlags.ACK_REQUIRED, reassembled.frame.flags)
    }

    @Test
    fun maximumPayloadFallsBackToMtu23WithoutRejectingValidFrame() {
        val original = Frame(
            type = MessageType.NotificationPush,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 89,
            payload = ByteArray(FrameCodec.MAX_PAYLOAD) { index ->
                (index and 0xFF).toByte()
            },
        )

        val fragments = FrameCodec.fragment(original, 20)

        assertEquals(147, fragments!!.size)
        val encoded = fragments.map { FrameCodec.encode(it)!! }
        assertTrue(encoded.all { it.size <= 20 })
        val reassembled = FrameCodec.reassemble(encoded)
        assertTrue(reassembled is DecodeResult.Ok)
        assertArrayEquals(
            original.payload,
            (reassembled as DecodeResult.Ok).frame.payload,
        )
    }

    @Test
    fun streamReassemblerCompletesThreeFragmentsAndPassesSingleFramesThrough() {
        val reassembler = FrameStreamReassembler()
        val single = Frame(MessageType.Hello, sequence = 1)
        assertEquals(StreamFrameResult.Complete(single), reassembler.accept(single))

        val original = Frame(
            type = MessageType.SettingsSet,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 20,
            payload = ByteArray(400) { it.toByte() },
        )
        val fragments = FrameCodec.fragment(original, 180)!!
        assertEquals(StreamFrameResult.Incomplete, reassembler.accept(fragments[0]))
        assertEquals(StreamFrameResult.Incomplete, reassembler.accept(fragments[1]))
        val complete = reassembler.accept(fragments[2])

        assertTrue(complete is StreamFrameResult.Complete)
        complete as StreamFrameResult.Complete
        assertEquals(original.type, complete.frame.type)
        assertEquals(original.flags, complete.frame.flags)
        assertEquals(original.sequence, complete.frame.sequence)
        assertArrayEquals(original.payload, complete.frame.payload)
    }

    @Test
    fun streamReassemblerRejectsWrongOrderOversizeAndDisconnectReset() {
        val reassembler = FrameStreamReassembler()
        val first = fragment(sequence = 7, index = 0, count = 2, bytes = 800)
        val second = fragment(sequence = 7, index = 1, count = 2, bytes = 300)

        assertEquals(StreamFrameResult.Incomplete, reassembler.accept(first))
        assertEquals(
            StreamFrameResult.Error(DecodeError.TooLarge),
            reassembler.accept(second),
        )

        assertEquals(
            StreamFrameResult.Error(DecodeError.BadFragment),
            reassembler.accept(fragment(8, index = 1, count = 3, bytes = 1)),
        )
        assertEquals(
            StreamFrameResult.Incomplete,
            reassembler.accept(fragment(9, index = 0, count = 3, bytes = 1)),
        )
        reassembler.reset()
        assertEquals(
            StreamFrameResult.Error(DecodeError.BadFragment),
            reassembler.accept(fragment(9, index = 1, count = 3, bytes = 1)),
        )
    }

    private fun fragment(
        sequence: Int,
        index: Int,
        count: Int,
        bytes: Int,
    ) = Frame(
        type = MessageType.SettingsSet,
        flags = FrameFlags.ACK_REQUIRED or FrameFlags.FRAGMENT or
            if (index + 1 == count) FrameFlags.LAST_FRAGMENT else 0,
        sequence = sequence,
        payload = byteArrayOf(index.toByte(), count.toByte()) + ByteArray(bytes),
    )

    private fun hex(value: String): ByteArray {
        require(value.length % 2 == 0)
        return ByteArray(value.length / 2) { index ->
            value.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }
}
