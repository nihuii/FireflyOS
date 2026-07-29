package com.fireflyos.companion.sync

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CompanionPayloadCodecTest {
    @Test
    fun weatherCodecRoundTripsBoundedUtf8Summary() {
        val original = PhoneWeather(
            city = "上海",
            temperatureTenthsC = 267,
            weatherCode = 61,
            highTenthsC = 301,
            lowTenthsC = 224,
            updatedAtEpochSeconds = 1_800_000_000,
        )

        val encoded = WeatherPayloadCodec.encode(original)
        val decoded = WeatherPayloadCodec.decode(encoded)

        assertTrue(encoded.size <= WeatherPayloadCodec.MAX_PAYLOAD_BYTES)
        assertEquals(original, decoded)
        assertNull(
            WeatherPayloadCodec.encodeOrNull(
                original.copy(city = "城".repeat(60)),
            ),
        )
    }

    @Test
    fun weatherCacheIsFreshAtThreeHoursAndExpiresOnlyAfterTwentyFourHours() {
        val updatedAt = 1_000_000L
        val cache = WeatherCache(
            PhoneWeather("Paris", 180, 2, 220, 130, updatedAt),
        )

        assertEquals(
            WeatherFreshness.Fresh,
            cache.read(updatedAt + 3 * 60 * 60, connected = false).freshness,
        )
        assertEquals(
            WeatherFreshness.Stale,
            cache.read(updatedAt + 3 * 60 * 60 + 1, connected = false).freshness,
        )
        assertNotNull(cache.read(updatedAt + 24 * 60 * 60, connected = false).weather)
        assertEquals(
            WeatherFreshness.Expired,
            cache.read(updatedAt + 24 * 60 * 60 + 1, connected = false).freshness,
        )
        assertNull(
            cache.read(updatedAt + 24 * 60 * 60 + 1, connected = false).weather,
        )
    }

    @Test
    fun calendarPlannerRequiresOptInAndPermissionThenLimitsSevenDaysAndEightRows() {
        val now = 2_000_000L
        val candidates = (0 until 12).map { index ->
            CalendarCandidate(
                title = "Event $index",
                startEpochMillis = now + index * 60_000,
                endEpochMillis = now + index * 60_000 + 30_000,
                allDay = index == 0,
            )
        } + CalendarCandidate(
            title = "Too late",
            startEpochMillis = now + CalendarSyncPolicy.WINDOW_MILLIS + 1,
            endEpochMillis = now + CalendarSyncPolicy.WINDOW_MILLIS + 2,
            allDay = false,
        )

        val denied = CalendarSyncPlanner.plan(
            userEnabled = true,
            permissionGranted = false,
            nowEpochMillis = now,
            candidates = candidates,
        )
        val disabledPayload = CalendarPayloadCodec.decode(
            CalendarPayloadCodec.encode(denied.payload),
        )
        assertFalse(denied.shouldQuery)
        assertFalse(disabledPayload!!.enabled)
        assertTrue(disabledPayload.entries.isEmpty())

        val enabled = CalendarSyncPlanner.plan(
            userEnabled = true,
            permissionGranted = true,
            nowEpochMillis = now,
            candidates = candidates,
        )
        assertTrue(enabled.shouldQuery)
        assertEquals(8, enabled.payload.entries.size)
        assertFalse(enabled.payload.entries.any { it.title == "Too late" })
    }

    @Test
    fun calendarWireModelContainsOnlyWhitelistedFieldsAndUtf8TruncatesSafely() {
        assertEquals(
            listOf("title", "begin", "end", "allDay"),
            CalendarSyncPolicy.QUERY_FIELDS,
        )
        val payload = CalendarPayload(
            enabled = true,
            updatedAtEpochMillis = 3_000_000,
            entries = listOf(
                CalendarEntry(
                    title = "会".repeat(80),
                    startEpochMillis = 3_100_000,
                    endEpochMillis = 3_200_000,
                    allDay = true,
                ),
            ),
        )

        val decoded = CalendarPayloadCodec.decode(CalendarPayloadCodec.encode(payload))

        assertNotNull(decoded)
        assertTrue(
            decoded!!.entries.single().title.toByteArray(Charsets.UTF_8).size <=
                CalendarPayloadCodec.MAX_TITLE_BYTES,
        )
        assertFalse(decoded.entries.single().title.contains('\uFFFD'))
    }
}
