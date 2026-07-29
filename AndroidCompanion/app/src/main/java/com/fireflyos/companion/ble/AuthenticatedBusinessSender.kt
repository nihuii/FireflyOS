package com.fireflyos.companion.ble

import com.fireflyos.companion.data.ConnectionStatus

class AuthenticatedBusinessSender(
    private val connectionStatus: () -> ConnectionStatus,
    private val token: () -> ByteArray?,
    private val maxAttBytes: () -> Int,
    private val writeCommandBatch:
        (List<ByteArray>, (Boolean) -> Unit) -> Boolean,
) {
    fun send(
        type: MessageType,
        payload: ByteArray,
        sequence: Int,
        flags: Int = FrameFlags.ACK_REQUIRED,
        onComplete: (Boolean) -> Unit = {},
    ): Boolean {
        if (connectionStatus() != ConnectionStatus.Connected ||
            payload.size > FrameCodec.MAX_PAYLOAD - FrameAuthenticator.AUTH_TAG_BYTES
        ) {
            return false
        }
        val authenticated = FrameAuthenticator.authenticate(
            Frame(type, flags, sequence, payload.copyOf()),
            token(),
        ) ?: return false
        val fragments =
            FrameCodec.fragment(authenticated, maxAttBytes()) ?: return false
        val encoded = fragments.map { FrameCodec.encode(it) ?: return false }
        return writeCommandBatch(encoded, onComplete)
    }
}
