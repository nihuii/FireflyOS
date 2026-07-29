package com.fireflyos.companion.notifications

import com.fireflyos.companion.ble.MessageType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NotificationPayloadCodecTest {
    @Test
    fun chineseTextTruncatesOnlyAtUtf8CodePointBoundary() {
        val truncated = Utf8Boundary.truncate("萤".repeat(50) + "A", 128)
        val encoded = truncated.toByteArray(Charsets.UTF_8)

        assertTrue(encoded.size <= 128)
        assertEquals(truncated, encoded.toString(Charsets.UTF_8))
        assertFalse(truncated.contains('\uFFFD'))
        assertEquals("萤".repeat(42), truncated)
    }

    @Test
    fun pushPayloadKeepsOnlyBoundedSummaryFields() {
        val payload = NotificationPayloadCodec.encodePush(
            PhoneNotificationSummary(
                packageName = "com.example.chat",
                appName = "聊天",
                title = "题".repeat(80),
                body = "正文".repeat(100),
                postedEpochMillis = 1_765_000_000_123L,
                key = NotificationPayloadCodec.stableKey("0|com.example.chat|42|null|1000"),
            ),
        )
        val decoded = decodePush(payload)

        assertEquals("com.example.chat", decoded.packageName)
        assertEquals("聊天", decoded.appName)
        assertEquals("题".repeat(42), decoded.title)
        assertTrue(decoded.title.toByteArray(Charsets.UTF_8).size <= 128)
        assertTrue(decoded.body.toByteArray(Charsets.UTF_8).size <= 256)
        assertEquals(1_765_000_000_123L, decoded.postedEpochMillis)
        assertEquals(32, decoded.key.length)
        assertTrue(payload.size <= NotificationPayloadCodec.MAX_PUSH_PAYLOAD_BYTES)
    }

    @Test
    fun reconnectReplaysOnlyCurrentEffectiveSummaries() {
        val sent = mutableListOf<NotificationOutboundMessage>()
        val coordinator = NotificationSyncCoordinator()
        val old = summary("old", 100)
        val current = summary("current", 200)

        coordinator.setConnected(false) { message ->
            sent += message
            true
        }
        coordinator.upsert(old)
        coordinator.upsert(current)
        coordinator.dismiss(old.key)
        assertTrue(sent.isEmpty())

        coordinator.setConnected(true) { message ->
            sent += message
            true
        }

        assertEquals(2, sent.size)
        assertEquals(MessageType.NotificationDismiss, sent[0].type)
        assertTrue(NotificationPayloadCodec.isSnapshotReset(sent[0].payload))
        assertEquals(MessageType.NotificationPush, sent[1].type)
        assertEquals(current.key, decodePush(sent[1].payload).key)
    }

    @Test
    fun activeSnapshotIsBoundedToTwentyNewestEntries() {
        val sent = mutableListOf<NotificationOutboundMessage>()
        val coordinator = NotificationSyncCoordinator()
        repeat(23) { index -> coordinator.upsert(summary("key-$index", index.toLong())) }

        coordinator.setConnected(true) { message ->
            sent += message
            true
        }

        assertEquals(21, sent.size)
        val pushedKeys = sent.drop(1).map { decodePush(it.payload).key }
        assertFalse(pushedKeys.contains("key-0"))
        assertFalse(pushedKeys.contains("key-1"))
        assertFalse(pushedKeys.contains("key-2"))
        assertTrue(pushedKeys.contains("key-22"))
    }

    @Test
    fun replayStopsAndExposesSenderRejection() {
        val coordinator = NotificationSyncCoordinator()
        coordinator.upsert(summary("one", 1))
        coordinator.upsert(summary("two", 2))
        var calls = 0

        coordinator.setConnected(true) {
            calls += 1
            false
        }

        assertEquals(1, calls)
        assertEquals(
            MessageType.NotificationDismiss,
            coordinator.lastSendFailure?.type,
        )
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

    private fun decodePush(payload: ByteArray): PhoneNotificationSummary {
        var offset = 1
        fun readUnsignedShort(): Int {
            val value = (payload[offset].toInt() and 0xFF) or
                ((payload[offset + 1].toInt() and 0xFF) shl 8)
            offset += 2
            return value
        }
        fun readLong(): Long {
            var value = 0L
            repeat(8) { shift ->
                value = value or ((payload[offset++].toLong() and 0xFF) shl (shift * 8))
            }
            return value
        }
        val keyLength = readUnsignedShort()
        val packageLength = readUnsignedShort()
        val appLength = readUnsignedShort()
        val titleLength = readUnsignedShort()
        val bodyLength = readUnsignedShort()
        val posted = readLong()
        fun readText(length: Int): String {
            val text = payload.copyOfRange(offset, offset + length).toString(Charsets.UTF_8)
            offset += length
            return text
        }
        val key = readText(keyLength)
        val packageName = readText(packageLength)
        val appName = readText(appLength)
        val title = readText(titleLength)
        val body = readText(bodyLength)
        assertEquals(payload.size, offset)
        return PhoneNotificationSummary(
            packageName = packageName,
            appName = appName,
            title = title,
            body = body,
            postedEpochMillis = posted,
            key = key,
        )
    }
}
