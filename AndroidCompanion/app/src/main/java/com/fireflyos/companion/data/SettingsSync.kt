package com.fireflyos.companion.data

import java.nio.ByteBuffer
import java.nio.ByteOrder

enum class SettingKind(val wireId: Int) {
    Alarm(1),
    Brightness(2),
    Volume(3),
    Theme(4);

    companion object {
        fun fromWireId(value: Int): SettingKind? =
            entries.firstOrNull { it.wireId == value }
    }
}

data class VersionedSetting(
    val revision: Long,
    val changedAtEpochMillis: Long,
    val value: ByteArray,
) {
    init {
        require(revision in 0..UINT32_MAX)
        require(value.size <= SettingsSyncCodec.MAX_VALUE_BYTES)
    }

    companion object {
        const val UINT32_MAX = 0xFFFF_FFFFL
    }
}

class SettingsSnapshot(entries: Map<SettingKind, VersionedSetting> = emptyMap()) {
    private val entries = entries.toMap()

    operator fun get(kind: SettingKind): VersionedSetting? = entries[kind]
    fun asMap(): Map<SettingKind, VersionedSetting> = entries
}

interface SettingsSnapshotPersistence {
    fun load(): SettingsSnapshot
    fun save(snapshot: SettingsSnapshot): Boolean
}

private object NoOpSettingsSnapshotPersistence : SettingsSnapshotPersistence {
    override fun load(): SettingsSnapshot = SettingsSnapshot()
    override fun save(snapshot: SettingsSnapshot): Boolean = true
}

class SettingsStateStore(
    initial: SettingsSnapshot = SettingsSnapshot(),
    private val persistence: SettingsSnapshotPersistence =
        NoOpSettingsSnapshotPersistence,
) {
    private val entries = arrayOfNulls<VersionedSetting>(SettingKind.entries.size)

    init {
        replaceInMemory(initial)
    }

    @Synchronized
    fun snapshot(): SettingsSnapshot =
        SettingsSnapshot(
            buildMap {
                SettingKind.entries.forEachIndexed { index, kind ->
                    this@SettingsStateStore.entries[index]?.let { setting ->
                        put(kind, setting.copy(value = setting.value.copyOf()))
                    }
                }
            },
        )

    @Synchronized
    fun recordLocal(
        kind: SettingKind,
        value: ByteArray,
        changedAtEpochMillis: Long,
    ): VersionedSetting? {
        require(value.size <= SettingsSyncCodec.MAX_VALUE_BYTES)
        val current = entries[kind.ordinal]
        val nextRevision = if (current == null) {
            1L
        } else {
            (current.revision + 1L) and VersionedSetting.UINT32_MAX
        }
        val next = VersionedSetting(
            revision = nextRevision,
            changedAtEpochMillis = maxOf(
                changedAtEpochMillis,
                current?.changedAtEpochMillis ?: changedAtEpochMillis,
            ),
            value = value.copyOf(),
        )
        val candidate = snapshot().asMap().toMutableMap()
        candidate[kind] = next
        if (!persistence.save(SettingsSnapshot(candidate))) return null
        entries[kind.ordinal] = next
        return next
    }

    @Synchronized
    fun reconcile(remote: SettingsSnapshot): SettingsSnapshot? {
        val resolved = SettingsConflictResolver.resolve(snapshot(), remote)
        if (!persistence.save(resolved)) return null
        replaceInMemory(resolved)
        return snapshot()
    }

    private fun replaceInMemory(snapshot: SettingsSnapshot) {
        entries.fill(null)
        snapshot.asMap().forEach { (kind, setting) ->
            entries[kind.ordinal] = setting.copy(value = setting.value.copyOf())
        }
    }
}

object SettingsConflictResolver {
    private const val UINT32_HALF_RANGE = 0x8000_0000L
    private const val UINT32_MASK = 0xFFFF_FFFFL

    /**
     * The later explicit-operation timestamp wins. If timestamps are equal,
     * RFC-1982 uint32 serial order wins. Equal or exactly half-range revisions
     * fall back to unsigned lexicographic value order, making ties stable and
     * independent of argument order.
     */
    fun pick(left: VersionedSetting, right: VersionedSetting): VersionedSetting {
        if (left.changedAtEpochMillis != right.changedAtEpochMillis) {
            return if (left.changedAtEpochMillis > right.changedAtEpochMillis) left else right
        }
        if (isNewerRevision(left.revision, right.revision)) return left
        if (isNewerRevision(right.revision, left.revision)) return right
        return if (compareUnsigned(left.value, right.value) >= 0) left else right
    }

    fun resolve(left: SettingsSnapshot, right: SettingsSnapshot): SettingsSnapshot {
        val resolved = mutableMapOf<SettingKind, VersionedSetting>()
        SettingKind.entries.forEach { kind ->
            val local = left[kind]
            val remote = right[kind]
            resolved[kind] = when {
                local == null -> remote
                remote == null -> local
                else -> pick(local, remote)
            } ?: return@forEach
        }
        return SettingsSnapshot(resolved)
    }

    fun isNewerRevision(candidate: Long, current: Long): Boolean {
        if (candidate !in 0..UINT32_MASK || current !in 0..UINT32_MASK) return false
        val delta = (candidate - current) and UINT32_MASK
        return delta != 0L && delta < UINT32_HALF_RANGE
    }

    private fun compareUnsigned(left: ByteArray, right: ByteArray): Int {
        val shared = minOf(left.size, right.size)
        repeat(shared) { index ->
            val comparison = (left[index].toInt() and 0xFF)
                .compareTo(right[index].toInt() and 0xFF)
            if (comparison != 0) return comparison
        }
        return left.size.compareTo(right.size)
    }
}

object SettingsSyncCodec {
    const val MAX_VALUE_BYTES = 256
    private const val SCHEMA = 1
    private const val HEADER_BYTES = 2
    private const val ENTRY_HEADER_BYTES = 15

    fun encode(snapshot: SettingsSnapshot): ByteArray {
        val ordered = snapshot.asMap().entries.sortedBy { it.key.wireId }
        require(ordered.size <= SettingKind.entries.size)
        val size = HEADER_BYTES + ordered.sumOf { ENTRY_HEADER_BYTES + it.value.value.size }
        require(size <= 1024)
        val buffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(SCHEMA.toByte())
        buffer.put(ordered.size.toByte())
        ordered.forEach { (kind, setting) ->
            require(setting.value.size <= MAX_VALUE_BYTES)
            buffer.put(kind.wireId.toByte())
            buffer.putInt(setting.revision.toInt())
            buffer.putLong(setting.changedAtEpochMillis)
            buffer.putShort(setting.value.size.toShort())
            buffer.put(setting.value)
        }
        return buffer.array()
    }

    fun decode(payload: ByteArray): SettingsSnapshot? {
        if (payload.size !in HEADER_BYTES..1024) return null
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        if ((buffer.get().toInt() and 0xFF) != SCHEMA) return null
        val count = buffer.get().toInt() and 0xFF
        if (count > SettingKind.entries.size) return null
        val decoded = mutableMapOf<SettingKind, VersionedSetting>()
        repeat(count) {
            if (buffer.remaining() < ENTRY_HEADER_BYTES) return null
            val kind = SettingKind.fromWireId(buffer.get().toInt() and 0xFF) ?: return null
            if (decoded.containsKey(kind)) return null
            val revision = buffer.int.toLong() and VersionedSetting.UINT32_MAX
            val changedAt = buffer.long
            val length = buffer.short.toInt() and 0xFFFF
            if (length > MAX_VALUE_BYTES || buffer.remaining() < length) return null
            val value = ByteArray(length)
            buffer.get(value)
            decoded[kind] = VersionedSetting(revision, changedAt, value)
        }
        if (buffer.hasRemaining()) return null
        return SettingsSnapshot(decoded)
    }
}
