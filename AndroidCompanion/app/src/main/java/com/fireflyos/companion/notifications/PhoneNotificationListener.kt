package com.fireflyos.companion.notifications

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import com.fireflyos.companion.ble.MessageType
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

data class PhoneNotificationSummary(
    val packageName: String,
    val appName: String,
    val title: String,
    val body: String,
    val postedEpochMillis: Long,
    val key: String,
)

data class NotificationOutboundMessage(
    val type: MessageType,
    val payload: ByteArray,
)

data class NotificationSyncFailure(val type: MessageType)

object Utf8Boundary {
    fun truncate(value: String, maxBytes: Int): String {
        if (maxBytes <= 0 || value.isEmpty()) return ""
        var charIndex = 0
        var byteCount = 0
        while (charIndex < value.length) {
            val codePoint = Character.codePointAt(value, charIndex)
            val encodedLength = String(Character.toChars(codePoint))
                .toByteArray(Charsets.UTF_8).size
            if (byteCount + encodedLength > maxBytes) break
            byteCount += encodedLength
            charIndex += Character.charCount(codePoint)
        }
        return value.substring(0, charIndex)
    }
}

object NotificationPayloadCodec {
    const val MAX_TITLE_BYTES = 128
    const val MAX_BODY_BYTES = 256
    const val MAX_ACTIVE_SUMMARIES = 20
    const val MAX_PUSH_PAYLOAD_BYTES = 561

    private const val SCHEMA = 1
    private const val MAX_KEY_BYTES = 32
    private const val MAX_PACKAGE_BYTES = 95
    private const val MAX_APP_NAME_BYTES = 31
    private const val PUSH_HEADER_BYTES = 19

    fun stableKey(platformKey: String): String {
        val digest = MessageDigest.getInstance("SHA-256")
            .digest(platformKey.toByteArray(Charsets.UTF_8))
        return buildString(32) {
            repeat(16) { index ->
                append("%02x".format(digest[index].toInt() and 0xFF))
            }
        }
    }

    fun encodePush(summary: PhoneNotificationSummary): ByteArray {
        val key = Utf8Boundary.truncate(summary.key, MAX_KEY_BYTES)
            .toByteArray(Charsets.UTF_8)
        require(key.isNotEmpty())
        val packageName = Utf8Boundary.truncate(summary.packageName, MAX_PACKAGE_BYTES)
            .toByteArray(Charsets.UTF_8)
        val appName = Utf8Boundary.truncate(summary.appName, MAX_APP_NAME_BYTES)
            .toByteArray(Charsets.UTF_8)
        val title = Utf8Boundary.truncate(summary.title, MAX_TITLE_BYTES)
            .toByteArray(Charsets.UTF_8)
        val body = Utf8Boundary.truncate(summary.body, MAX_BODY_BYTES)
            .toByteArray(Charsets.UTF_8)
        return ByteBuffer.allocate(
            PUSH_HEADER_BYTES + key.size + packageName.size + appName.size +
                title.size + body.size,
        ).order(ByteOrder.LITTLE_ENDIAN)
            .put(SCHEMA.toByte())
            .putShort(key.size.toShort())
            .putShort(packageName.size.toShort())
            .putShort(appName.size.toShort())
            .putShort(title.size.toShort())
            .putShort(body.size.toShort())
            .putLong(summary.postedEpochMillis)
            .put(key)
            .put(packageName)
            .put(appName)
            .put(title)
            .put(body)
            .array()
    }

    fun encodeDismiss(key: String): ByteArray {
        val encoded = Utf8Boundary.truncate(key, MAX_KEY_BYTES)
            .toByteArray(Charsets.UTF_8)
        return ByteBuffer.allocate(3 + encoded.size)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(SCHEMA.toByte())
            .putShort(encoded.size.toShort())
            .put(encoded)
            .array()
    }

    fun snapshotReset(): ByteArray = encodeDismiss("")

    fun isSnapshotReset(payload: ByteArray): Boolean =
        payload.size == 3 &&
            payload[0].toInt() == SCHEMA &&
            payload[1].toInt() == 0 &&
            payload[2].toInt() == 0
}

class NotificationSyncCoordinator {
    private val active = arrayOfNulls<PhoneNotificationSummary>(
        NotificationPayloadCodec.MAX_ACTIVE_SUMMARIES,
    )
    private var count = 0
    private var connected = false
    private var sender: ((NotificationOutboundMessage) -> Boolean)? = null
    var lastSendFailure: NotificationSyncFailure? = null
        private set

    @Synchronized
    fun setConnected(
        value: Boolean,
        frameSender: (NotificationOutboundMessage) -> Boolean,
    ) {
        sender = frameSender
        connected = value
        if (connected) {
            lastSendFailure = null
            replayCurrent()
        }
    }

    @Synchronized
    fun replaceCurrent(summaries: Array<PhoneNotificationSummary>) {
        active.fill(null)
        count = 0
        summaries.forEach { upsertStored(it) }
        if (connected) replayCurrent()
    }

    @Synchronized
    fun upsert(summary: PhoneNotificationSummary) {
        upsertStored(summary)
        if (connected) sendPush(summary)
    }

    @Synchronized
    fun dismiss(key: String) {
        val index = findIndex(key)
        if (index >= 0) {
            for (move in index until count - 1) active[move] = active[move + 1]
            active[--count] = null
        }
        if (connected) {
            sendMessage(
                NotificationOutboundMessage(
                    MessageType.NotificationDismiss,
                    NotificationPayloadCodec.encodeDismiss(key),
                ),
            )
        }
    }

    @Synchronized
    fun detach() {
        connected = false
        sender = null
    }

    private fun replayCurrent() {
        if (!sendMessage(
            NotificationOutboundMessage(
                MessageType.NotificationDismiss,
                NotificationPayloadCodec.snapshotReset(),
            ),
        )) {
            return
        }
        repeat(count) { index ->
            val summary = active[index] ?: return@repeat
            if (!sendPush(summary)) return
        }
    }

    private fun sendPush(summary: PhoneNotificationSummary): Boolean =
        sendMessage(
            NotificationOutboundMessage(
                MessageType.NotificationPush,
                NotificationPayloadCodec.encodePush(summary),
            ),
        )

    private fun sendMessage(message: NotificationOutboundMessage): Boolean {
        val sent = sender?.invoke(message) == true
        if (!sent) lastSendFailure = NotificationSyncFailure(message.type)
        return sent
    }

    private fun upsertStored(summary: PhoneNotificationSummary) {
        val existing = findIndex(summary.key)
        if (existing >= 0) {
            active[existing] = summary
        } else if (count < active.size) {
            active[count++] = summary
        } else {
            var oldest = 0
            for (index in 1 until count) {
                if (active[index]!!.postedEpochMillis <
                    active[oldest]!!.postedEpochMillis
                ) {
                    oldest = index
                }
            }
            active[oldest] = summary
        }
    }

    private fun findIndex(key: String): Int {
        repeat(count) { index ->
            if (active[index]?.key == key) return index
        }
        return -1
    }
}

object NotificationSyncBridge {
    private val coordinator = NotificationSyncCoordinator()
    private var sender: ((NotificationOutboundMessage) -> Boolean)? = null

    @Synchronized
    fun attach(frameSender: (NotificationOutboundMessage) -> Boolean) {
        sender = frameSender
    }

    @Synchronized
    fun setConnected(connected: Boolean) {
        sender?.let { coordinator.setConnected(connected, it) }
    }

    fun replaceCurrent(summaries: Array<PhoneNotificationSummary>) =
        coordinator.replaceCurrent(summaries)

    fun upsert(summary: PhoneNotificationSummary) = coordinator.upsert(summary)

    fun dismiss(key: String) = coordinator.dismiss(key)

    @Synchronized
    fun detach() {
        coordinator.detach()
        sender = null
    }
}

class PhoneNotificationListener : NotificationListenerService() {
    override fun onListenerConnected() {
        super.onListenerConnected()
        val summaries = arrayOfNulls<PhoneNotificationSummary>(
            NotificationPayloadCodec.MAX_ACTIVE_SUMMARIES,
        )
        var count = 0
        for (sbn in activeNotifications) {
            val summary = summaryOf(sbn) ?: continue
            if (count < summaries.size) {
                summaries[count++] = summary
            } else {
                var oldest = 0
                for (index in 1 until count) {
                    if (summaries[index]!!.postedEpochMillis <
                        summaries[oldest]!!.postedEpochMillis
                    ) {
                        oldest = index
                    }
                }
                if (summary.postedEpochMillis > summaries[oldest]!!.postedEpochMillis) {
                    summaries[oldest] = summary
                }
            }
        }
        NotificationSyncBridge.replaceCurrent(
            Array(count) { index -> summaries[index]!! },
        )
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        sbn?.let(::summaryOf)?.let(NotificationSyncBridge::upsert)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        val rawKey = sbn?.key ?: return
        NotificationSyncBridge.dismiss(NotificationPayloadCodec.stableKey(rawKey))
    }

    private fun summaryOf(sbn: StatusBarNotification): PhoneNotificationSummary? {
        if (sbn.packageName == packageName) return null
        val extras = sbn.notification.extras
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
        val body = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString().orEmpty()
        val applicationLabel = try {
            packageManager.getApplicationLabel(
                packageManager.getApplicationInfo(sbn.packageName, 0),
            ).toString()
        } catch (_: Exception) {
            sbn.packageName
        }
        return PhoneNotificationSummary(
            packageName = Utf8Boundary.truncate(sbn.packageName, 95),
            appName = Utf8Boundary.truncate(applicationLabel, 31),
            title = Utf8Boundary.truncate(title, NotificationPayloadCodec.MAX_TITLE_BYTES),
            body = Utf8Boundary.truncate(body, NotificationPayloadCodec.MAX_BODY_BYTES),
            postedEpochMillis = sbn.postTime,
            key = NotificationPayloadCodec.stableKey(sbn.key),
        )
    }
}
