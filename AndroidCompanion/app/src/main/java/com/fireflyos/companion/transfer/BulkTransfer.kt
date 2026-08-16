package com.fireflyos.companion.transfer

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLConnection
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.InternalCoroutinesApi
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.job
import kotlinx.coroutines.withContext
import kotlin.coroutines.coroutineContext

enum class BulkTransferMode(val wireId: Int) {
    SharedLan(1),
    SoftAp(2);

    companion object {
        fun fromWireId(value: Int): BulkTransferMode? =
            entries.firstOrNull { it.wireId == value }
    }
}

data class BulkTransferSession(
    val requestId: Int,
    val mode: BulkTransferMode,
    val expiresInMillis: Long,
    val endpoint: String,
    val tokenHex: String,
    val softApSsid: String = "",
    val softApPassword: String = "",
)

data class BulkTransferStatus(
    val requestId: Int,
    val state: Int,
    val failure: Int,
)

data class BulkTransferRequest(
    val requestId: Int,
    val preferSharedLan: Boolean,
    val declaredSize: Long,
    val sha256Hex: String,
    val managedPath: String,
)

class BulkTransferLaunchGuard {
    private var requestId = 0
    private var networkPending = false
    private var uploadStarted = false

    @Synchronized
    fun reset(requestId: Int) {
        this.requestId = requestId
        networkPending = false
        uploadStarted = false
    }

    @Synchronized
    fun claimNetworkRequest(requestId: Int): Boolean {
        if (requestId != this.requestId || networkPending || uploadStarted) return false
        networkPending = true
        return true
    }

    @Synchronized
    fun claimNetworkAvailable(requestId: Int): Boolean {
        if (requestId != this.requestId || !networkPending || uploadStarted) return false
        networkPending = false
        uploadStarted = true
        return true
    }

    @Synchronized
    fun claimNetworkUnavailable(requestId: Int): Boolean {
        if (requestId != this.requestId || !networkPending || uploadStarted) return false
        networkPending = false
        return true
    }

    @Synchronized
    fun claimDirectUpload(requestId: Int): Boolean {
        if (requestId != this.requestId || networkPending || uploadStarted) return false
        uploadStarted = true
        return true
    }

    @Synchronized
    fun clear() {
        requestId = 0
        networkPending = false
        uploadStarted = false
    }
}

object BulkTransferCodec {
    private const val SCHEMA = 2
    private const val REQUEST = 1
    private const val READY = 2
    private const val STATUS = 3
    private const val CANCEL = 4
    private const val MAX_ENDPOINT_BYTES = 95
    private const val TOKEN_HEX_BYTES = 32
    private const val MAX_SSID_BYTES = 23
    private const val MAX_PASSWORD_BYTES = 15
    private const val MAX_PATH_BYTES = 191
    private const val MAX_FILE_BYTES = 64L * 1024L * 1024L
    private val sha256Hex = Regex("[0-9a-f]{64}")
    private val allowedPath = Regex(
        "^/FireflyOS/(Themes|Pictures|Music|Updates)/[^/\\\\:*?%#]+$",
    )

    fun encodeRequestOrNull(request: BulkTransferRequest): ByteArray? {
        val path = request.managedPath.toByteArray(Charsets.UTF_8)
        if (request.requestId !in 1..0xFFFF ||
            request.declaredSize !in 1..MAX_FILE_BYTES ||
            !sha256Hex.matches(request.sha256Hex) ||
            !allowedPath.matches(request.managedPath) ||
            request.managedPath.contains("..") ||
            request.managedPath.endsWith(".part") ||
            path.size !in 1..MAX_PATH_BYTES
        ) return null
        val digest = ByteArray(32)
        for (index in digest.indices) {
            digest[index] = request.sha256Hex.substring(index * 2, index * 2 + 2)
                .toInt(16).toByte()
        }
        return ByteBuffer.allocate(46 + path.size)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(SCHEMA.toByte())
            .put(REQUEST.toByte())
            .putShort(request.requestId.toShort())
            .put(if (request.preferSharedLan) 1.toByte() else 0.toByte())
            .putLong(request.declaredSize)
            .put(digest)
            .put(path.size.toByte())
            .put(path)
            .array()
    }

    fun encodeCancelOrNull(requestId: Int): ByteArray? =
        requestId.takeIf { it in 1..0xFFFF }?.let {
            ByteBuffer.allocate(4)
                .order(ByteOrder.LITTLE_ENDIAN)
                .put(SCHEMA.toByte())
                .put(CANCEL.toByte())
                .putShort(it.toShort())
                .array()
        }

    fun decodeSession(payload: ByteArray): BulkTransferSession? {
        if (payload.size < 2 + 2 + 1 + 8 + 1 + TOKEN_HEX_BYTES + 2) return null
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        if ((buffer.get().toInt() and 0xFF) != SCHEMA ||
            (buffer.get().toInt() and 0xFF) != READY
        ) return null
        val requestId = buffer.short.toInt() and 0xFFFF
        if (requestId == 0) return null
        val mode = BulkTransferMode.fromWireId(buffer.get().toInt() and 0xFF)
            ?: return null
        val expires = buffer.long
        if (expires !in 1..(15L * 60L * 1000L)) return null
        val endpoint = takeUtf8(buffer, MAX_ENDPOINT_BYTES, allowEmpty = false) ?: return null
        if (buffer.remaining() < TOKEN_HEX_BYTES + 2) return null
        val tokenBytes = ByteArray(TOKEN_HEX_BYTES)
        buffer.get(tokenBytes)
        val token = tokenBytes.toString(Charsets.US_ASCII)
        if (!token.matches(Regex("[0-9a-f]{32}"))) return null
        val ssid = takeUtf8(buffer, MAX_SSID_BYTES, allowEmpty = true) ?: return null
        val password = takeUtf8(buffer, MAX_PASSWORD_BYTES, allowEmpty = true) ?: return null
        if (buffer.hasRemaining() ||
            (mode == BulkTransferMode.SoftAp && (ssid.isEmpty() || password.length < 8)) ||
            (mode == BulkTransferMode.SharedLan && (ssid.isNotEmpty() || password.isNotEmpty()))
        ) return null
        return BulkTransferSession(requestId, mode, expires, endpoint, token,
            ssid, password)
    }

    fun decodeStatus(payload: ByteArray): BulkTransferStatus? =
        payload.takeIf {
            it.size == 6 && (it[0].toInt() and 0xFF) == SCHEMA &&
                (it[1].toInt() and 0xFF) == STATUS
        }?.let {
            val buffer = ByteBuffer.wrap(it).order(ByteOrder.LITTLE_ENDIAN)
            buffer.position(2)
            val requestId = buffer.short.toInt() and 0xFFFF
            if (requestId == 0) null else BulkTransferStatus(
                requestId,
                buffer.get().toInt() and 0xFF,
                buffer.get().toInt() and 0xFF,
            )
        }

    private fun takeUtf8(buffer: ByteBuffer, maxBytes: Int, allowEmpty: Boolean): String? {
        if (!buffer.hasRemaining()) return null
        val length = buffer.get().toInt() and 0xFF
        if (length > maxBytes || (!allowEmpty && length == 0) || buffer.remaining() < length) {
            return null
        }
        val bytes = ByteArray(length)
        buffer.get(bytes)
        val text = bytes.toString(Charsets.UTF_8)
        return text.takeIf { it.toByteArray(Charsets.UTF_8).contentEquals(bytes) && !it.contains('\uFFFD') }
    }
}

sealed interface BulkUploadResult {
    data object Success : BulkUploadResult
    data class Rejected(val httpStatus: Int) : BulkUploadResult
    data class NetworkError(val message: String) : BulkUploadResult
}

object BulkTransferUploader {
    private const val BUFFER_BYTES = 64 * 1024
    private val allowedPath = Regex(
        "^/FireflyOS/(Themes|Pictures|Music|Updates)/[^/\\\\:*?%#]+$",
    )
    private val sha256Hex = Regex("[0-9a-f]{64}")

    @OptIn(InternalCoroutinesApi::class)
    suspend fun upload(
        session: BulkTransferSession,
        managedPath: String,
        declaredSize: Long,
        sha256: String,
        input: InputStream,
        onProgress: (Long, Long) -> Unit = { _, _ -> },
        openConnection: (URL) -> URLConnection = { it.openConnection() },
    ): BulkUploadResult = withContext(Dispatchers.IO) {
        if (!allowedPath.matches(managedPath) || managedPath.contains("..") ||
            managedPath.endsWith(".part") ||
            declaredSize !in 1..(64L * 1024L * 1024L) ||
            !sha256Hex.matches(sha256)
        ) {
            return@withContext BulkUploadResult.Rejected(400)
        }
        val endpointUrl = runCatching { URL(session.endpoint) }.getOrNull()
        if (endpointUrl == null || endpointUrl.protocol != "http" ||
            endpointUrl.path != "/upload" ||
            endpointUrl.port !in listOf(-1, 80) ||
            !isPrivateIpv4(endpointUrl.host)
        ) {
            return@withContext BulkUploadResult.Rejected(400)
        }
        val connection = try {
            val boundary = "Firefly${session.tokenHex.take(8)}"
            val prefix = (
                "--$boundary\r\n" +
                    "Content-Disposition: form-data; name=\"file\"; filename=\"upload.bin\"\r\n" +
                    "Content-Type: application/octet-stream\r\n\r\n"
                ).toByteArray(Charsets.US_ASCII)
            val suffix = "\r\n--$boundary--\r\n".toByteArray(Charsets.US_ASCII)
            (openConnection(endpointUrl) as HttpURLConnection).apply {
                requestMethod = "POST"
                connectTimeout = 15_000
                readTimeout = 15_000
                doOutput = true
                setFixedLengthStreamingMode(declaredSize + prefix.size + suffix.size)
                setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
                setRequestProperty("Authorization", "Bearer ${session.tokenHex}")
                setRequestProperty("X-Firefly-Path", managedPath)
                setRequestProperty("X-Firefly-Size", declaredSize.toString())
                setRequestProperty("X-Firefly-SHA256", sha256)
            }
        } catch (error: Exception) {
            return@withContext BulkUploadResult.NetworkError(
                error.message ?: "Unable to open local transfer endpoint",
            )
        }
        val cancellationHandle = coroutineContext.job.invokeOnCompletion(
            onCancelling = true,
            invokeImmediately = true,
        ) { cause ->
            if (cause is CancellationException) connection.disconnect()
        }
        try {
            val boundary = "Firefly${session.tokenHex.take(8)}"
            val prefix = (
                "--$boundary\r\n" +
                    "Content-Disposition: form-data; name=\"file\"; filename=\"upload.bin\"\r\n" +
                    "Content-Type: application/octet-stream\r\n\r\n"
                ).toByteArray(Charsets.US_ASCII)
            val suffix = "\r\n--$boundary--\r\n".toByteArray(Charsets.US_ASCII)
            val buffer = ByteArray(BUFFER_BYTES)
            var sent = 0L
            connection.outputStream.use { output ->
                output.write(prefix)
                while (sent < declaredSize) {
                    coroutineContext.ensureActive()
                    val count = input.read(
                        buffer,
                        0,
                        minOf(buffer.size.toLong(), declaredSize - sent).toInt(),
                    )
                    if (count <= 0) break
                    output.write(buffer, 0, count)
                    sent += count
                    onProgress(sent, declaredSize)
                }
                if (sent == declaredSize) output.write(suffix)
            }
            if (sent != declaredSize) return@withContext BulkUploadResult.Rejected(400)
            val status = connection.responseCode
            if (status == HttpURLConnection.HTTP_CREATED) BulkUploadResult.Success
            else BulkUploadResult.Rejected(status)
        } catch (error: CancellationException) {
            throw error
        } catch (error: Exception) {
            BulkUploadResult.NetworkError(error.message ?: "Local transfer failed")
        } finally {
            cancellationHandle.dispose()
            connection.disconnect()
        }
    }

    private fun isPrivateIpv4(host: String): Boolean {
        val octets = host.split('.').map { it.toIntOrNull() ?: return false }
        if (octets.size != 4 || octets.any { it !in 0..255 }) return false
        return octets[0] == 10 ||
            (octets[0] == 172 && octets[1] in 16..31) ||
            (octets[0] == 192 && octets[1] == 168)
    }
}
