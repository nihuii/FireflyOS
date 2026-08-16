package com.fireflyos.companion.transfer

import java.io.InputStream
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class BulkTransferUploaderTest {
    @Test
    fun streamsRepresentativeSizesWithBoundedReadBuffer() = runBlocking {
        val sizes = listOf(1L * 1024L, 1L * 1024L * 1024L, 32L * 1024L * 1024L)
        for (size in sizes) {
            val input = GeneratedInputStream(size)
            val connection = RecordingHttpConnection(URL("http://192.168.4.1/upload"))
            val result = BulkTransferUploader.upload(
                session = BulkTransferSession(
                    requestId = 7,
                    mode = BulkTransferMode.SharedLan,
                    expiresInMillis = 60_000,
                    endpoint = "http://192.168.4.1/upload",
                    tokenHex = "0123456789abcdef0123456789abcdef",
                ),
                managedPath = "/FireflyOS/Pictures/generated.bin",
                declaredSize = size,
                sha256 = "0".repeat(64),
                input = input,
                openConnection = { connection },
            )
            assertEquals(BulkUploadResult.Success, result)
            assertEquals(size, input.totalRead)
            assertTrue(input.maxRequestedBytes <= 64 * 1024)
            assertTrue(connection.writtenBytes > size)
        }
    }

    @Test
    fun cancellationDisconnectsAStalledOutputStream() = runBlocking {
        val connection = StallingHttpConnection(URL("http://192.168.4.1/upload"))
        val upload = async(Dispatchers.Default) {
            BulkTransferUploader.upload(
                session = BulkTransferSession(
                    requestId = 8,
                    mode = BulkTransferMode.SharedLan,
                    expiresInMillis = 60_000,
                    endpoint = "http://192.168.4.1/upload",
                    tokenHex = "0123456789abcdef0123456789abcdef",
                ),
                managedPath = "/FireflyOS/Pictures/stalled.bin",
                declaredSize = 1024,
                sha256 = "0".repeat(64),
                input = GeneratedInputStream(1024),
                openConnection = { connection },
            )
        }
        assertTrue(connection.writeStarted.await(2, TimeUnit.SECONDS))
        upload.cancel()
        withTimeout(2_000) { upload.join() }
        assertTrue(connection.disconnected)
    }

    private class GeneratedInputStream(size: Long) : InputStream() {
        private var remaining = size
        var totalRead = 0L
            private set
        var maxRequestedBytes = 0
            private set

        override fun read(): Int {
            if (remaining == 0L) return -1
            --remaining
            ++totalRead
            return 0x5A
        }

        override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
            if (remaining == 0L) return -1
            maxRequestedBytes = maxOf(maxRequestedBytes, length)
            val count = minOf(remaining, length.toLong()).toInt()
            java.util.Arrays.fill(buffer, offset, offset + count, 0x5A.toByte())
            remaining -= count
            totalRead += count
            return count
        }
    }

    private class RecordingHttpConnection(url: URL) : HttpURLConnection(url) {
        private val sink = object : OutputStream() {
            override fun write(value: Int) {
                ++writtenBytes
            }

            override fun write(buffer: ByteArray, offset: Int, length: Int) {
                writtenBytes += length
            }
        }
        var writtenBytes = 0L
            private set

        override fun getOutputStream(): OutputStream = sink
        override fun getResponseCode(): Int = HTTP_CREATED
        override fun disconnect() = Unit
        override fun usingProxy(): Boolean = false
        override fun connect() = Unit
    }

    private class StallingHttpConnection(url: URL) : HttpURLConnection(url) {
        val writeStarted = CountDownLatch(1)
        private val released = CountDownLatch(1)
        @Volatile var disconnected = false
            private set
        private val sink = object : OutputStream() {
            override fun write(value: Int) = stall()

            override fun write(buffer: ByteArray, offset: Int, length: Int) = stall()

            private fun stall() {
                writeStarted.countDown()
                released.await()
            }
        }

        override fun getOutputStream(): OutputStream = sink
        override fun getResponseCode(): Int = HTTP_CREATED
        override fun disconnect() {
            disconnected = true
            released.countDown()
        }
        override fun usingProxy(): Boolean = false
        override fun connect() = Unit
    }
}
