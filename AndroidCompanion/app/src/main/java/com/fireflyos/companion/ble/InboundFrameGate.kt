package com.fireflyos.companion.ble

enum class InboundFrameDisposition {
    Accepted,
    Duplicate,
    Rejected,
}

class InboundSequenceTracker {
    private var hasLatest = false
    private var latest = 0

    fun isFresh(sequence: Int): Boolean {
        if (!hasLatest) return true
        val delta = (sequence - latest) and 0xFFFF
        return delta != 0 && delta < 0x8000
    }

    fun accept(sequence: Int) {
        latest = sequence and 0xFFFF
        hasLatest = true
    }

    fun reset() {
        latest = 0
        hasLatest = false
    }
}

class InboundFrameGate(
    private val token: () -> ByteArray?,
    private val sendAck: (Frame) -> Boolean,
) {
    private val sequenceTracker = InboundSequenceTracker()

    fun receive(
        frame: Frame,
        encrypted: Boolean,
        accept: (Frame) -> Boolean,
    ): InboundFrameDisposition {
        if (frame.isMalformedAck()) return InboundFrameDisposition.Rejected
        val verified = if (FrameAuthenticator.isSensitive(frame.type)) {
            if (!encrypted) null
            else FrameAuthenticator.verify(frame, token())
        } else {
            frame
        } ?: return InboundFrameDisposition.Rejected

        if (!sequenceTracker.isFresh(verified.sequence)) {
            acknowledgeIfRequired(verified)
            return InboundFrameDisposition.Duplicate
        }
        if (!accept(verified)) return InboundFrameDisposition.Rejected
        sequenceTracker.accept(verified.sequence)
        acknowledgeIfRequired(verified)
        return InboundFrameDisposition.Accepted
    }

    fun reset() {
        sequenceTracker.reset()
    }

    private fun acknowledgeIfRequired(frame: Frame) {
        if ((frame.flags and FrameFlags.ACK_REQUIRED) == 0) return
        sendAck(
            Frame(
                type = MessageType.Ack,
                flags = FrameFlags.IS_ACK,
                sequence = frame.sequence,
            ),
        )
    }
}

class FixedBusinessFrameSink(private val capacity: Int) {
    private val pending = ArrayDeque<Frame>()
    private var listener: ((Frame) -> Unit)? = null

    init {
        require(capacity > 0)
    }

    val pendingCount: Int
        get() = pending.size

    fun setListener(next: ((Frame) -> Unit)?) {
        listener = next
        val active = next ?: return
        while (pending.isNotEmpty()) {
            val frame = pending.first()
            val delivered = runCatching { active(frame) }.isSuccess
            if (!delivered) return
            pending.removeFirst()
        }
    }

    fun accept(frame: Frame): Boolean {
        val active = listener
        if (active != null) return runCatching { active(frame) }.isSuccess
        if (pending.size >= capacity) return false
        pending.addLast(frame)
        return true
    }

    fun take(): Frame? =
        if (pending.isEmpty()) null else pending.removeFirst()
}
