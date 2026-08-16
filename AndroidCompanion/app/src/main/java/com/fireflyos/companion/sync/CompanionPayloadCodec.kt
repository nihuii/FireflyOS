package com.fireflyos.companion.sync

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.roundToInt

data class PhoneWeather(
    val city: String,
    val temperatureTenthsC: Int,
    val weatherCode: Int,
    val highTenthsC: Int,
    val lowTenthsC: Int,
    val updatedAtEpochSeconds: Long,
    val latitude: Double? = null,
    val longitude: Double? = null,
)

object WeatherPayloadCodec {
    const val MAX_CITY_BYTES = 31
    const val MAX_PAYLOAD_BYTES = 58
    private const val SCHEMA_V1 = 1
    private const val SCHEMA_V2 = 2
    private const val FIXED_BYTES_V1 = 18
    private const val FIXED_BYTES_V2 = 26

    fun encode(weather: PhoneWeather): ByteArray =
        requireNotNull(encodeOrNull(weather)) { "weather payload is out of bounds" }

    fun encodeOrNull(weather: PhoneWeather): ByteArray? {
        val city = weather.city.toByteArray(Charsets.UTF_8)
        val hasLocation = weather.latitude != null || weather.longitude != null
        val latitude = weather.latitude
        val longitude = weather.longitude
        if (city.isEmpty() || city.size > MAX_CITY_BYTES ||
            city.toString(Charsets.UTF_8) != weather.city ||
            weather.temperatureTenthsC !in Short.MIN_VALUE..Short.MAX_VALUE ||
            weather.highTenthsC !in Short.MIN_VALUE..Short.MAX_VALUE ||
            weather.lowTenthsC !in Short.MIN_VALUE..Short.MAX_VALUE ||
            weather.weatherCode !in 0..0xFFFF ||
            (hasLocation && (latitude == null || longitude == null ||
                !latitude.isFinite() || !longitude.isFinite() ||
                latitude !in -90.0..90.0 || longitude !in -180.0..180.0))
        ) {
            return null
        }
        val fixedBytes = if (hasLocation) FIXED_BYTES_V2 else FIXED_BYTES_V1
        val buffer = ByteBuffer.allocate(fixedBytes + city.size)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put((if (hasLocation) SCHEMA_V2 else SCHEMA_V1).toByte())
            .put(city.size.toByte())
            .putShort(weather.weatherCode.toShort())
            .putShort(weather.temperatureTenthsC.toShort())
            .putShort(weather.highTenthsC.toShort())
            .putShort(weather.lowTenthsC.toShort())
            .putLong(weather.updatedAtEpochSeconds)
        if (hasLocation) {
            buffer.putInt((latitude!! * 1_000_000.0).roundToInt())
            buffer.putInt((longitude!! * 1_000_000.0).roundToInt())
        }
        return buffer.put(city).array()
    }

    fun decode(payload: ByteArray): PhoneWeather? {
        if (payload.size !in (FIXED_BYTES_V1 + 1)..MAX_PAYLOAD_BYTES) return null
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        val schema = buffer.get().toInt() and 0xFF
        if (schema != SCHEMA_V1 && schema != SCHEMA_V2) return null
        val cityLength = buffer.get().toInt() and 0xFF
        val fixedTail = if (schema == SCHEMA_V2) 24 else 16
        if (cityLength !in 1..MAX_CITY_BYTES || buffer.remaining() != fixedTail + cityLength) return null
        val weatherCode = buffer.short.toInt() and 0xFFFF
        val temperature = buffer.short.toInt()
        val high = buffer.short.toInt()
        val low = buffer.short.toInt()
        val updatedAt = buffer.long
        val latitude = if (schema == SCHEMA_V2) buffer.int / 1_000_000.0 else null
        val longitude = if (schema == SCHEMA_V2) buffer.int / 1_000_000.0 else null
        val cityBytes = ByteArray(cityLength)
        buffer.get(cityBytes)
        val city = cityBytes.toString(Charsets.UTF_8)
        if (city.toByteArray(Charsets.UTF_8).contentEquals(cityBytes).not() ||
            city.contains('\uFFFD')
        ) {
            return null
        }
        return PhoneWeather(city, temperature, weatherCode, high, low, updatedAt,
            latitude, longitude)
    }
}

enum class WeatherFreshness { Fresh, Stale, Expired }

data class WeatherRead(
    val weather: PhoneWeather?,
    val freshness: WeatherFreshness,
    val connected: Boolean,
)

class WeatherCache(private var weather: PhoneWeather? = null) {
    fun update(value: PhoneWeather) {
        weather = value
    }

    fun read(nowEpochSeconds: Long, connected: Boolean): WeatherRead {
        val cached = weather ?: return WeatherRead(null, WeatherFreshness.Expired, connected)
        val age = (nowEpochSeconds - cached.updatedAtEpochSeconds).coerceAtLeast(0)
        val freshness = when {
            age > MAX_AGE_SECONDS -> WeatherFreshness.Expired
            age > STALE_AFTER_SECONDS -> WeatherFreshness.Stale
            else -> WeatherFreshness.Fresh
        }
        return WeatherRead(
            if (freshness == WeatherFreshness.Expired) null else cached,
            freshness,
            connected,
        )
    }

    companion object {
        const val STALE_AFTER_SECONDS = 3 * 60 * 60L
        const val MAX_AGE_SECONDS = 24 * 60 * 60L
    }
}

data class CalendarCandidate(
    val title: String,
    val startEpochMillis: Long,
    val endEpochMillis: Long,
    val allDay: Boolean,
)

data class CalendarEntry(
    val title: String,
    val startEpochMillis: Long,
    val endEpochMillis: Long,
    val allDay: Boolean,
)

data class CalendarPayload(
    val enabled: Boolean,
    val updatedAtEpochMillis: Long,
    val entries: List<CalendarEntry>,
)

data class CalendarSyncPlan(
    val shouldQuery: Boolean,
    val payload: CalendarPayload,
)

object CalendarSyncPolicy {
    const val WINDOW_MILLIS = 7 * 24 * 60 * 60 * 1000L
    const val MAX_ENTRIES = 8
    val QUERY_FIELDS = listOf("title", "begin", "end", "allDay")

    fun select(nowEpochMillis: Long, candidates: List<CalendarCandidate>): List<CalendarEntry> =
        candidates.asSequence()
            .filter {
                it.startEpochMillis >= nowEpochMillis &&
                    it.startEpochMillis <= nowEpochMillis + WINDOW_MILLIS &&
                    it.endEpochMillis >= it.startEpochMillis
            }
            .sortedBy { it.startEpochMillis }
            .take(MAX_ENTRIES)
            .map {
                CalendarEntry(
                    title = Utf8Text.truncate(it.title, CalendarPayloadCodec.MAX_TITLE_BYTES),
                    startEpochMillis = it.startEpochMillis,
                    endEpochMillis = it.endEpochMillis,
                    allDay = it.allDay,
                )
            }
            .toList()
}

object CalendarSyncPlanner {
    fun plan(
        userEnabled: Boolean,
        permissionGranted: Boolean,
        nowEpochMillis: Long,
        candidates: List<CalendarCandidate>,
    ): CalendarSyncPlan {
        val enabled = userEnabled && permissionGranted
        return CalendarSyncPlan(
            shouldQuery = enabled,
            payload = CalendarPayload(
                enabled = enabled,
                updatedAtEpochMillis = nowEpochMillis,
                entries = if (enabled) CalendarSyncPolicy.select(nowEpochMillis, candidates)
                else emptyList(),
            ),
        )
    }
}

object CalendarPayloadCodec {
    const val MAX_TITLE_BYTES = 63
    const val MAX_PAYLOAD_BYTES = 1024
    private const val SCHEMA = 1
    private const val HEADER_BYTES = 11
    private const val ENTRY_FIXED_BYTES = 18

    fun encode(payload: CalendarPayload): ByteArray {
        val entries = payload.entries.take(CalendarSyncPolicy.MAX_ENTRIES)
        val output = ByteArrayOutputStream()
        output.write(SCHEMA)
        output.write(if (payload.enabled) 1 else 0)
        output.write(entries.size)
        output.write(i64(payload.updatedAtEpochMillis))
        entries.forEach { entry ->
            val title = Utf8Text.truncate(entry.title, MAX_TITLE_BYTES)
                .toByteArray(Charsets.UTF_8)
            output.write(title.size)
            output.write(i64(entry.startEpochMillis))
            output.write(i64(entry.endEpochMillis))
            output.write(if (entry.allDay) 1 else 0)
            output.write(title)
        }
        return output.toByteArray().also { require(it.size <= MAX_PAYLOAD_BYTES) }
    }

    fun decode(payload: ByteArray): CalendarPayload? {
        if (payload.size !in HEADER_BYTES..MAX_PAYLOAD_BYTES) return null
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        if ((buffer.get().toInt() and 0xFF) != SCHEMA) return null
        val enabled = when (buffer.get().toInt() and 0xFF) {
            0 -> false
            1 -> true
            else -> return null
        }
        val count = buffer.get().toInt() and 0xFF
        if (count > CalendarSyncPolicy.MAX_ENTRIES || (!enabled && count != 0)) return null
        val updatedAt = buffer.long
        val entries = ArrayList<CalendarEntry>(count)
        repeat(count) {
            if (buffer.remaining() < ENTRY_FIXED_BYTES) return null
            val titleLength = buffer.get().toInt() and 0xFF
            val start = buffer.long
            val end = buffer.long
            val allDay = when (buffer.get().toInt() and 0xFF) {
                0 -> false
                1 -> true
                else -> return null
            }
            if (titleLength > MAX_TITLE_BYTES || buffer.remaining() < titleLength ||
                end < start
            ) {
                return null
            }
            val titleBytes = ByteArray(titleLength)
            buffer.get(titleBytes)
            val title = titleBytes.toString(Charsets.UTF_8)
            if (!title.toByteArray(Charsets.UTF_8).contentEquals(titleBytes) ||
                title.contains('\uFFFD')
            ) {
                return null
            }
            entries += CalendarEntry(title, start, end, allDay)
        }
        if (buffer.hasRemaining()) return null
        return CalendarPayload(enabled, updatedAt, entries)
    }

    private fun i64(value: Long): ByteArray =
        ByteBuffer.allocate(Long.SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)
            .putLong(value).array()
}

object Utf8Text {
    fun truncate(value: String, maxBytes: Int): String {
        if (value.toByteArray(Charsets.UTF_8).size <= maxBytes) return value
        val result = StringBuilder()
        var bytes = 0
        var index = 0
        while (index < value.length) {
            val codePoint = value.codePointAt(index)
            val text = String(Character.toChars(codePoint))
            val encoded = text.toByteArray(Charsets.UTF_8)
            if (bytes + encoded.size > maxBytes) break
            result.append(text)
            bytes += encoded.size
            index += Character.charCount(codePoint)
        }
        return result.toString()
    }
}
