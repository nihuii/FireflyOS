package com.fireflyos.companion.ble

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

object FireflyProtocol {
    val SERVICE_UUID: UUID = UUID.fromString("7b7f0001-4f53-4653-8000-ff1e00000001")
    val RX_UUID: UUID = UUID.fromString("7b7f0002-4f53-4653-8000-ff1e00000001")
    val TX_UUID: UUID = UUID.fromString("7b7f0003-4f53-4653-8000-ff1e00000001")
    val BULK_UUID: UUID = UUID.fromString("7b7f0004-4f53-4653-8000-ff1e00000001")
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}

enum class MessageType(val value: Int) {
    Hello(0x01),
    PairRequest(0x02),
    PairConfirm(0x03),
    Ack(0x04),
    UnpairRequest(0x05),
    UnpairConfirm(0x06),
    DeviceState(0x10),
    SettingsGet(0x11),
    SettingsSet(0x12),
    NotificationPush(0x20),
    NotificationDismiss(0x21),
    WeatherUpdate(0x30),
    CalendarUpdate(0x31),
    MediaCommand(0x40),
    FindPhone(0x41),
    FindWatch(0x42),
    WifiProvision(0x50),
    BulkTransfer(0x51),
    OtaControl(0x60),
    Error(0x7F);

    companion object {
        fun fromValue(value: Int): MessageType? =
            entries.firstOrNull { it.value == value }
    }
}

object FrameFlags {
    const val ACK_REQUIRED: Int = 0x01
    const val IS_ACK: Int = 0x02
    const val FRAGMENT: Int = 0x04
    const val LAST_FRAGMENT: Int = 0x08
}

data class Frame(
    val type: MessageType = MessageType.Error,
    val flags: Int = 0,
    val sequence: Int = 0,
    val payload: ByteArray = ByteArray(0),
)

fun Frame.isStrictAck(): Boolean =
    type == MessageType.Ack &&
        flags == FrameFlags.IS_ACK &&
        payload.isEmpty()

fun Frame.hasAckMarker(): Boolean =
    type == MessageType.Ack ||
        (flags and FrameFlags.IS_ACK) != 0

fun Frame.isMalformedAck(): Boolean =
    hasAckMarker() && !isStrictAck()

enum class DecodeError {
    TooShort,
    BadMagic,
    BadVersion,
    TooLarge,
    LengthMismatch,
    CrcMismatch,
    BadFragment,
    UnknownMessageType,
}

sealed interface DecodeResult {
    data class Ok(val frame: Frame) : DecodeResult
    data class Error(val error: DecodeError) : DecodeResult
}

object FrameCodec {
    const val MAX_PAYLOAD = 1024
    const val HEADER_SIZE = 11
    const val MAX_FRAGMENTS = 147

    private const val MAGIC_0 = 0x46
    private const val MAGIC_1 = 0x46
    private const val VERSION = 1
    private const val CRC_SEED = 0xFFFF
    private const val CRC_POLY = 0x1021
    fun encode(frame: Frame): ByteArray? {
        if (frame.payload.size > MAX_PAYLOAD) {
            return null
        }

        val output = ByteArray(HEADER_SIZE + frame.payload.size)
        val buffer = ByteBuffer.wrap(output).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(MAGIC_0.toByte())
        buffer.put(MAGIC_1.toByte())
        buffer.put(VERSION.toByte())
        buffer.put(frame.type.value.toByte())
        buffer.put((frame.flags and 0xFF).toByte())
        buffer.putShort((frame.sequence and 0xFFFF).toShort())
        buffer.putShort(frame.payload.size.toShort())

        val crcInput = ByteArray(9 + frame.payload.size)
        output.copyInto(crcInput, destinationOffset = 0, startIndex = 0, endIndex = 9)
        frame.payload.copyInto(crcInput, destinationOffset = 9)
        buffer.putShort(crc16(crcInput).toShort())
        frame.payload.copyInto(output, destinationOffset = HEADER_SIZE)
        return output
    }

    fun decode(input: ByteArray): DecodeResult {
        if (input.size < HEADER_SIZE) {
            return DecodeResult.Error(DecodeError.TooShort)
        }
        if (input[0].unsigned() != MAGIC_0 || input[1].unsigned() != MAGIC_1) {
            return DecodeResult.Error(DecodeError.BadMagic)
        }
        if (input[2].unsigned() != VERSION) {
            return DecodeResult.Error(DecodeError.BadVersion)
        }

        val buffer = ByteBuffer.wrap(input).order(ByteOrder.LITTLE_ENDIAN)
        val sequence = buffer.getShort(5).toInt() and 0xFFFF
        val payloadLength = buffer.getShort(7).toInt() and 0xFFFF
        if (payloadLength > MAX_PAYLOAD) {
            return DecodeResult.Error(DecodeError.TooLarge)
        }
        if (input.size != HEADER_SIZE + payloadLength) {
            return DecodeResult.Error(DecodeError.LengthMismatch)
        }

        val expectedCrc = buffer.getShort(9).toInt() and 0xFFFF
        val crcInput = ByteArray(9 + payloadLength)
        input.copyInto(crcInput, destinationOffset = 0, startIndex = 0, endIndex = 9)
        input.copyInto(crcInput, destinationOffset = 9, startIndex = HEADER_SIZE, endIndex = input.size)
        val actualCrc = crc16(crcInput)
        if (actualCrc != expectedCrc) {
            return DecodeResult.Error(DecodeError.CrcMismatch)
        }

        val type = MessageType.fromValue(input[3].unsigned())
            ?: return DecodeResult.Error(DecodeError.UnknownMessageType)
        return DecodeResult.Ok(
            Frame(
                type = type,
                flags = input[4].unsigned(),
                sequence = sequence,
                payload = input.copyOfRange(HEADER_SIZE, input.size),
            ),
        )
    }

    fun fragment(frame: Frame, maxEncodedSize: Int): List<Frame>? {
        if (frame.payload.size > MAX_PAYLOAD || maxEncodedSize <= HEADER_SIZE) {
            return null
        }
        val singlePayloadCapacity = maxEncodedSize - HEADER_SIZE
        if (frame.payload.size <= singlePayloadCapacity) {
            return listOf(frame)
        }
        val chunkCapacity = singlePayloadCapacity - 2
        if (chunkCapacity <= 0) return null
        val count = (frame.payload.size + chunkCapacity - 1) / chunkCapacity
        if (count <= 0 || count > MAX_FRAGMENTS) return null
        return List(count) { index ->
            val start = index * chunkCapacity
            val end = minOf(start + chunkCapacity, frame.payload.size)
            Frame(
                type = frame.type,
                flags = frame.flags or FrameFlags.FRAGMENT or
                    if (index + 1 == count) FrameFlags.LAST_FRAGMENT else 0,
                sequence = frame.sequence,
                payload = byteArrayOf(index.toByte(), count.toByte()) +
                    frame.payload.copyOfRange(start, end),
            )
        }
    }

    fun reassemble(fragments: List<ByteArray>): DecodeResult {
        if (fragments.isEmpty()) {
            return DecodeResult.Error(DecodeError.BadFragment)
        }

        var expectedSequence: Int? = null
        var expectedCount: Int? = null
        var expectedType: MessageType? = null
        var expectedBaseFlags: Int? = null
        val output = ByteArray(MAX_PAYLOAD)
        var outputLength = 0

        for (position in fragments.indices) {
            val decoded = decode(fragments[position])
            if (decoded !is DecodeResult.Ok) {
                return decoded
            }

            val frame = decoded.frame
            if ((frame.flags and FrameFlags.FRAGMENT) == 0 || frame.payload.size < 2) {
                return DecodeResult.Error(DecodeError.BadFragment)
            }

            val index = frame.payload[0].unsigned()
            val count = frame.payload[1].unsigned()
            if (index != position || count == 0 || count > MAX_FRAGMENTS) {
                return DecodeResult.Error(DecodeError.BadFragment)
            }

            if (expectedSequence == null) {
                expectedSequence = frame.sequence
                expectedCount = count
                expectedType = frame.type
                expectedBaseFlags = frame.flags and
                    (FrameFlags.FRAGMENT or FrameFlags.LAST_FRAGMENT).inv()
            }
            val baseFlags = frame.flags and
                (FrameFlags.FRAGMENT or FrameFlags.LAST_FRAGMENT).inv()
            if (frame.sequence != expectedSequence || count != expectedCount ||
                frame.type != expectedType || baseFlags != expectedBaseFlags
            ) {
                return DecodeResult.Error(DecodeError.BadFragment)
            }

            val chunkLength = frame.payload.size - 2
            if (outputLength + chunkLength > MAX_PAYLOAD) {
                return DecodeResult.Error(DecodeError.TooLarge)
            }
            frame.payload.copyInto(
                output,
                destinationOffset = outputLength,
                startIndex = 2,
                endIndex = frame.payload.size,
            )
            outputLength += chunkLength

            val isLast = (frame.flags and FrameFlags.LAST_FRAGMENT) != 0
            if (isLast != (index + 1 == count)) {
                return DecodeResult.Error(DecodeError.BadFragment)
            }
        }

        if (fragments.size != expectedCount) {
            return DecodeResult.Error(DecodeError.BadFragment)
        }

        return DecodeResult.Ok(
            Frame(
                type = expectedType ?: MessageType.Error,
                flags = expectedBaseFlags ?: 0,
                sequence = expectedSequence ?: 0,
                payload = output.copyOf(outputLength),
            ),
        )
    }

    fun crc16(data: ByteArray, seed: Int = CRC_SEED): Int {
        var crc = seed and 0xFFFF
        for (value in data) {
            crc = crc xor (value.unsigned() shl 8)
            repeat(8) {
                crc = if ((crc and 0x8000) != 0) {
                    ((crc shl 1) xor CRC_POLY) and 0xFFFF
                } else {
                    (crc shl 1) and 0xFFFF
                }
            }
        }
        return crc and 0xFFFF
    }

    private fun Byte.unsigned(): Int = toInt() and 0xFF
}

sealed interface StreamFrameResult {
    data class Complete(val frame: Frame) : StreamFrameResult
    data object Incomplete : StreamFrameResult
    data class Error(val error: DecodeError) : StreamFrameResult
}

class FrameStreamReassembler {
    private val payload = ByteArray(FrameCodec.MAX_PAYLOAD)
    private var active = false
    private var type = MessageType.Error
    private var sequence = 0
    private var count = 0
    private var nextIndex = 0
    private var baseFlags = 0
    private var length = 0

    fun accept(frame: Frame): StreamFrameResult {
        if ((frame.flags and FrameFlags.FRAGMENT) == 0) {
            if (active) return fail(DecodeError.BadFragment)
            return StreamFrameResult.Complete(frame)
        }
        if (frame.payload.size < 2) return fail(DecodeError.BadFragment)
        val index = frame.payload[0].toInt() and 0xFF
        val fragmentCount = frame.payload[1].toInt() and 0xFF
        val fragmentBaseFlags = frame.flags and
            (FrameFlags.FRAGMENT or FrameFlags.LAST_FRAGMENT).inv()
        val isLast = (frame.flags and FrameFlags.LAST_FRAGMENT) != 0
        if (fragmentCount == 0 || fragmentCount > FrameCodec.MAX_FRAGMENTS ||
            index >= fragmentCount || isLast != (index + 1 == fragmentCount)
        ) {
            return fail(DecodeError.BadFragment)
        }

        if (!active) {
            if (index != 0) return fail(DecodeError.BadFragment)
            active = true
            type = frame.type
            sequence = frame.sequence
            count = fragmentCount
            nextIndex = 0
            baseFlags = fragmentBaseFlags
            length = 0
        }
        if (frame.type != type || frame.sequence != sequence ||
            fragmentCount != count || fragmentBaseFlags != baseFlags ||
            index != nextIndex
        ) {
            return fail(DecodeError.BadFragment)
        }

        val chunkLength = frame.payload.size - 2
        if (length + chunkLength > payload.size) {
            return fail(DecodeError.TooLarge)
        }
        frame.payload.copyInto(
            payload,
            destinationOffset = length,
            startIndex = 2,
            endIndex = frame.payload.size,
        )
        length += chunkLength
        nextIndex += 1
        if (!isLast) return StreamFrameResult.Incomplete

        val complete = Frame(
            type = type,
            flags = baseFlags,
            sequence = sequence,
            payload = payload.copyOf(length),
        )
        reset()
        return StreamFrameResult.Complete(complete)
    }

    fun reset() {
        payload.fill(0)
        active = false
        type = MessageType.Error
        sequence = 0
        count = 0
        nextIndex = 0
        baseFlags = 0
        length = 0
    }

    private fun fail(error: DecodeError): StreamFrameResult.Error {
        reset()
        return StreamFrameResult.Error(error)
    }
}
