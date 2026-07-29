package com.fireflyos.companion.find

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioManager
import android.media.ToneGenerator
import android.os.Build
import android.os.Handler
import android.os.Looper

class AndroidFindPhoneController(
    private val context: Context,
    private val appInForeground: () -> Boolean,
) {
    private var tone: ToneGenerator? = null
    private val handler = Handler(Looper.getMainLooper())
    private val releaseTone = Runnable {
        tone?.stopTone()
        tone?.release()
        tone = null
    }

    fun trigger(): FindPhoneRoute {
        val route = FindDevicePolicy.phoneRoute(
            appInForeground = appInForeground(),
            notificationPermissionAvailable = notificationPermissionAvailable(),
        )
        handler.removeCallbacks(releaseTone)
        releaseTone.run()
        when (route) {
            FindPhoneRoute.Foreground -> {
                tone = ToneGenerator(AudioManager.STREAM_ALARM, 100).also {
                    it.startTone(
                        ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD,
                        TONE_DURATION_MILLIS.toInt(),
                    )
                }
                handler.postDelayed(releaseTone, TONE_DURATION_MILLIS)
            }
            FindPhoneRoute.Notification -> postFindNotification()
            FindPhoneRoute.Unavailable -> Unit
        }
        return route
    }

    fun stop() {
        handler.removeCallbacks(releaseTone)
        releaseTone.run()
    }

    private fun notificationPermissionAvailable(): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) ==
                PackageManager.PERMISSION_GRANTED
        } else {
            true
        }

    private fun postFindNotification() {
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        val channelId = "firefly_find_phone"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(
                    channelId,
                    "Find phone",
                    NotificationManager.IMPORTANCE_HIGH,
                ),
            )
        }
        val notification = android.app.Notification.Builder(context, channelId)
            .setSmallIcon(android.R.drawable.ic_lock_idle_alarm)
            .setContentTitle("FireflyOS is finding this phone")
            .setContentText("Open the companion app to stop the alert.")
            .setAutoCancel(true)
            .build()
        try {
            manager.notify(FIND_NOTIFICATION_ID, notification)
        } catch (_: SecurityException) {
            // Permission may be revoked between route selection and posting.
        }
    }

    companion object {
        private const val FIND_NOTIFICATION_ID = 0x4646
        private const val TONE_DURATION_MILLIS = 30_000L
    }
}
