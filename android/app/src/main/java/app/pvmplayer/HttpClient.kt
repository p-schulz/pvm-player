package app.pvmplayer

import android.os.SystemClock
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL

/**
 * Blocking HTTP GET / JSON POST for the native teletext services, behaving
 * like the libcurl transport on desktop (teletext/http_fetch.cpp): redirects
 * over http and https only (at most [MAX_REDIRECTS]), a POST turning into a
 * GET on 301/302/303, a response size cap, connect and total timeouts, and
 * cancellation through a native flag. Called from native worker threads via
 * JNI (platform/android/http_fetch_android.cpp).
 */
object HttpClient {
    /** [error] is null on success; [body] is only set then. [status] is 0 if no response arrived. */
    class Result(
        @JvmField val status: Int,
        @JvmField val body: ByteArray?,
        @JvmField val error: String?,
    )

    private const val MAX_REDIRECTS = 5
    private const val WATCH_INTERVAL_MS = 50L
    private val REDIRECT_CODES = setOf(301, 302, 303, 307, 308)

    /** Reads the native `std::atomic<bool>` whose address is [cancelToken]; registered from native code. */
    @JvmStatic
    external fun nativeIsCancelled(cancelToken: Long): Boolean

    /**
     * @param cancelToken address of a native atomic<bool> that turns true to
     *   abort the request, or 0 for none.
     */
    @JvmStatic
    fun request(
        url: String,
        postBody: ByteArray?,
        userAgent: String,
        connectTimeoutMs: Int,
        totalTimeoutMs: Int,
        maxBodyBytes: Long,
        cancelToken: Long,
    ): Result {
        val watchdog = Watchdog(cancelToken, totalTimeoutMs)
        watchdog.start()
        var status = 0
        try {
            var currentUrl = url
            var body = postBody
            var redirects = 0
            while (true) {
                val target = URL(currentUrl)
                require(target.protocol == "http" || target.protocol == "https") { "unsupported protocol ${target.protocol}" }

                val connection = target.openConnection() as HttpURLConnection
                watchdog.connection = connection
                // A cancel that landed between two hops found no connection to close.
                watchdog.throwIfAborted()

                // Redirects are followed by hand: HttpURLConnection refuses
                // http <-> https hops, which curl (and feed hosts) rely on.
                connection.instanceFollowRedirects = false
                connection.connectTimeout = connectTimeoutMs
                connection.readTimeout = totalTimeoutMs
                connection.setRequestProperty("User-Agent", userAgent)
                if (body != null) {
                    connection.requestMethod = "POST"
                    connection.doOutput = true
                    connection.setRequestProperty("Content-Type", "application/json")
                    connection.setFixedLengthStreamingMode(body.size)
                    connection.outputStream.use { it.write(body) }
                }

                status = connection.responseCode
                val location = connection.getHeaderField("Location")
                if (status in REDIRECT_CODES && location != null) {
                    connection.disconnect()
                    if (++redirects > MAX_REDIRECTS) {
                        return Result(status, null, "maximum ($MAX_REDIRECTS) redirects followed")
                    }
                    currentUrl = URL(target, location).toString()
                    if (status != 307 && status != 308) {
                        body = null  // like curl: the redirected request is a plain GET
                    }
                    continue
                }
                if (status !in 200..299) {
                    connection.disconnect()
                    return Result(status, null, "HTTP status $status")
                }
                return readBody(connection, status, maxBodyBytes, watchdog)
            }
        } catch (e: Exception) {
            val message = when {
                watchdog.cancelled -> "cancelled"
                watchdog.timedOut || e is SocketTimeoutException -> "timed out after ${totalTimeoutMs / 1000}s"
                else -> e.message ?: e.javaClass.simpleName
            }
            return Result(status, null, message)
        } finally {
            watchdog.finish()
        }
    }

    private fun readBody(connection: HttpURLConnection, status: Int, maxBodyBytes: Long, watchdog: Watchdog): Result {
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(16 * 1024)
        try {
            connection.inputStream.use { input ->
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    if (out.size().toLong() + n > maxBodyBytes) {
                        return Result(status, null, "response larger than the configured size limit")
                    }
                    out.write(buffer, 0, n)
                }
            }
        } finally {
            connection.disconnect()
        }
        watchdog.throwIfAborted()
        return Result(status, out.toByteArray(), null)
    }

    /**
     * Enforces the total timeout and polls the native cancel flag every
     * [WATCH_INTERVAL_MS]; on either it disconnects the current connection,
     * which makes a blocked connect or read throw at once (a plain read
     * timeout would only notice after its full duration).
     */
    private class Watchdog(private val cancelToken: Long, private val totalTimeoutMs: Int) : Thread("http-watchdog") {
        @Volatile var connection: HttpURLConnection? = null
        @Volatile var cancelled = false
        @Volatile var timedOut = false
        @Volatile private var finished = false

        init {
            isDaemon = true
        }

        override fun run() {
            val deadline = SystemClock.elapsedRealtime() + totalTimeoutMs
            while (!finished) {
                if (cancelToken != 0L && nativeIsCancelled(cancelToken)) {
                    cancelled = true
                } else if (SystemClock.elapsedRealtime() >= deadline) {
                    timedOut = true
                }
                if (cancelled || timedOut) {
                    connection?.disconnect()
                    return
                }
                try {
                    sleep(WATCH_INTERVAL_MS)
                } catch (_: InterruptedException) {
                    return
                }
            }
        }

        fun throwIfAborted() {
            if (cancelled || timedOut) {
                throw java.io.IOException(if (cancelled) "cancelled" else "timed out")
            }
        }

        fun finish() {
            finished = true
            interrupt()
        }
    }
}
