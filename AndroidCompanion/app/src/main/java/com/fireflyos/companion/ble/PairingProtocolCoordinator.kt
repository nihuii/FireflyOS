package com.fireflyos.companion.ble

data class PairingInboundDecision(
    val consumed: Boolean,
    val accepted: Boolean,
    val ack: Frame?,
    val tokenChanged: Boolean,
    val unpaired: Boolean,
)

class PairingProtocolCoordinator(
    private val localDeviceName: String,
    private val tokenStore: PairingTokenStore,
) {
    companion object {
        const val MAX_PHONE_NAME_BYTES = 32
    }

    private var explicitPairingRequested = false
    private var pairRequestSent = false
    private var unpairRequestSent = false
    private var repairPairingRequested = false
    private var unpairCompleted = false
    private var pendingUnpairConfirmSequence: Int? = null
    private var unpairAcknowledgementWritten = false
    private var completedToken: ByteArray? = null

    fun beginExplicitPairing() {
        clearTransientTokens()
        explicitPairingRequested = true
        pairRequestSent = false
        unpairRequestSent = false
        repairPairingRequested = false
        unpairCompleted = false
    }

    fun beginRepairPairing() {
        clearTransientTokens()
        explicitPairingRequested = true
        pairRequestSent = false
        unpairRequestSent = false
        repairPairingRequested = true
        unpairCompleted = false
    }

    fun onConnected(sequence: Int): Frame? {
        val token = tokenStore.loadToken()
        val hasToken =
            token?.size == FrameAuthenticator.APP_TOKEN_BYTES
        if (!explicitPairingRequested || pairRequestSent ||
            unpairRequestSent
        ) {
            return null
        }
        if (hasToken) {
            if (!repairPairingRequested &&
                !tokenStore.hasRetiringToken()
            ) {
                explicitPairingRequested = false
                return null
            }
            unpairRequestSent = true
            return unpairRequest(sequence)
        }
        pairRequestSent = true
        val name = truncateUtf8(
            localDeviceName.ifBlank { "Android" },
            MAX_PHONE_NAME_BYTES,
        )
        return Frame(
            type = MessageType.PairRequest,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = sequence,
            payload = name.toByteArray(Charsets.UTF_8),
        )
    }

    fun requestUnpair(sequence: Int): Frame? {
        val token = tokenStore.loadToken()
        if (token?.size != FrameAuthenticator.APP_TOKEN_BYTES ||
            unpairRequestSent
        ) {
            return null
        }
        explicitPairingRequested = false
        repairPairingRequested = false
        unpairCompleted = false
        unpairRequestSent = true
        return unpairRequest(sequence)
    }

    fun cancelUnpairRequest() {
        if (!repairPairingRequested) unpairRequestSent = false
    }

    fun sessionToken(): ByteArray? = tokenStore.loadToken()

    fun onDisconnected(): Boolean {
        val unpairCommitted =
            unpairCompleted && unpairAcknowledgementWritten
        val tokenCleared = !unpairCommitted || tokenStore.clearToken()
        val continueRepair = repairPairingRequested &&
            unpairCompleted &&
            (!unpairCommitted || tokenCleared)
        explicitPairingRequested = continueRepair
        pairRequestSent = false
        unpairRequestSent = false
        repairPairingRequested = false
        unpairCompleted = false
        pendingUnpairConfirmSequence = null
        unpairAcknowledgementWritten = false
        completedToken?.fill(0)
        completedToken = null
        return continueRepair
    }

    fun onAcknowledgementWritten(sequence: Int, success: Boolean) {
        if (pendingUnpairConfirmSequence == (sequence and 0xFFFF) &&
            success
        ) {
            unpairAcknowledgementWritten = true
        }
    }

    fun onInbound(frame: Frame, bonded: Boolean): PairingInboundDecision {
        var tokenChanged = false
        var consumed = false
        var accepted = false
        var unpaired = false
        if (frame.type == MessageType.PairConfirm) {
            consumed = true
            if (bonded && frame.payload.size == FrameAuthenticator.APP_TOKEN_BYTES) {
                if (explicitPairingRequested && pairRequestSent) {
                    val current = tokenStore.loadToken()
                    val tokenAccepted = if (current != null &&
                        current.contentEquals(frame.payload)
                    ) {
                        true
                    } else {
                        tokenChanged = tokenStore.saveToken(frame.payload)
                        tokenChanged
                    }
                    if (tokenAccepted) {
                        accepted = true
                        this.acceptPairingToken(frame.payload)
                        explicitPairingRequested = false
                        pairRequestSent = false
                        repairPairingRequested = false
                    }
                } else if (completedToken?.contentEquals(frame.payload) == true) {
                    accepted = true
                }
            }
        } else if (frame.type == MessageType.UnpairConfirm) {
            consumed = true
            if (bonded && frame.payload.isEmpty()) {
                if (unpairRequestSent) {
                    val current = sessionToken()
                    if (current?.size == FrameAuthenticator.APP_TOKEN_BYTES) {
                        if(tokenStore.retireToken()) {
                            unpairRequestSent = false
                            unpairCompleted = true
                            pendingUnpairConfirmSequence =
                                frame.sequence and 0xFFFF
                            unpairAcknowledgementWritten = false
                            accepted = true
                            unpaired = true
                        }
                    }
                    current?.fill(0)
                } else if (unpairCompleted &&
                    tokenStore.hasRetiringToken()
                ) {
                    accepted = true
                }
            }
        } else {
            accepted = true
        }
        return PairingInboundDecision(
            consumed = consumed,
            accepted = accepted,
            ack = if (accepted) acknowledgementFor(frame) else null,
            tokenChanged = tokenChanged,
            unpaired = unpaired,
        )
    }

    private fun acceptPairingToken(token: ByteArray) {
        completedToken?.fill(0)
        completedToken = token.copyOf()
    }

    private fun clearTransientTokens() {
        completedToken?.fill(0)
        completedToken = null
    }

    private fun unpairRequest(sequence: Int) = Frame(
        type = MessageType.UnpairRequest,
        flags = FrameFlags.ACK_REQUIRED,
        sequence = sequence,
        payload = ByteArray(0),
    )

    private fun acknowledgementFor(frame: Frame): Frame? {
        if (frame.hasAckMarker() ||
            (frame.flags and FrameFlags.ACK_REQUIRED) == 0
        ) {
            return null
        }
        return Frame(
            type = MessageType.Ack,
            flags = FrameFlags.IS_ACK,
            sequence = frame.sequence,
            payload = ByteArray(0),
        )
    }

    private fun truncateUtf8(value: String, maxBytes: Int): String {
        var charIndex = 0
        var byteCount = 0
        while (charIndex < value.length) {
            val codePoint = Character.codePointAt(value, charIndex)
            val encodedLength = String(Character.toChars(codePoint))
                .toByteArray(Charsets.UTF_8).size
            if (byteCount + encodedLength > maxBytes) break
            byteCount += encodedLength
            charIndex += Character.charCount(codePoint)
        }
        return value.substring(0, charIndex)
    }
}
