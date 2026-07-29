package com.fireflyos.companion.data

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsSyncTest {
    @Test
    fun resolvesEachSettingRevisionIndependently() {
        val local = SettingsSnapshot(
            mapOf(
                SettingKind.Alarm to setting(9, 900, 1),
                SettingKind.Brightness to setting(4, 400, 40),
                SettingKind.Volume to setting(2, 200, 20),
                SettingKind.Theme to setting(7, 700, 7),
            ),
        )
        val remote = SettingsSnapshot(
            mapOf(
                SettingKind.Alarm to setting(8, 800, 2),
                SettingKind.Brightness to setting(5, 500, 50),
                SettingKind.Volume to setting(1, 100, 10),
                SettingKind.Theme to setting(8, 650, 8),
            ),
        )

        val resolved = SettingsConflictResolver.resolve(local, remote)

        assertEquals(1, resolved[SettingKind.Alarm]!!.value.single().toInt())
        assertEquals(50, resolved[SettingKind.Brightness]!!.value.single().toInt())
        assertEquals(20, resolved[SettingKind.Volume]!!.value.single().toInt())
        assertEquals(7, resolved[SettingKind.Theme]!!.value.single().toInt())
    }

    @Test
    fun sameTimestampUsesUint32SerialOrderIncludingWrap() {
        val beforeWrap = setting(0xFFFF_FFFFL, 1000, 1)
        val afterWrap = setting(0, 1000, 2)

        assertEquals(
            2,
            SettingsConflictResolver.pick(beforeWrap, afterWrap)
                .value.single().toInt(),
        )
        assertTrue(SettingsConflictResolver.isNewerRevision(0, 0xFFFF_FFFFL))
    }

    @Test
    fun identicalVersionTieBreakIsStableRegardlessOfArgumentOrder() {
        val low = VersionedSetting(4, 1000, byteArrayOf(1, 9))
        val high = VersionedSetting(4, 1000, byteArrayOf(2, 0))

        assertArrayEquals(
            byteArrayOf(2, 0),
            SettingsConflictResolver.pick(low, high).value,
        )
        assertArrayEquals(
            byteArrayOf(2, 0),
            SettingsConflictResolver.pick(high, low).value,
        )
    }

    @Test
    fun codecRoundTripsFourIndependentEntriesAndRejectsDuplicates() {
        val original = SettingsSnapshot(
            SettingKind.entries.associateWith { kind ->
                setting(kind.wireId.toLong(), 1_800_000_000L + kind.wireId, kind.wireId)
            },
        )

        val encoded = SettingsSyncCodec.encode(original)
        val decoded = SettingsSyncCodec.decode(encoded)

        assertTrue(encoded.size <= 1024)
        assertNotNull(decoded)
        SettingKind.entries.forEach { kind ->
            val expected = original[kind]!!
            val actual = decoded!![kind]!!
            assertEquals(expected.revision, actual.revision)
            assertEquals(expected.changedAtEpochMillis, actual.changedAtEpochMillis)
            assertArrayEquals(expected.value, actual.value)
        }
        val duplicate = encoded + encoded.copyOfRange(2, encoded.size)
        duplicate[1] = 8
        assertEquals(null, SettingsSyncCodec.decode(duplicate))
    }

    @Test
    fun fixedStoreVersionsFourKindsIndependentlyAndRestoresSnapshot() {
        val store = SettingsStateStore()
        SettingKind.entries.forEachIndexed { index, kind ->
            store.recordLocal(kind, byteArrayOf((index + 1).toByte()), 1000L + index)
        }
        store.recordLocal(SettingKind.Brightness, byteArrayOf(90.toByte()), 2000)

        val snapshot = store.snapshot()
        assertEquals(4, snapshot.asMap().size)
        assertEquals(2, snapshot[SettingKind.Brightness]!!.revision)
        assertEquals(1, snapshot[SettingKind.Volume]!!.revision)
        assertEquals(90.toByte(), snapshot[SettingKind.Brightness]!!.value.single())

        val restored = SettingsStateStore(snapshot)
        assertEquals(90.toByte(), restored.snapshot()[SettingKind.Brightness]!!.value.single())
        assertEquals(4, restored.snapshot().asMap().size)
    }

    @Test
    fun storeReconcileKeepsLocalNewerAndAcceptsRemoteNewerPerKind() {
        val store = SettingsStateStore(
            SettingsSnapshot(
                mapOf(
                    SettingKind.Brightness to setting(2, 2000, 80),
                    SettingKind.Volume to setting(9, 1000, 20),
                ),
            ),
        )
        val resolved = store.reconcile(
            SettingsSnapshot(
                mapOf(
                    SettingKind.Brightness to setting(99, 1999, 10),
                    SettingKind.Volume to setting(1, 1001, 70),
                ),
            ),
        )!!

        assertEquals(80, resolved[SettingKind.Brightness]!!.value.single().toInt())
        assertEquals(70, resolved[SettingKind.Volume]!!.value.single().toInt())
        assertEquals(2, resolved.asMap().size)
    }

    @Test
    fun privateSnapshotEncodingRestartsWithAllFourKinds() {
        val blobs = FakeSettingsSnapshotBlobStore()
        val persistence = Base64SettingsSnapshotPersistence(blobs)
        val firstProcess = SettingsStateStore(
            initial = persistence.load(),
            persistence = persistence,
        )
        SettingKind.entries.forEachIndexed { index, kind ->
            assertNotNull(
                firstProcess.recordLocal(
                    kind,
                    byteArrayOf((index + 10).toByte()),
                    10_000L + index,
                ),
            )
        }

        val restarted = SettingsStateStore(
            initial = Base64SettingsSnapshotPersistence(blobs).load(),
            persistence = Base64SettingsSnapshotPersistence(blobs),
        ).snapshot()

        assertEquals(4, restarted.asMap().size)
        SettingKind.entries.forEachIndexed { index, kind ->
            assertEquals(1L, restarted[kind]!!.revision)
            assertEquals(10_000L + index, restarted[kind]!!.changedAtEpochMillis)
            assertArrayEquals(
                byteArrayOf((index + 10).toByte()),
                restarted[kind]!!.value,
            )
        }
    }

    @Test
    fun corruptPersistedBlobIsIgnoredAndCleared() {
        val blobs = FakeSettingsSnapshotBlobStore(blob = "not valid base64")

        val loaded = Base64SettingsSnapshotPersistence(blobs).load()

        assertTrue(loaded.asMap().isEmpty())
        assertEquals(null, blobs.blob)
        assertEquals(1, blobs.clearCount)
    }

    @Test
    fun persistenceFailureNeverAdvancesMemoryForLocalOrReconcile() {
        val initial = SettingsSnapshot(
            mapOf(SettingKind.Volume to setting(4, 4000, 40)),
        )
        val blobs = FakeSettingsSnapshotBlobStore(writeSucceeds = false)
        val store = SettingsStateStore(
            initial = initial,
            persistence = Base64SettingsSnapshotPersistence(blobs),
        )

        assertNull(store.recordLocal(SettingKind.Volume, byteArrayOf(70), 5000))
        assertEquals(4L, store.snapshot()[SettingKind.Volume]!!.revision)
        assertEquals(40, store.snapshot()[SettingKind.Volume]!!.value.single().toInt())

        assertNull(
            store.reconcile(
                SettingsSnapshot(
                    mapOf(SettingKind.Volume to setting(5, 6000, 80)),
                ),
            ),
        )
        assertEquals(4L, store.snapshot()[SettingKind.Volume]!!.revision)
        assertEquals(40, store.snapshot()[SettingKind.Volume]!!.value.single().toInt())
    }

    private fun setting(revision: Long, changedAt: Long, value: Int) =
        VersionedSetting(revision, changedAt, byteArrayOf(value.toByte()))

    private class FakeSettingsSnapshotBlobStore(
        var blob: String? = null,
        var writeSucceeds: Boolean = true,
    ) : SettingsSnapshotBlobStore {
        var clearCount = 0

        override fun read(): String? = blob

        override fun write(value: String): Boolean {
            if (!writeSucceeds) return false
            blob = value
            return true
        }

        override fun clear(): Boolean {
            blob = null
            clearCount += 1
            return true
        }
    }
}
