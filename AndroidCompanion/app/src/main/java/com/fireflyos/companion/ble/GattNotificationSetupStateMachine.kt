package com.fireflyos.companion.ble

enum class GattNotificationSetupState {
    Idle,
    AwaitingCccd,
    Connected,
    Error,
}

class GattNotificationSetupStateMachine {
    var state: GattNotificationSetupState = GattNotificationSetupState.Idle
        private set

    fun begin(
        notificationEnabled: Boolean,
        cccdPresent: Boolean,
        descriptorQueued: Boolean,
    ): GattNotificationSetupState {
        state = if (notificationEnabled && cccdPresent && descriptorQueued) {
            GattNotificationSetupState.AwaitingCccd
        } else {
            GattNotificationSetupState.Error
        }
        return state
    }

    fun onDescriptorWrite(success: Boolean): GattNotificationSetupState {
        state = if (state == GattNotificationSetupState.AwaitingCccd && success) {
            GattNotificationSetupState.Connected
        } else {
            GattNotificationSetupState.Error
        }
        return state
    }

    fun reset() {
        state = GattNotificationSetupState.Idle
    }
}
