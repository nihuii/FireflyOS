package com.fireflyos.companion.ui

import com.fireflyos.companion.data.ConnectionStatus
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CompanionUiPolicyTest {
    @Test
    fun remoteActionsAreDisabledOfflineWithReason() {
        val state = CompanionUiPolicy.forConnection(ConnectionStatus.Disconnected)

        assertFalse(state.remoteActionsEnabled)
        assertTrue(state.unavailableReason.contains("offline", ignoreCase = true))
        assertTrue(state.localWatchFeaturesRemainAvailable)
    }

    @Test
    fun connectedStateEnablesRemoteActionsWithoutClaimingPlatformCapabilities() {
        val state = CompanionUiPolicy.forConnection(ConnectionStatus.Connected)

        assertTrue(state.remoteActionsEnabled)
        assertTrue(state.unavailableReason.isEmpty())
        assertFalse(state.mediaSessionAvailable)
        assertFalse(state.calendarPermissionGranted)
    }
}
