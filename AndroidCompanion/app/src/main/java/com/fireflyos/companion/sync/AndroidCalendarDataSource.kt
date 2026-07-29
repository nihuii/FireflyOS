package com.fireflyos.companion.sync

import android.content.ContentUris
import android.content.Context
import android.provider.CalendarContract

class AndroidCalendarDataSource(private val context: Context) {
    fun readNextSevenDays(nowEpochMillis: Long): List<CalendarCandidate>? {
        val windowEnd = nowEpochMillis + CalendarSyncPolicy.WINDOW_MILLIS
        val uriBuilder = CalendarContract.Instances.CONTENT_URI.buildUpon()
        ContentUris.appendId(uriBuilder, nowEpochMillis)
        ContentUris.appendId(uriBuilder, windowEnd)
        val projection = arrayOf(
            CalendarContract.Instances.TITLE,
            CalendarContract.Instances.BEGIN,
            CalendarContract.Instances.END,
            CalendarContract.Instances.ALL_DAY,
        )
        return try {
            context.contentResolver.query(
                uriBuilder.build(),
                projection,
                null,
                null,
                "${CalendarContract.Instances.BEGIN} ASC",
            )?.use { cursor ->
                val titleColumn = cursor.getColumnIndexOrThrow(
                    CalendarContract.Instances.TITLE,
                )
                val beginColumn = cursor.getColumnIndexOrThrow(
                    CalendarContract.Instances.BEGIN,
                )
                val endColumn = cursor.getColumnIndexOrThrow(
                    CalendarContract.Instances.END,
                )
                val allDayColumn = cursor.getColumnIndexOrThrow(
                    CalendarContract.Instances.ALL_DAY,
                )
                buildList {
                    while (cursor.moveToNext()) {
                        add(
                            CalendarCandidate(
                                title = cursor.getString(titleColumn).orEmpty(),
                                startEpochMillis = cursor.getLong(beginColumn),
                                endEpochMillis = cursor.getLong(endColumn),
                                allDay = cursor.getInt(allDayColumn) != 0,
                            ),
                        )
                    }
                }.take(CalendarSyncPolicy.MAX_ENTRIES)
            } ?: emptyList()
        } catch (_: SecurityException) {
            null
        }
    }
}
