package com.fireflyos.companion.ble

import org.junit.Assert.assertEquals
import org.junit.Test

class GattNotificationSetupStateMachineTest {
    @Test
    fun connectedIsPublishedOnlyAfterCccdWriteSucceeds() {
        val setup = GattNotificationSetupStateMachine()

        assertEquals(
            GattNotificationSetupState.AwaitingCccd,
            setup.begin(
                notificationEnabled = true,
                cccdPresent = true,
                descriptorQueued = true,
            ),
        )
        assertEquals(
            GattNotificationSetupState.Connected,
            setup.onDescriptorWrite(success = true),
        )
    }

    @Test
    fun missingRejectedOrFailedCccdNeverConnects() {
        val missing = GattNotificationSetupStateMachine()
        assertEquals(
            GattNotificationSetupState.Error,
            missing.begin(true, cccdPresent = false, descriptorQueued = false),
        )

        val rejected = GattNotificationSetupStateMachine()
        assertEquals(
            GattNotificationSetupState.Error,
            rejected.begin(true, cccdPresent = true, descriptorQueued = false),
        )

        val failed = GattNotificationSetupStateMachine()
        assertEquals(
            GattNotificationSetupState.AwaitingCccd,
            failed.begin(true, cccdPresent = true, descriptorQueued = true),
        )
        assertEquals(
            GattNotificationSetupState.Error,
            failed.onDescriptorWrite(success = false),
        )
    }
}
