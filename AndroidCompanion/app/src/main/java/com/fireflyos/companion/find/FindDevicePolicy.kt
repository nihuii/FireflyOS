package com.fireflyos.companion.find

data class FindWatchPlan(
    val durationMillis: Long,
    val flashScreen: Boolean,
    val playAudio: Boolean,
)

enum class FindPhoneRoute { Foreground, Notification, Unavailable }

object FindDevicePolicy {
    const val EXTREMELY_LOW_BATTERY_PERCENT = 5
    const val NORMAL_DURATION_MILLIS = 30_000L
    const val LOW_BATTERY_DURATION_MILLIS = 5_000L

    fun watchPlan(batteryPercent: Int): FindWatchPlan =
        if (batteryPercent in 0..EXTREMELY_LOW_BATTERY_PERCENT) {
            FindWatchPlan(LOW_BATTERY_DURATION_MILLIS, flashScreen = true, playAudio = false)
        } else {
            FindWatchPlan(NORMAL_DURATION_MILLIS, flashScreen = true, playAudio = true)
        }

    fun phoneRoute(
        appInForeground: Boolean,
        notificationPermissionAvailable: Boolean,
    ): FindPhoneRoute = when {
        appInForeground -> FindPhoneRoute.Foreground
        notificationPermissionAvailable -> FindPhoneRoute.Notification
        else -> FindPhoneRoute.Unavailable
    }
}

data class FindWatchState(
    val active: Boolean,
    val flashScreen: Boolean,
    val playAudio: Boolean,
    val endsAtMillis: Long,
)

class FindWatchStateService {
    private var state = FindWatchState(false, false, false, 0)

    fun start(nowMillis: Long, batteryPercent: Int): FindWatchState {
        val plan = FindDevicePolicy.watchPlan(batteryPercent)
        state = FindWatchState(
            active = true,
            flashScreen = plan.flashScreen,
            playAudio = plan.playAudio,
            endsAtMillis = nowMillis + plan.durationMillis,
        )
        return state
    }

    fun cancel(): Boolean {
        if (!state.active) return false
        state = FindWatchState(false, false, false, 0)
        return true
    }

    fun snapshot(nowMillis: Long): FindWatchState {
        if (state.active && nowMillis >= state.endsAtMillis) cancel()
        return state
    }
}
