package com.fireflyos.companion.ble

import com.fireflyos.companion.notifications.NotificationOutboundMessage
import com.fireflyos.companion.notifications.NotificationPayloadCodec
import com.fireflyos.companion.notifications.NotificationSyncCoordinator
import com.fireflyos.companion.notifications.PhoneNotificationSummary
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ReliableFrameSenderTest {
    @Test
    fun ackRequiredFramesAreStrictlySerialAndDuplicateAckIsIgnored() {
        val writes = mutableListOf<Frame>()
        val completions = mutableListOf<(Boolean, Long) -> Unit>()
        val sender = ReliableFrameSender(writeFrame = { frame, completion ->
            writes += frame
            completions += completion
            true
        })
        sender.setConnected(true, 0)

        assertTrue(sender.enqueue(MessageType.SettingsSet, byteArrayOf(1), 0))
        assertTrue(sender.enqueue(MessageType.WeatherUpdate, byteArrayOf(2), 0))
        assertEquals(1, writes.size)
        val firstSequence = writes.single().sequence
        completions[0](true, 5)

        assertFalse(sender.onAck(firstSequence + 1, 10))
        assertEquals(1, writes.size)
        assertTrue(sender.onAck(firstSequence, 20))
        assertEquals(2, writes.size)
        assertFalse(sender.onAck(firstSequence, 30))
        assertTrue(writes[1].sequence != firstSequence)
    }

    @Test
    fun timeoutRetriesThreeTimesWithSameSequenceThenReportsFailure() {
        val writes = mutableListOf<Frame>()
        val completions = mutableListOf<(Boolean, Long) -> Unit>()
        val failures = mutableListOf<ReliableSendFailure>()
        val sender = ReliableFrameSender(
            writeFrame = { frame, completion ->
                writes += frame.copy(payload = frame.payload.copyOf())
                completions += completion
                true
            },
            onFailure = failures::add,
        )
        sender.setConnected(true, 0)
        sender.enqueue(MessageType.NotificationPush, byteArrayOf(7), 0)

        repeat(4) { attempt ->
            completions[attempt](true, attempt * 2_000L)
            sender.service(attempt * 2_000L + 2_000L)
        }

        assertEquals(4, writes.size)
        assertEquals(1, writes.map { it.sequence }.distinct().size)
        assertTrue(failures.last() is ReliableSendFailure.RetriesExhausted)
        assertEquals(0, sender.pendingCount)
    }

    @Test
    fun ackBypassesInFlightAndQueueFullIsExplicit() {
        val writes = mutableListOf<Frame>()
        val completions = mutableListOf<(Boolean, Long) -> Unit>()
        val failures = mutableListOf<ReliableSendFailure>()
        val sender = ReliableFrameSender(
            capacity = 2,
            writeFrame = { frame, completion ->
                writes += frame
                completions += completion
                true
            },
            onFailure = failures::add,
        )
        sender.setConnected(true, 0)
        assertTrue(sender.enqueue(MessageType.SettingsSet, byteArrayOf(1), 0))
        assertTrue(sender.enqueue(MessageType.WeatherUpdate, byteArrayOf(2), 0))
        assertFalse(sender.enqueue(MessageType.CalendarUpdate, byteArrayOf(3), 0))
        assertTrue(failures.last() is ReliableSendFailure.QueueFull)

        assertTrue(
            sender.sendFrame(
                Frame(
                    type = MessageType.Ack,
                    flags = FrameFlags.IS_ACK,
                    sequence = 99,
                ),
                1,
            ),
        )
        assertEquals(MessageType.Ack, writes.last().type)
        assertEquals(2, sender.pendingCount)
    }

    @Test
    fun malformedAckMarkerDoesNotBypassInFlightReliableFrame() {
        val writes = mutableListOf<Frame>()
        val completions = mutableListOf<(Boolean, Long) -> Unit>()
        val sender = ReliableFrameSender(writeFrame = { frame, completion ->
            writes += frame
            completions += completion
            true
        })
        sender.setConnected(true, 0)
        sender.enqueue(MessageType.SettingsSet, byteArrayOf(1), 0)
        val inFlightSequence = sender.inFlightSequence

        val accepted = sender.sendFrame(
            Frame(
                type = MessageType.SettingsGet,
                flags = FrameFlags.IS_ACK,
                sequence = 99,
                payload = byteArrayOf(1),
            ),
            1,
        )

        assertFalse(accepted)
        assertEquals(1, writes.size)
        assertEquals(inFlightSequence, sender.inFlightSequence)
    }

    @Test
    fun immediateAckReportsActualGattWriteCompletion() {
        var completion: ((Boolean, Long) -> Unit)? = null
        val written = mutableListOf<Boolean>()
        val sender = ReliableFrameSender(writeFrame = { _, callback ->
            completion = callback
            true
        })
        sender.setConnected(true, 0)

        assertTrue(
            sender.sendFrame(
                Frame(
                    type = MessageType.Ack,
                    flags = FrameFlags.IS_ACK,
                    sequence = 100,
                ),
                1,
                { success -> written += success },
            ),
        )
        assertTrue(written.isEmpty())

        completion!!(true, 2)

        assertEquals(listOf(true), written)
    }

    @Test
    fun reconnectCanQueueResetPlusTwentyCurrentNotifications() {
        val writes = mutableListOf<Frame>()
        val completions = mutableListOf<(Boolean, Long) -> Unit>()
        val sender = ReliableFrameSender(writeFrame = { frame, completion ->
            writes += frame
            completions += completion
            true
        })
        val coordinator = NotificationSyncCoordinator()
        repeat(20) { index ->
            coordinator.upsert(summary("key-$index", index.toLong()))
        }

        sender.setConnected(true, 100)
        coordinator.setConnected(true) { message: NotificationOutboundMessage ->
            sender.enqueue(message.type, message.payload, 100)
        }

        assertEquals(21, sender.pendingCount)
        repeat(21) {
            val current = writes.last()
            completions.last()(true, 101L + it)
            assertTrue(sender.onAck(current.sequence, 101L + it))
        }
        assertEquals(21, writes.size)
        assertTrue(NotificationPayloadCodec.isSnapshotReset(writes.first().payload))
        assertEquals(0, sender.pendingCount)
    }

    @Test
    fun slowBatchStartsAckTimeoutOnlyAtActualCompletion() {
        val writes = mutableListOf<Frame>()
        val completions = mutableListOf<(Boolean, Long) -> Unit>()
        val sender = ReliableFrameSender(writeFrame = { frame, completion ->
            writes += frame
            completions += completion
            true
        })
        sender.setConnected(true, 0)
        sender.enqueue(MessageType.SettingsSet, byteArrayOf(1), 0)

        sender.service(10_000)
        assertEquals(1, writes.size)
        completions.single()(true, 10_000)
        sender.service(11_999)
        assertEquals(1, writes.size)
        sender.service(12_000)
        assertEquals(2, writes.size)
        assertEquals(writes[0].sequence, writes[1].sequence)
    }

    @Test
    fun completionFailureWaitsTwoSecondsThenRetriesSameSequence() {
        val writes = mutableListOf<Frame>()
        val completions = mutableListOf<(Boolean, Long) -> Unit>()
        val failures = mutableListOf<ReliableSendFailure>()
        val sender = ReliableFrameSender(
            writeFrame = { frame, completion ->
                writes += frame
                completions += completion
                true
            },
            onFailure = failures::add,
        )
        sender.setConnected(true, 0)
        sender.enqueue(MessageType.CalendarUpdate, byteArrayOf(2), 0)
        val sequence = writes.single().sequence

        completions.single()(false, 100)
        assertTrue(failures.single() is ReliableSendFailure.WriteRejected)
        sender.service(2_099)
        assertEquals(1, writes.size)
        sender.service(2_100)
        assertEquals(2, writes.size)
        assertEquals(sequence, writes.last().sequence)
    }

    @Test
    fun ackBeforeBatchCompletionSafelyClearsInflight() {
        val completions = mutableListOf<(Boolean, Long) -> Unit>()
        val sender = ReliableFrameSender(writeFrame = { _, completion ->
            completions += completion
            true
        })
        sender.setConnected(true, 0)
        sender.enqueue(MessageType.FindWatch, byteArrayOf(1, 1), 0)
        val sequence = sender.inFlightSequence!!

        assertTrue(sender.onAck(sequence, 1))
        assertEquals(null, sender.inFlightSequence)
        completions.single()(true, 2)
        assertEquals(null, sender.inFlightSequence)
    }

    private fun summary(key: String, posted: Long) =
        PhoneNotificationSummary(
            packageName = "com.example",
            appName = "Example",
            title = key,
            body = "body",
            postedEpochMillis = posted,
            key = key,
        )
}
