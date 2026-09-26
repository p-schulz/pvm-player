package app.pvmplayer

import android.content.Context
import java.io.File

/**
 * Reads the one setting the Java side needs before the native app has run:
 * "launch_on_top_screen" in config.cfg, the same file (and key) the native
 * settings screen writes. Missing file or key means the default, on.
 */
object LaunchPreference {
    private const val KEY = "launch_on_top_screen"

    fun launchOnTopScreen(context: Context): Boolean {
        val config = File(context.filesDir, "config.cfg")
        if (!config.isFile) return true
        return try {
            val value = config.useLines { lines ->
                lines.map { it.trim() }
                    .firstOrNull { it.startsWith("$KEY=") }
                    ?.substringAfter('=')
                    ?.trim()
            }
            // The same spellings as the native parser (settings.cpp) accepts as "off".
            value !in setOf("false", "0", "off")
        } catch (e: Exception) {
            true
        }
    }
}
