package com.fireflyos.companion.media

import android.content.ComponentName
import android.content.Context
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.provider.Settings
import com.fireflyos.companion.notifications.PhoneNotificationListener

class AndroidMediaSessionGateway(private val context: Context) {
    private val manager = context.getSystemService(MediaSessionManager::class.java)
    private val listenerComponent = ComponentName(
        context,
        PhoneNotificationListener::class.java,
    )

    fun dispatcher(): MediaCommandDispatcher = MediaCommandDispatcher(
        notificationAccessAvailable = ::notificationAccessAvailable,
        activeSession = ::activeSession,
    )

    fun notificationAccessAvailable(): Boolean {
        val enabled = Settings.Secure.getString(
            context.contentResolver,
            "enabled_notification_listeners",
        ).orEmpty()
        return enabled.split(':').any { flattened ->
            ComponentName.unflattenFromString(flattened)?.packageName ==
                context.packageName
        }
    }

    @Throws(SecurityException::class)
    fun activeSession(): MediaSessionTarget? {
        if (!notificationAccessAvailable()) return null
        return manager?.getActiveSessions(listenerComponent)
            ?.firstOrNull()
            ?.let(::AndroidMediaSessionTarget)
    }

    fun currentAvailabilityError(): MediaDispatchError? = when {
        !notificationAccessAvailable() ->
            MediaDispatchError.NotificationAccessRequired
        activeSession() == null ->
            MediaDispatchError.NoActiveSession
        else -> null
    }

    private class AndroidMediaSessionTarget(
        private val controller: MediaController,
    ) : MediaSessionTarget {
        override fun playPause() {
            if (controller.playbackState?.state == PlaybackState.STATE_PLAYING) {
                controller.transportControls.pause()
            } else {
                controller.transportControls.play()
            }
        }

        override fun previous() {
            controller.transportControls.skipToPrevious()
        }

        override fun next() {
            controller.transportControls.skipToNext()
        }

        override fun setVolume(percent: Int) {
            val info = controller.playbackInfo ?: return
            val target = (info.maxVolume * percent.coerceIn(0, 100)) / 100
            controller.setVolumeTo(target, 0)
        }
    }
}
