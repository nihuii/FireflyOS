package com.fireflyos.companion.sync

import com.fireflyos.companion.ble.Frame
import com.fireflyos.companion.ble.MessageType
import com.fireflyos.companion.data.SettingKind
import com.fireflyos.companion.data.SettingsSnapshot
import com.fireflyos.companion.data.SettingsStateStore
import com.fireflyos.companion.data.VersionedSetting
import com.fireflyos.companion.find.FindPhoneRoute
import com.fireflyos.companion.media.MediaCommandDispatcher
import com.fireflyos.companion.media.MediaCommandCodec
import com.fireflyos.companion.media.MediaCommand
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CompanionControllerTest {
    @Test
    fun settingsWeatherCalendarAndFindWatchUseBusinessSender() {
        val sent = mutableListOf<Pair<MessageType, ByteArray>>()
        val controller = controller(sent)

        assertTrue(
            controller.syncSettings(
                SettingsSnapshot(
                    mapOf(
                        SettingKind.Brightness to
                            VersionedSetting(1, 100, byteArrayOf(90.toByte())),
                    ),
                ),
            ),
        )
        assertTrue(
            controller.syncWeather(
                PhoneWeather("Tokyo", 200, 1, 230, 170, 1000),
            ),
        )
        assertTrue(
            controller.syncCalendar(
                CalendarPayload(false, 1000, emptyList()),
            ),
        )
        assertTrue(controller.setFindWatch(active = true))

        assertEquals(
            listOf(
                MessageType.SettingsSet,
                MessageType.WeatherUpdate,
                MessageType.CalendarUpdate,
                MessageType.FindWatch,
            ),
            sent.map { it.first },
        )
    }

    @Test
    fun unavailableMediaAndFindPhoneReturnExplicitErrorFrames() {
        val sent = mutableListOf<Pair<MessageType, ByteArray>>()
        val controller = controller(sent)

        controller.handleInbound(
            Frame(
                type = MessageType.MediaCommand,
                sequence = 8,
                payload = MediaCommandCodec.encode(MediaCommand.Next),
            ),
        )
        controller.handleInbound(
            Frame(
                type = MessageType.FindPhone,
                sequence = 9,
                payload = byteArrayOf(1, 1),
            ),
        )

        assertEquals(2, sent.size)
        assertTrue(sent.all { it.first == MessageType.Error })
        assertEquals(
            CompanionErrorCode.NoActiveMediaSession,
            CompanionErrorCodec.decode(sent[0].second)!!.code,
        )
        assertEquals(
            CompanionErrorCode.FindPhoneUnavailable,
            CompanionErrorCodec.decode(sent[1].second)!!.code,
        )
    }

    @Test
    fun unauthorizedWireErrorIsDecodedForUi() {
        val decoded = CompanionErrorCodec.decode(
            byteArrayOf(
                1,
                MessageType.SettingsSet.value.toByte(),
                7,
            ),
        )

        assertEquals(MessageType.SettingsSet, decoded!!.failedType)
        assertEquals(CompanionErrorCode.Unauthorized, decoded.code)
    }

    @Test
    fun invalidWeatherIsRejectedBeforeSend() {
        val sent = mutableListOf<Pair<MessageType, ByteArray>>()
        val controller = controller(sent)

        assertFalse(
            controller.syncWeather(
                PhoneWeather("城".repeat(60), 1, 1, 1, 1, 1),
            ),
        )
        assertTrue(sent.isEmpty())
    }

    @Test
    fun settingsGetRoundTripMergesAppliesAndPushesWinningSnapshot() {
        val sent = mutableListOf<Pair<MessageType, ByteArray>>()
        val applied = mutableListOf<SettingsSnapshot>()
        val store = SettingsStateStore(
            SettingsSnapshot(
                mapOf(
                    SettingKind.Brightness to
                        VersionedSetting(2, 2000, byteArrayOf(80.toByte())),
                    SettingKind.Volume to
                        VersionedSetting(1, 1000, byteArrayOf(20.toByte())),
                ),
            ),
        )
        val controller = controller(sent, store, applied::add)
        val watch = SettingsSnapshot(
            mapOf(
                SettingKind.Brightness to
                    VersionedSetting(99, 1999, byteArrayOf(10)),
                SettingKind.Volume to
                    VersionedSetting(1, 1001, byteArrayOf(70)),
            ),
        )

        assertTrue(controller.requestSettings())
        controller.handleInbound(
            Frame(
                type = MessageType.SettingsSet,
                sequence = 44,
                payload = com.fireflyos.companion.data.SettingsSyncCodec.encode(watch),
            ),
        )

        assertEquals(MessageType.SettingsGet, sent.first().first)
        assertTrue(sent.first().second.contentEquals(byteArrayOf(1)))
        assertEquals(1, applied.size)
        assertEquals(
            80,
            applied.single()[SettingKind.Brightness]!!.value.single().toInt(),
        )
        assertEquals(
            70,
            applied.single()[SettingKind.Volume]!!.value.single().toInt(),
        )
        assertEquals(MessageType.SettingsSet, sent.last().first)
        val pushed = com.fireflyos.companion.data.SettingsSyncCodec.decode(
            sent.last().second,
        )!!
        assertEquals(80, pushed[SettingKind.Brightness]!!.value.single().toInt())
        assertEquals(70, pushed[SettingKind.Volume]!!.value.single().toInt())
    }

    private fun controller(sent: MutableList<Pair<MessageType, ByteArray>>) =
        controller(sent, SettingsStateStore()) {}

    private fun controller(
        sent: MutableList<Pair<MessageType, ByteArray>>,
        settingsStore: SettingsStateStore,
        onSettingsResolved: (SettingsSnapshot) -> Unit,
    ) =
        CompanionController(
            sendBusiness = { type, payload ->
                sent += type to payload
                true
            },
            mediaDispatcher = MediaCommandDispatcher(
                notificationAccessAvailable = { true },
                activeSession = { null },
            ),
            triggerFindPhone = { FindPhoneRoute.Unavailable },
            settingsStore = settingsStore,
            onSettingsResolved = onSettingsResolved,
        )
}
