package com.fireflyos.companion.data

import android.content.Context
import java.util.Base64

interface SettingsSnapshotBlobStore {
    fun read(): String?
    fun write(value: String): Boolean
    fun clear(): Boolean
}

class Base64SettingsSnapshotPersistence(
    private val blobs: SettingsSnapshotBlobStore,
) : SettingsSnapshotPersistence {
    override fun load(): SettingsSnapshot {
        val encoded = blobs.read() ?: return SettingsSnapshot()
        val snapshot = runCatching {
            val bytes = Base64.getDecoder().decode(encoded)
            SettingsSyncCodec.decode(bytes)
        }.getOrNull()
        if (snapshot != null) return snapshot
        blobs.clear()
        return SettingsSnapshot()
    }

    override fun save(snapshot: SettingsSnapshot): Boolean {
        val encoded = runCatching {
            Base64.getEncoder().withoutPadding().encodeToString(
                SettingsSyncCodec.encode(snapshot),
            )
        }.getOrNull() ?: return false
        return blobs.write(encoded)
    }
}

class PrivateSharedPreferencesSettingsSnapshotStore(
    context: Context,
) : SettingsSnapshotBlobStore {
    companion object {
        private const val PREFERENCES_NAME = "firefly_settings_private"
        private const val SNAPSHOT_KEY = "settings_snapshot_v1"
    }

    private val preferences = context.applicationContext.getSharedPreferences(
        PREFERENCES_NAME,
        Context.MODE_PRIVATE,
    )

    override fun read(): String? = preferences.getString(SNAPSHOT_KEY, null)

    override fun write(value: String): Boolean =
        preferences.edit().putString(SNAPSHOT_KEY, value).commit()

    override fun clear(): Boolean =
        preferences.edit().remove(SNAPSHOT_KEY).commit()
}
