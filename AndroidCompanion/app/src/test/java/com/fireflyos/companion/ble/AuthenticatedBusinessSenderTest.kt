package com.fireflyos.companion.ble

import com.fireflyos.companion.data.ConnectionStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AuthenticatedBusinessSenderTest {
    @Test
    fun disconnectedOrUnpairedBusinessSendFailsWithoutWriting() {
        val batches = mutableListOf<List<ByteArray>>()
        var status = ConnectionStatus.Disconnected
        var token: ByteArray? = ByteArray(16)
        val sender = AuthenticatedBusinessSender(
            connectionStatus = { status },
            token = { token },
            maxAttBytes = { 180 },
            writeCommandBatch = { frames, _ ->
                batches += frames
                true
            },
        )

        assertFalse(sender.send(MessageType.WeatherUpdate, byteArrayOf(1), 1) {})
        status = ConnectionStatus.Connected
        token = null
        assertFalse(sender.send(MessageType.WeatherUpdate, byteArrayOf(1), 2) {})
        assertTrue(batches.isEmpty())
    }

    @Test
    fun connectedPairedBusinessSendUsesAuthenticatedBoundedFrames() {
        val batches = mutableListOf<List<ByteArray>>()
        var batchCompletion: ((Boolean) -> Unit)? = null
        val completions = mutableListOf<Boolean>()
        val sender = AuthenticatedBusinessSender(
            connectionStatus = { ConnectionStatus.Connected },
            token = { ByteArray(16) { it.toByte() } },
            maxAttBytes = { 180 },
            writeCommandBatch = { frames, completion ->
                batches += frames
                batchCompletion = completion
                true
            },
        )

        assertTrue(
            sender.send(
                MessageType.CalendarUpdate,
                ByteArray(600) { it.toByte() },
                sequence = 7,
                onComplete = completions::add,
            ),
        )
        assertEquals(1, batches.size)
        assertTrue(batches.single().size > 1)
        assertTrue(batches.single().all { it.size <= 180 })
        assertTrue(completions.isEmpty())
        batchCompletion!!(true)
        assertEquals(listOf(true), completions)
    }

    @Test
    fun negotiatedMtuControlsFragmentSizeAtSendTime() {
        val batches = mutableListOf<List<ByteArray>>()
        var maxAttBytes = 20
        val sender = AuthenticatedBusinessSender(
            connectionStatus = { ConnectionStatus.Connected },
            token = { ByteArray(16) { it.toByte() } },
            maxAttBytes = { maxAttBytes },
            writeCommandBatch = { frames, _ ->
                batches += frames
                true
            },
        )

        assertTrue(
            sender.send(
                MessageType.SettingsSet,
                ByteArray(40) { it.toByte() },
                sequence = 8,
            ),
        )
        assertEquals(7, batches.single().size)
        assertTrue(batches.single().all { it.size <= 20 })

        maxAttBytes = 180
        assertTrue(
            sender.send(
                MessageType.SettingsSet,
                ByteArray(40) { it.toByte() },
                sequence = 9,
            ),
        )
        assertEquals(1, batches.last().size)
        assertTrue(batches.last().single().size <= 180)
    }
}
