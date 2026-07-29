package com.fireflyos.companion.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class InboundFrameGateTest {
    @Test
    fun listenerDeliveryNeverConsumesFallbackSlotsAndQueuedFramesDrainOnce() {
        val delivered = mutableListOf<Int>()
        val sink = FixedBusinessFrameSink(capacity = 2)
        sink.setListener { delivered += it.sequence }
        repeat(12) { sequence ->
            assertTrue(
                sink.accept(
                    Frame(MessageType.MediaCommand, sequence = sequence),
                ),
            )
        }
        assertEquals((0 until 12).toList(), delivered)
        assertEquals(0, sink.pendingCount)

        sink.setListener(null)
        assertTrue(sink.accept(Frame(MessageType.FindPhone, sequence = 20)))
        assertTrue(sink.accept(Frame(MessageType.FindPhone, sequence = 21)))
        assertEquals(
            false,
            sink.accept(Frame(MessageType.FindPhone, sequence = 22)),
        )
        assertEquals(2, sink.pendingCount)

        sink.setListener { delivered += it.sequence }
        assertEquals(listOf(20, 21), delivered.takeLast(2))
        assertEquals(0, sink.pendingCount)
        sink.setListener { delivered += it.sequence }
        assertEquals(14, delivered.size)
    }

    @Test
    fun authenticatedMediaAndFindDuplicatesExecuteOnceAndAreAcknowledged() {
        val token = ByteArray(FrameAuthenticator.APP_TOKEN_BYTES) { it.toByte() }
        val acknowledgements = mutableListOf<Frame>()
        val accepted = mutableListOf<Frame>()
        val gate = InboundFrameGate(
            token = { token },
            sendAck = { acknowledgements += it; true },
        )
        val media = authenticated(
            token,
            MessageType.MediaCommand,
            sequence = 10,
            payload = byteArrayOf(3),
        )
        val find = authenticated(
            token,
            MessageType.FindPhone,
            sequence = 11,
            payload = byteArrayOf(1, 1),
        )

        assertEquals(
            InboundFrameDisposition.Accepted,
            gate.receive(media, encrypted = true) { accepted += it; true },
        )
        assertEquals(
            InboundFrameDisposition.Duplicate,
            gate.receive(media, encrypted = true) { accepted += it; true },
        )
        assertEquals(
            InboundFrameDisposition.Accepted,
            gate.receive(find, encrypted = true) { accepted += it; true },
        )
        assertEquals(
            InboundFrameDisposition.Duplicate,
            gate.receive(find, encrypted = true) { accepted += it; true },
        )

        assertEquals(listOf(MessageType.MediaCommand, MessageType.FindPhone), accepted.map { it.type })
        assertEquals(listOf(10, 10, 11, 11), acknowledgements.map { it.sequence })
    }

    @Test
    fun badHmacDuplicateIsRejectedWithoutAckAndDisconnectResetsSequence() {
        val token = ByteArray(FrameAuthenticator.APP_TOKEN_BYTES) { (it + 1).toByte() }
        val acknowledgements = mutableListOf<Frame>()
        var executions = 0
        val gate = InboundFrameGate(
            token = { token },
            sendAck = { acknowledgements += it; true },
        )
        val valid = authenticated(
            token,
            MessageType.MediaCommand,
            sequence = 42,
            payload = byteArrayOf(1),
        )
        val tampered = valid.copy(payload = valid.payload.copyOf()).also {
            it.payload[it.payload.lastIndex] =
                (it.payload.last().toInt() xor 1).toByte()
        }

        gate.receive(valid, encrypted = true) { executions += 1; true }
        assertEquals(
            InboundFrameDisposition.Rejected,
            gate.receive(tampered, encrypted = true) { executions += 1; true },
        )
        assertEquals(1, executions)
        assertEquals(1, acknowledgements.size)

        gate.reset()
        assertEquals(
            InboundFrameDisposition.Accepted,
            gate.receive(valid, encrypted = true) { executions += 1; true },
        )
        assertEquals(2, executions)
        assertTrue(acknowledgements.last().sequence == 42)
    }

    @Test
    fun nonAckFrameCarryingAckMarkerIsRejectedBeforeBusinessDispatch() {
        val acknowledgements = mutableListOf<Frame>()
        var executed = false
        val gate = InboundFrameGate(
            token = { null },
            sendAck = { acknowledgements += it; true },
        )
        val malformed = Frame(
            type = MessageType.FindPhone,
            flags = FrameFlags.IS_ACK,
            sequence = 70,
            payload = byteArrayOf(1, 1),
        )

        val result = gate.receive(malformed, encrypted = true) {
            executed = true
            true
        }

        assertEquals(InboundFrameDisposition.Rejected, result)
        assertFalse(executed)
        assertTrue(acknowledgements.isEmpty())
    }

    private fun authenticated(
        token: ByteArray,
        type: MessageType,
        sequence: Int,
        payload: ByteArray,
    ): Frame = FrameAuthenticator.authenticate(
        Frame(
            type = type,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = sequence,
            payload = payload,
        ),
        token,
    )!!
}
