package com.fireflyos.companion.ble

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class ScanSessionTracker {
    private var nextGeneration = 0
    private var activeGeneration = 0

    fun begin(): Int {
        nextGeneration =
            if (nextGeneration == Int.MAX_VALUE) 1 else nextGeneration + 1
        activeGeneration = nextGeneration
        return activeGeneration
    }

    fun claimResult(generation: Int): Boolean =
        claim(generation)

    fun fail(generation: Int): Boolean =
        claim(generation)

    fun cancel() {
        activeGeneration = 0
    }

    private fun claim(generation: Int): Boolean {
        if (generation == 0 || generation != activeGeneration) return false
        activeGeneration = 0
        return true
    }
}

sealed interface MainActionOutcome<out T> {
    data class Completed<T>(val value: T) : MainActionOutcome<T>
    data object Cancelled : MainActionOutcome<Nothing>
    data object Failed : MainActionOutcome<Nothing>
}

class CancellableMainAction<T>(
    private val action: () -> T,
) {
    private val state = AtomicInteger(PENDING)
    private val completed = CountDownLatch(1)

    @Volatile
    private var value: T? = null

    @Volatile
    private var failed = false

    fun run(): Boolean {
        if (!state.compareAndSet(PENDING, RUNNING)) return false
        try {
            value = action()
        } catch (_: Throwable) {
            failed = true
        } finally {
            state.set(COMPLETE)
            completed.countDown()
        }
        return true
    }

    fun cancel(): Boolean =
        state.compareAndSet(PENDING, CANCELLED)

    fun await(timeoutMillis: Long): MainActionOutcome<T> {
        var interrupted = false
        val finishedWithinTimeout = try {
            completed.await(
                timeoutMillis.coerceAtLeast(0),
                TimeUnit.MILLISECONDS,
            )
        } catch (_: InterruptedException) {
            interrupted = true
            false
        }
        if (!finishedWithinTimeout &&
            state.compareAndSet(PENDING, CANCELLED)
        ) {
            if (interrupted) Thread.currentThread().interrupt()
            return MainActionOutcome.Cancelled
        }

        if (state.get() == CANCELLED) {
            if (interrupted) Thread.currentThread().interrupt()
            return MainActionOutcome.Cancelled
        }

        while (state.get() == RUNNING) {
            try {
                completed.await()
            } catch (_: InterruptedException) {
                interrupted = true
            }
        }
        if (interrupted) Thread.currentThread().interrupt()

        if (state.get() == CANCELLED) return MainActionOutcome.Cancelled
        if (failed) return MainActionOutcome.Failed
        @Suppress("UNCHECKED_CAST")
        return MainActionOutcome.Completed(value as T)
    }

    companion object {
        private const val PENDING = 0
        private const val RUNNING = 1
        private const val CANCELLED = 2
        private const val COMPLETE = 3
    }
}

class FixedGattBatchQueue<T>(
    private val capacity: Int,
) {
    data class BatchItem<T>(
        val batchId: Int,
        val value: T,
    )

    private data class BatchRecord(
        val id: Int,
        var remaining: Int,
        val completion: (Boolean) -> Unit,
    )

    private val pending = ArrayDeque<BatchItem<T>>(capacity)
    private val batches = arrayOfNulls<BatchRecord>(capacity)
    private var current: BatchItem<T>? = null
    private var nextBatchId = 0

    init {
        require(capacity > 0)
    }

    val operationCount: Int
        get() = pending.size + if (current == null) 0 else 1

    fun enqueueBatch(
        values: List<T>,
        completion: (Boolean) -> Unit,
    ): Boolean {
        if (values.isEmpty() ||
            values.size > capacity - operationCount
        ) {
            return false
        }
        val recordIndex = batches.indexOfFirst { it == null }
        if (recordIndex < 0) return false

        var batchId = 0
        repeat(capacity + 1) {
            nextBatchId =
                if (nextBatchId == Int.MAX_VALUE) 1 else nextBatchId + 1
            if (batchId == 0 &&
                batches.none { record -> record?.id == nextBatchId }
            ) {
                batchId = nextBatchId
            }
        }
        if (batchId == 0) return false
        batches[recordIndex] =
            BatchRecord(batchId, values.size, completion)
        values.forEach { pending.addLast(BatchItem(batchId, it)) }
        return true
    }

    fun takeNext(): BatchItem<T>? {
        if (current != null || pending.isEmpty()) return null
        return pending.removeFirst().also { current = it }
    }

    fun completeCurrent(success: Boolean) {
        val finished = current ?: return
        current = null
        val recordIndex =
            batches.indexOfFirst { it?.id == finished.batchId }
        if (recordIndex < 0) return
        val record = checkNotNull(batches[recordIndex])

        if (!success) {
            pending.removeAll { it.batchId == finished.batchId }
            batches[recordIndex] = null
            record.completion(false)
            return
        }

        record.remaining -= 1
        if (record.remaining == 0) {
            batches[recordIndex] = null
            record.completion(true)
        }
    }

    fun timeoutCurrent(): Boolean {
        if (current == null) return false
        completeCurrent(success = false)
        return true
    }

    fun cancelAll() {
        val callbacks =
            arrayOfNulls<((Boolean) -> Unit)>(capacity)
        var count = 0
        batches.forEachIndexed { index, record ->
            if (record != null) {
                callbacks[count++] = record.completion
                batches[index] = null
            }
        }
        current = null
        pending.clear()
        repeat(count) { callbacks[it]?.invoke(false) }
    }
}
