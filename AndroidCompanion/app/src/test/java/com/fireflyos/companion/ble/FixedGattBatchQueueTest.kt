package com.fireflyos.companion.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FixedGattBatchQueueTest {
    @Test
    fun capacityFailureIsAtomicAndEnqueuesNothing() {
        val queue = FixedGattBatchQueue<Int>(capacity = 2)
        var callbackCount = 0

        assertFalse(queue.enqueueBatch(listOf(1, 2, 3)) { callbackCount += 1 })
        assertEquals(0, queue.operationCount)
        assertNull(queue.takeNext())
        assertEquals(0, callbackCount)
    }

    @Test
    fun midBatchFailureCancelsTailAndCompletesFalseExactlyOnce() {
        val queue = FixedGattBatchQueue<Int>(capacity = 4)
        val completions = mutableListOf<Boolean>()
        assertTrue(queue.enqueueBatch(listOf(10, 11, 12), completions::add))

        assertEquals(10, queue.takeNext()!!.value)
        queue.completeCurrent(success = true)
        assertEquals(11, queue.takeNext()!!.value)
        queue.completeCurrent(success = false)

        assertEquals(listOf(false), completions)
        assertEquals(0, queue.operationCount)
        assertNull(queue.takeNext())
        queue.completeCurrent(success = false)
        assertEquals(1, completions.size)
    }

    @Test
    fun allFragmentsCompleteTrueOnlyAfterLastCallback() {
        val queue = FixedGattBatchQueue<Int>(capacity = 4)
        val completions = mutableListOf<Boolean>()
        queue.enqueueBatch(listOf(1, 2, 3), completions::add)

        repeat(2) {
            queue.takeNext()
            queue.completeCurrent(success = true)
            assertTrue(completions.isEmpty())
        }
        queue.takeNext()
        queue.completeCurrent(success = true)

        assertEquals(listOf(true), completions)
        assertEquals(0, queue.operationCount)
    }

    @Test
    fun currentOperationTimeoutFailsOnlyThatBatchAndUnblocksQueue() {
        val queue = FixedGattBatchQueue<Int>(capacity = 4)
        val completions = mutableListOf<Pair<String, Boolean>>()
        assertTrue(queue.enqueueBatch(listOf(1, 2)) {
            completions += "first" to it
        })
        assertTrue(queue.enqueueBatch(listOf(3)) {
            completions += "second" to it
        })

        assertEquals(1, queue.takeNext()!!.value)
        assertTrue(queue.timeoutCurrent())

        assertEquals(listOf("first" to false), completions)
        assertEquals(3, queue.takeNext()!!.value)
        queue.completeCurrent(success = true)
        assertEquals(
            listOf("first" to false, "second" to true),
            completions,
        )
        assertNull(queue.takeNext())
    }

    @Test
    fun cleanupClearsCurrentAndQueuedBatchesAndFailsEachOnce() {
        val queue = FixedGattBatchQueue<Int>(capacity = 4)
        val completions = mutableListOf<Pair<String, Boolean>>()
        queue.enqueueBatch(listOf(1, 2)) {
            completions += "first" to it
        }
        queue.enqueueBatch(listOf(3)) {
            completions += "second" to it
        }
        assertEquals(1, queue.takeNext()!!.value)

        queue.cancelAll()
        queue.cancelAll()

        assertEquals(
            listOf("first" to false, "second" to false),
            completions,
        )
        assertEquals(0, queue.operationCount)
        assertNull(queue.takeNext())
    }
}
