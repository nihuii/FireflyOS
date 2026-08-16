package com.fireflyos.companion.transfer

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BulkTransferLaunchGuardTest {
    @Test
    fun duplicateDirectReadyStartsExactlyOneUpload() {
        val guard = BulkTransferLaunchGuard()
        guard.reset(7)
        assertTrue(guard.claimDirectUpload(7))
        assertFalse(guard.claimDirectUpload(7))
        assertFalse(guard.claimDirectUpload(8))
    }

    @Test
    fun duplicateSoftApCallbacksCannotStartOrCancelTwice() {
        val guard = BulkTransferLaunchGuard()
        guard.reset(9)
        assertTrue(guard.claimNetworkRequest(9))
        assertFalse(guard.claimNetworkRequest(9))
        assertTrue(guard.claimNetworkAvailable(9))
        assertFalse(guard.claimNetworkAvailable(9))
        assertFalse(guard.claimNetworkUnavailable(9))
        assertFalse(guard.claimDirectUpload(9))
    }
}
