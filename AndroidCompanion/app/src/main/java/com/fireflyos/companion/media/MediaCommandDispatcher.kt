package com.fireflyos.companion.media

sealed interface MediaCommand {
    data object PlayPause : MediaCommand
    data object Previous : MediaCommand
    data object Next : MediaCommand
    data class Volume(val percent: Int) : MediaCommand
}

interface MediaSessionTarget {
    fun playPause()
    fun previous()
    fun next()
    fun setVolume(percent: Int)
}

enum class MediaDispatchError {
    NotificationAccessRequired,
    NoActiveSession,
    SecurityDenied,
    InvalidCommand,
}

sealed interface MediaDispatchResult {
    data object Success : MediaDispatchResult
    data class Error(val reason: MediaDispatchError) : MediaDispatchResult
}

class MediaCommandDispatcher(
    private val notificationAccessAvailable: () -> Boolean,
    private val activeSession: () -> MediaSessionTarget?,
) {
    fun dispatch(command: MediaCommand): MediaDispatchResult {
        if (!notificationAccessAvailable()) {
            return MediaDispatchResult.Error(MediaDispatchError.NotificationAccessRequired)
        }
        return try {
            val session = activeSession()
                ?: return MediaDispatchResult.Error(MediaDispatchError.NoActiveSession)
            when (command) {
                MediaCommand.PlayPause -> session.playPause()
                MediaCommand.Previous -> session.previous()
                MediaCommand.Next -> session.next()
                is MediaCommand.Volume -> {
                    if (command.percent !in 0..100) {
                        return MediaDispatchResult.Error(MediaDispatchError.InvalidCommand)
                    }
                    session.setVolume(command.percent)
                }
            }
            MediaDispatchResult.Success
        } catch (_: SecurityException) {
            MediaDispatchResult.Error(MediaDispatchError.SecurityDenied)
        }
    }
}

object MediaCommandCodec {
    private const val PLAY_PAUSE = 1
    private const val PREVIOUS = 2
    private const val NEXT = 3
    private const val VOLUME = 4

    fun encode(command: MediaCommand): ByteArray = when (command) {
        MediaCommand.PlayPause -> byteArrayOf(PLAY_PAUSE.toByte())
        MediaCommand.Previous -> byteArrayOf(PREVIOUS.toByte())
        MediaCommand.Next -> byteArrayOf(NEXT.toByte())
        is MediaCommand.Volume -> {
            require(command.percent in 0..100)
            byteArrayOf(VOLUME.toByte(), command.percent.toByte())
        }
    }

    fun decode(payload: ByteArray): MediaCommand? {
        if (payload.isEmpty()) return null
        return when (payload[0].toInt() and 0xFF) {
            PLAY_PAUSE -> MediaCommand.PlayPause.takeIf { payload.size == 1 }
            PREVIOUS -> MediaCommand.Previous.takeIf { payload.size == 1 }
            NEXT -> MediaCommand.Next.takeIf { payload.size == 1 }
            VOLUME -> if (payload.size == 2 && (payload[1].toInt() and 0xFF) <= 100) {
                MediaCommand.Volume(payload[1].toInt() and 0xFF)
            } else {
                null
            }
            else -> null
        }
    }
}
