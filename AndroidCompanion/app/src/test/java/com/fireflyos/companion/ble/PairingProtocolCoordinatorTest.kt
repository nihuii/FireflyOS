package com.fireflyos.companion.ble

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PairingProtocolCoordinatorTest {
    @Test
    fun connectionWithoutExplicitUserActionDoesNotRequestPairing() {
        val store = FakeTokenStore()
        val coordinator = PairingProtocolCoordinator("Android Phone", store)

        val request = coordinator.onConnected(sequence = 1)

        assertNull(request)
        assertEquals(0, store.saveCount)
    }

    @Test
    fun explicitPairingSendsOneBoundedPairRequestAfterConnection() {
        val store = FakeTokenStore()
        val coordinator = PairingProtocolCoordinator("安卓手机".repeat(8), store)
        coordinator.beginExplicitPairing()

        val first = coordinator.onConnected(sequence = 9)
        val duplicateConnectedCallback = coordinator.onConnected(sequence = 10)

        assertEquals(MessageType.PairRequest, first!!.type)
        assertEquals(FrameFlags.ACK_REQUIRED, first.flags)
        assertEquals(9, first.sequence)
        assertTrue(first.payload.size <= 32)
        assertEquals(
            first.payload.toString(Charsets.UTF_8),
            first.payload.toString(Charsets.UTF_8)
                .toByteArray(Charsets.UTF_8)
                .toString(Charsets.UTF_8),
        )
        assertNull(duplicateConnectedCallback)
    }

    @Test
    fun existingTokenSuppressesPairRequest() {
        val store = FakeTokenStore(ByteArray(16) { it.toByte() })
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        coordinator.beginExplicitPairing()

        assertNull(coordinator.onConnected(sequence = 1))
        assertEquals(0, store.clearCount)
        assertArrayEquals(ByteArray(16) { it.toByte() }, store.loadToken())
    }

    @Test
    fun explicitRepairKeepsOldTokenAndRequestsAuthenticatedUnpairFirst() {
        val store = FakeTokenStore(ByteArray(16) { it.toByte() })
        val coordinator = PairingProtocolCoordinator("Android Phone", store)

        coordinator.beginRepairPairing()
        val request = coordinator.onConnected(sequence = 12)

        assertEquals(0, store.clearCount)
        assertArrayEquals(ByteArray(16) { it.toByte() }, store.loadToken())
        assertEquals(MessageType.UnpairRequest, request!!.type)
        assertEquals(12, request.sequence)
    }

    @Test
    fun unsolicitedPairConfirmIsRejectedWithoutAckOrTokenWrite() {
        val store = FakeTokenStore()
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        val confirm = pairConfirm(ByteArray(16) { it.toByte() }, sequence = 44)

        val result = coordinator.onInbound(confirm, bonded = true)

        assertFalse(result.tokenChanged)
        assertEquals(0, store.saveCount)
        assertFalse(result.accepted)
        assertNull(result.ack)
    }

    @Test
    fun bondedPairConfirmSavesOnceAndAcknowledgesEveryDelivery() {
        val store = FakeTokenStore()
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        val token = ByteArray(16) { (0xA0 + it).toByte() }
        coordinator.beginExplicitPairing()
        coordinator.onConnected(sequence = 1)
        val confirm = pairConfirm(token, sequence = 0x3456)

        val first = coordinator.onInbound(confirm, bonded = true)
        val repeated = coordinator.onInbound(confirm, bonded = true)

        assertTrue(first.tokenChanged)
        assertFalse(repeated.tokenChanged)
        assertEquals(1, store.saveCount)
        assertArrayEquals(token, store.loadToken())
        for (result in listOf(first, repeated)) {
            assertTrue(result.consumed)
            assertTrue(result.accepted)
            assertEquals(MessageType.Ack, result.ack!!.type)
            assertEquals(FrameFlags.IS_ACK, result.ack.flags)
            assertEquals(confirm.sequence, result.ack.sequence)
            assertTrue(result.ack.payload.isEmpty())
        }
    }

    @Test
    fun completedPairingTransactionRejectsDifferentTokenRotation() {
        val store = FakeTokenStore()
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        val accepted = ByteArray(16) { (0x20 + it).toByte() }
        val different = ByteArray(16) { (0x60 + it).toByte() }
        coordinator.beginExplicitPairing()
        coordinator.onConnected(sequence = 1)

        val first = coordinator.onInbound(
            pairConfirm(accepted, sequence = 2),
            bonded = true,
        )
        val rotation = coordinator.onInbound(
            pairConfirm(different, sequence = 3),
            bonded = true,
        )

        assertTrue(first.tokenChanged)
        assertFalse(rotation.tokenChanged)
        assertFalse(rotation.accepted)
        assertEquals(1, store.saveCount)
        assertArrayEquals(accepted, store.loadToken())
        assertNull(rotation.ack)
    }

    @Test
    fun invalidOrUnbondedPairConfirmDoesNotSaveOrAcknowledge() {
        val store = FakeTokenStore()
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        coordinator.beginExplicitPairing()
        coordinator.onConnected(sequence = 1)
        val wrongLength = Frame(
            type = MessageType.PairConfirm,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 3,
            payload = ByteArray(15),
        )

        val unbonded = coordinator.onInbound(
            wrongLength.copy(payload = ByteArray(16)),
            bonded = false,
        )
        val invalid = coordinator.onInbound(wrongLength, bonded = true)

        assertFalse(unbonded.tokenChanged)
        assertFalse(invalid.tokenChanged)
        assertEquals(0, store.saveCount)
        assertFalse(unbonded.accepted)
        assertFalse(invalid.accepted)
        assertNull(unbonded.ack)
        assertNull(invalid.ack)
    }

    @Test
    fun tokenPersistenceFailureRejectsPairConfirmWithoutAck() {
        val store = FakeTokenStore(saveResult = false)
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        coordinator.beginExplicitPairing()
        coordinator.onConnected(sequence = 1)

        val result = coordinator.onInbound(
            pairConfirm(ByteArray(16) { it.toByte() }, sequence = 4),
            bonded = true,
        )

        assertFalse(result.accepted)
        assertFalse(result.tokenChanged)
        assertNull(result.ack)
        assertNull(store.loadToken())
    }

    @Test
    fun authenticatedUnpairConfirmPersistsRetiringTokenUntilDisconnect() {
        val token = ByteArray(16) { (0x40 + it).toByte() }
        val store = FakeTokenStore(token)
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        coordinator.beginRepairPairing()
        val request = coordinator.onConnected(sequence = 10)
        assertEquals(MessageType.UnpairRequest, request!!.type)

        val confirm = Frame(
            type = MessageType.UnpairConfirm,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 11,
        )
        val first = coordinator.onInbound(confirm, bonded = true)
        val repeated = coordinator.onInbound(confirm, bonded = true)

        assertTrue(first.accepted)
        assertTrue(first.unpaired)
        assertFalse(repeated.unpaired)
        assertEquals(MessageType.Ack, first.ack!!.type)
        assertEquals(MessageType.Ack, repeated.ack!!.type)
        assertArrayEquals(token, store.loadToken())
        assertTrue(store.hasRetiringToken())
        assertArrayEquals(token, coordinator.sessionToken())
        coordinator.onAcknowledgementWritten(confirm.sequence, success = true)
        assertTrue(coordinator.onDisconnected())
        assertNull(store.loadToken())
        assertNull(coordinator.sessionToken())
        assertEquals(
            MessageType.PairRequest,
            coordinator.onConnected(sequence = 12)!!.type,
        )
    }

    @Test
    fun disconnectBeforeUnpairAckWriteKeepsRetiringTokenForRecovery() {
        val token = ByteArray(16) { (0x50 + it).toByte() }
        val store = FakeTokenStore(token)
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        coordinator.beginRepairPairing()
        coordinator.onConnected(sequence = 40)
        val confirm = Frame(
            type = MessageType.UnpairConfirm,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 41,
        )

        assertTrue(coordinator.onInbound(confirm, bonded = true).accepted)
        coordinator.onAcknowledgementWritten(confirm.sequence, success = false)

        assertTrue(coordinator.onDisconnected())
        assertArrayEquals(token, store.loadToken())
        assertTrue(store.hasRetiringToken())
        assertEquals(
            MessageType.UnpairRequest,
            coordinator.onConnected(sequence = 42)!!.type,
        )
    }

    @Test
    fun processRestartCanResumePendingUnpairWithDurableRetiringToken() {
        val token = ByteArray(16) { (0x30 + it).toByte() }
        val store = FakeTokenStore(token)
        val firstCoordinator =
            PairingProtocolCoordinator("Android Phone", store)
        assertEquals(
            MessageType.UnpairRequest,
            firstCoordinator.requestUnpair(sequence = 30)!!.type,
        )
        val confirm = Frame(
            type = MessageType.UnpairConfirm,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 31,
        )

        assertTrue(firstCoordinator.onInbound(confirm, bonded = true).accepted)
        assertTrue(store.hasRetiringToken())

        val restartedCoordinator =
            PairingProtocolCoordinator("Android Phone", store)
        restartedCoordinator.beginExplicitPairing()
        assertEquals(
            MessageType.UnpairRequest,
            restartedCoordinator.onConnected(sequence = 32)!!.type,
        )
        assertArrayEquals(token, restartedCoordinator.sessionToken())
    }

    @Test
    fun unpairConfirmIsRejectedWhenTokenRetirementCannotCommit() {
        val token = ByteArray(16) { (0x20 + it).toByte() }
        val store = FakeTokenStore(token, retireResult = false)
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        coordinator.beginRepairPairing()
        assertEquals(
            MessageType.UnpairRequest,
            coordinator.onConnected(sequence = 20)!!.type,
        )

        val result = coordinator.onInbound(
            Frame(
                type = MessageType.UnpairConfirm,
                flags = FrameFlags.ACK_REQUIRED,
                sequence = 21,
            ),
            bonded = true,
        )

        assertFalse(result.accepted)
        assertFalse(result.unpaired)
        assertNull(result.ack)
        assertArrayEquals(token, store.loadToken())
        assertArrayEquals(token, coordinator.sessionToken())
    }

    @Test
    fun ackRequiredFrameIsAcknowledgedButAckNeverAcknowledgesAck() {
        val store = FakeTokenStore()
        val coordinator = PairingProtocolCoordinator("Android Phone", store)
        val incoming = Frame(
            type = MessageType.NotificationDismiss,
            flags = FrameFlags.ACK_REQUIRED,
            sequence = 77,
            payload = byteArrayOf(1, 0, 0),
        )
        val ack = Frame(
            type = MessageType.Ack,
            flags = FrameFlags.IS_ACK or FrameFlags.ACK_REQUIRED,
            sequence = 77,
        )

        val result = coordinator.onInbound(incoming, bonded = true)
        val ackResult = coordinator.onInbound(ack, bonded = true)

        assertEquals(MessageType.Ack, result.ack!!.type)
        assertEquals(77, result.ack.sequence)
        assertNull(ackResult.ack)
    }

    private class FakeTokenStore(
        initial: ByteArray? = null,
        private val saveResult: Boolean = true,
        private val retireResult: Boolean = true,
        private val clearResult: Boolean = true,
    ) : PairingTokenStore {
        private var token = initial?.copyOf()
        private var retiringToken: ByteArray? = null
        var saveCount = 0
        var clearCount = 0

        override fun loadToken(): ByteArray? =
            (token ?: retiringToken)?.copyOf()

        override fun saveToken(token: ByteArray): Boolean {
            if (!saveResult) return false
            this.token = token.copyOf()
            retiringToken = null
            saveCount += 1
            return true
        }

        override fun retireToken(): Boolean {
            if (!retireResult) return false
            if (retiringToken != null) return true
            val active = token ?: return false
            retiringToken = active
            token = null
            return true
        }

        override fun hasRetiringToken(): Boolean = retiringToken != null

        override fun clearToken(): Boolean {
            if (!clearResult) return false
            token = null
            retiringToken = null
            clearCount += 1
            return true
        }
    }

    private fun pairConfirm(token: ByteArray, sequence: Int) = Frame(
        type = MessageType.PairConfirm,
        flags = FrameFlags.ACK_REQUIRED,
        sequence = sequence,
        payload = token,
    )
}
