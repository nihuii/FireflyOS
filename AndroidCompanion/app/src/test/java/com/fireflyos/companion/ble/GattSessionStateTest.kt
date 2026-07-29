package com.fireflyos.companion.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GattSessionStateTest {
    @Test
    fun onlyFirstResultFromCurrentScanGenerationCanConnect() {
        val scans = ScanSessionTracker()
        val old = scans.begin()
        val current = scans.begin()

        assertFalse(scans.claimResult(old))
        assertTrue(scans.claimResult(current))
        assertFalse(scans.claimResult(current))
        assertFalse(scans.fail(current))

        val next = scans.begin()
        assertFalse(scans.fail(old))
        assertTrue(scans.fail(next))

        val cancelled = scans.begin()
        scans.cancel()
        assertFalse(scans.claimResult(cancelled))
        assertFalse(scans.fail(cancelled))
    }

    @Test
    fun timedOutPendingMainActionCannotRemoveFrameLater() {
        val sink = FixedBusinessFrameSink(capacity = 1)
        sink.accept(Frame(MessageType.MediaCommand, sequence = 9))
        val action = CancellableMainAction { sink.take() }

        assertEquals(
            MainActionOutcome.Cancelled,
            action.await(timeoutMillis = 0),
        )
        assertFalse(action.run())
        assertEquals(1, sink.pendingCount)
        assertEquals(9, sink.take()!!.sequence)
    }

    @Test
    fun runningMainActionCompletesWithItsRealResult() {
        val action = CancellableMainAction { 42 }

        assertTrue(action.run())
        assertEquals(
            MainActionOutcome.Completed(42),
            action.await(timeoutMillis = 0),
        )
    }
}
