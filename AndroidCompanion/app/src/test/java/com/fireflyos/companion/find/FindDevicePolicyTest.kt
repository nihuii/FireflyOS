package com.fireflyos.companion.find

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FindDevicePolicyTest {
    @Test
    fun normalFindWatchRunsThirtySecondsWithSoundAndLight() {
        val plan = FindDevicePolicy.watchPlan(batteryPercent = 50)

        assertEquals(30_000L, plan.durationMillis)
        assertTrue(plan.flashScreen)
        assertTrue(plan.playAudio)
    }

    @Test
    fun extremelyLowBatteryFlashesFiveSecondsWithoutAudio() {
        val plan = FindDevicePolicy.watchPlan(
            batteryPercent = FindDevicePolicy.EXTREMELY_LOW_BATTERY_PERCENT,
        )

        assertEquals(5_000L, plan.durationMillis)
        assertTrue(plan.flashScreen)
        assertFalse(plan.playAudio)
    }

    @Test
    fun findWatchCanBeCancelledAndExpiresAtPlannedBoundary() {
        val service = FindWatchStateService()
        service.start(nowMillis = 100, batteryPercent = 80)
        assertTrue(service.snapshot(30_099).active)
        assertFalse(service.snapshot(30_100).active)

        service.start(nowMillis = 40_000, batteryPercent = 80)
        assertTrue(service.cancel())
        assertFalse(service.snapshot(40_001).active)
    }

    @Test
    fun findPhoneRequiresForegroundOrNotificationPermission() {
        assertEquals(
            FindPhoneRoute.Foreground,
            FindDevicePolicy.phoneRoute(
                appInForeground = true,
                notificationPermissionAvailable = false,
            ),
        )
        assertEquals(
            FindPhoneRoute.Notification,
            FindDevicePolicy.phoneRoute(
                appInForeground = false,
                notificationPermissionAvailable = true,
            ),
        )
        assertEquals(
            FindPhoneRoute.Unavailable,
            FindDevicePolicy.phoneRoute(
                appInForeground = false,
                notificationPermissionAvailable = false,
            ),
        )
    }
}
