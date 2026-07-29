package com.fireflyos.companion.ble

sealed interface ReliableSendFailure {
    data class QueueFull(val capacity: Int) : ReliableSendFailure
    data class WriteRejected(val sequence: Int) : ReliableSendFailure
    data class RetriesExhausted(val sequence: Int) : ReliableSendFailure
}

class ReliableFrameSender(
    private val capacity: Int = CAPACITY,
    private val writeFrame:
        (Frame, (Boolean, Long) -> Unit) -> Boolean,
    private val onFailure: (ReliableSendFailure) -> Unit = {},
) {
    private enum class Phase {
        Writing,
        AwaitingAck,
        RetryDelay,
    }

    private data class Pending(
        val frame: Frame,
        var phase: Phase = Phase.Writing,
        var completedAtMillis: Long = 0,
        var retries: Int = 0,
        var attempt: Int = 0,
    )

    private val waiting = arrayOfNulls<Pending>(capacity)
    private var head = 0
    private var tail = 0
    private var waitingCount = 0
    private var inFlight: Pending? = null
    private var connected = false
    private var nextSequence = 1

    init {
        require(capacity > 0)
    }

    val pendingCount: Int
        @Synchronized get() = waitingCount + if (inFlight == null) 0 else 1

    val inFlightSequence: Int?
        @Synchronized get() = inFlight?.frame?.sequence

    @Synchronized
    fun setConnected(value: Boolean, nowMillis: Long) {
        connected = value
        if (!value) {
            clearPending()
        } else {
            pump(nowMillis)
        }
    }

    @Synchronized
    fun allocateSequence(): Int {
        val allocated = nextSequence
        nextSequence = (nextSequence + 1) and 0xFFFF
        if (nextSequence == 0) nextSequence = 1
        return allocated
    }

    @Synchronized
    fun enqueue(
        type: MessageType,
        payload: ByteArray,
        nowMillis: Long,
        flags: Int = FrameFlags.ACK_REQUIRED,
    ): Boolean = sendFrame(
        Frame(
            type = type,
            flags = flags,
            sequence = allocateSequence(),
            payload = payload.copyOf(),
        ),
        nowMillis,
    )

    @Synchronized
    fun sendFrame(frame: Frame, nowMillis: Long): Boolean =
        sendFrame(frame, nowMillis) {}

    @Synchronized
    fun sendFrame(
        frame: Frame,
        nowMillis: Long,
        onWritten: (Boolean) -> Unit,
    ): Boolean {
        if (!connected) return false
        val snapshot = frame.copy(payload = frame.payload.copyOf())
        if (snapshot.isMalformedAck()) return false
        if (isAck(snapshot) ||
            (snapshot.flags and FrameFlags.ACK_REQUIRED) == 0
        ) {
            var callbackInvoked = false
            val written = writeFrame(snapshot) { success, _ ->
                synchronized(this) {
                    if (callbackInvoked) return@synchronized
                    callbackInvoked = true
                    onWritten(success)
                    if (!success) {
                        onFailure(
                            ReliableSendFailure.WriteRejected(
                                snapshot.sequence,
                            ),
                        )
                    }
                }
            }
            if (!written) {
                if (!callbackInvoked) {
                    callbackInvoked = true
                    onWritten(false)
                    onFailure(
                        ReliableSendFailure.WriteRejected(snapshot.sequence),
                    )
                }
            }
            return written
        }
        if (pendingCount >= capacity) {
            onFailure(ReliableSendFailure.QueueFull(capacity))
            return false
        }
        waiting[tail] = Pending(snapshot)
        tail = (tail + 1) % capacity
        waitingCount += 1
        pump(nowMillis)
        return true
    }

    @Synchronized
    fun onAck(sequence: Int, nowMillis: Long): Boolean {
        val current = inFlight ?: return false
        if (current.frame.sequence != (sequence and 0xFFFF)) return false
        inFlight = null
        pump(nowMillis)
        return true
    }

    @Synchronized
    fun service(nowMillis: Long) {
        val current = inFlight
        if (current == null) {
            pump(nowMillis)
            return
        }
        if (current.phase == Phase.Writing ||
            nowMillis - current.completedAtMillis < ACK_TIMEOUT_MILLIS
        ) {
            return
        }
        if (current.retries >= MAX_RETRIES) {
            onFailure(
                ReliableSendFailure.RetriesExhausted(current.frame.sequence),
            )
            inFlight = null
            pump(nowMillis)
            return
        }
        current.retries += 1
        beginWrite(current, nowMillis)
    }

    private fun pump(nowMillis: Long) {
        if (!connected || inFlight != null || waitingCount == 0) return
        val next = checkNotNull(waiting[head])
        waiting[head] = null
        head = (head + 1) % capacity
        waitingCount -= 1
        inFlight = next
        beginWrite(next, nowMillis)
    }

    private fun beginWrite(pending: Pending, nowMillis: Long) {
        pending.phase = Phase.Writing
        pending.attempt += 1
        val attempt = pending.attempt
        var callbackInvoked = false
        val submitted = writeFrame(pending.frame) { success, completedAt ->
            synchronized(this) {
                if (callbackInvoked) return@synchronized
                callbackInvoked = true
                completeWrite(
                    pending.frame.sequence,
                    attempt,
                    success,
                    completedAt,
                )
            }
        }
        if (!submitted && !callbackInvoked) {
            callbackInvoked = true
            completeWrite(
                pending.frame.sequence,
                attempt,
                false,
                nowMillis,
            )
        }
    }

    private fun completeWrite(
        sequence: Int,
        attempt: Int,
        success: Boolean,
        completedAtMillis: Long,
    ) {
        val current = inFlight ?: return
        if (current.frame.sequence != sequence ||
            current.attempt != attempt ||
            current.phase != Phase.Writing
        ) {
            return
        }
        current.completedAtMillis = completedAtMillis
        current.phase =
            if (success) Phase.AwaitingAck else Phase.RetryDelay
        if (!success) {
            onFailure(ReliableSendFailure.WriteRejected(sequence))
        }
    }

    private fun clearPending() {
        waiting.fill(null)
        head = 0
        tail = 0
        waitingCount = 0
        inFlight = null
    }

    private fun isAck(frame: Frame): Boolean =
        frame.isStrictAck()

    companion object {
        const val ACK_TIMEOUT_MILLIS = 2_000L
        const val MAX_RETRIES = 3
        const val CAPACITY = 32
    }
}
