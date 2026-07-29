package com.fireflyos.companion.media

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MediaCommandDispatcherTest {
    @Test
    fun noActiveSessionReturnsExplicitErrorWithoutExecuting() {
        val dispatcher = MediaCommandDispatcher(
            notificationAccessAvailable = { true },
            activeSession = { null },
        )

        assertEquals(
            MediaDispatchResult.Error(MediaDispatchError.NoActiveSession),
            dispatcher.dispatch(MediaCommand.PlayPause),
        )
    }

    @Test
    fun missingNotificationAccessAndSecurityExceptionAreExplicit() {
        val unavailable = MediaCommandDispatcher(
            notificationAccessAvailable = { false },
            activeSession = { error("must not query") },
        )
        assertEquals(
            MediaDispatchResult.Error(MediaDispatchError.NotificationAccessRequired),
            unavailable.dispatch(MediaCommand.Next),
        )

        val denied = MediaCommandDispatcher(
            notificationAccessAvailable = { true },
            activeSession = { throw SecurityException("revoked") },
        )
        assertEquals(
            MediaDispatchResult.Error(MediaDispatchError.SecurityDenied),
            denied.dispatch(MediaCommand.Previous),
        )
    }

    @Test
    fun availableSessionDispatchesAllSupportedCommands() {
        val target = RecordingMediaSession()
        val dispatcher = MediaCommandDispatcher(
            notificationAccessAvailable = { true },
            activeSession = { target },
        )

        listOf(
            MediaCommand.PlayPause,
            MediaCommand.Previous,
            MediaCommand.Next,
            MediaCommand.Volume(73),
        ).forEach {
            assertEquals(MediaDispatchResult.Success, dispatcher.dispatch(it))
        }

        assertEquals(
            listOf("playPause", "previous", "next", "volume:73"),
            target.calls,
        )
        assertTrue(MediaCommandCodec.encode(MediaCommand.Volume(73)).size <= 1024)
    }

    private class RecordingMediaSession : MediaSessionTarget {
        val calls = mutableListOf<String>()

        override fun playPause() {
            calls += "playPause"
        }

        override fun previous() {
            calls += "previous"
        }

        override fun next() {
            calls += "next"
        }

        override fun setVolume(percent: Int) {
            calls += "volume:$percent"
        }
    }
}
