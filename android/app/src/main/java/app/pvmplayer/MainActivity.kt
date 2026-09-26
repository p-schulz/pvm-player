package app.pvmplayer

import android.content.ActivityNotFoundException
import android.content.Intent
import android.hardware.display.DisplayManager
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.os.storage.StorageManager
import android.provider.Settings
import android.view.Display
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.google.androidgamesdk.GameActivity

/**
 * The whole app is native: GameActivity starts libpvm_player.so's
 * android_main() on its own thread. This class only does what has to happen
 * on the Java side -- unpack the bundled files before the native code starts,
 * and keep the system bars hidden.
 */
class MainActivity : GameActivity() {
    companion object {
        init {
            System.loadLibrary("pvm_player")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        // Before super.onCreate(), which is what launches the native thread.
        AssetExtractor.extractIfNeeded(this)
        super.onCreate(savedInstanceState)
        askForStorageAccessOnce()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemBars()
        }
    }

    /** Called from the native thread (Platform::setKeepAwake) -- window flags must change on the UI thread. */
    @Suppress("unused")
    fun keepScreenOn(on: Boolean) {
        runOnUiThread {
            if (on) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            } else {
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }
    }

    /** The app's cache directory, for Platform::cacheDir(): the OS may clear it, the page caches cope. */
    @Suppress("unused")
    fun cachePath(): String = cacheDir.absolutePath

    /**
     * Whether the app may read shared storage by plain path (the "all files
     * access" permission). Media is opened by path, by the file browser and by
     * libmpv, so this is what makes them work. Fine for sideloading; the Play
     * Store restricts the permission (see the port plan's Phase 7 for the
     * Storage Access Framework alternative).
     */
    @Suppress("unused")
    fun hasStorageAccess(): Boolean = Environment.isExternalStorageManager()

    /** Opens the system screen where the user grants that permission. Callable from the native thread. */
    @Suppress("unused")
    fun requestStorageAccess() {
        runOnUiThread { openAllFilesAccessSettings() }
    }

    /**
     * The media folders the file browser starts from, for
     * Platform::defaultMediaRoots(): Movies and Music on the primary storage
     * (when they exist) and the root of every mounted removable volume.
     */
    @Suppress("unused", "DEPRECATION")
    fun mediaRoots(): Array<String> {
        val roots = mutableListOf<String>()
        for (type in listOf(Environment.DIRECTORY_MOVIES, Environment.DIRECTORY_MUSIC)) {
            val directory = Environment.getExternalStoragePublicDirectory(type)
            if (directory.isDirectory) roots.add(directory.path)
        }
        val storage = getSystemService(STORAGE_SERVICE) as StorageManager
        for (volume in storage.storageVolumes) {
            if (volume.isPrimary || volume.state != Environment.MEDIA_MOUNTED) continue
            volume.directory?.let { roots.add(it.path) }
        }
        if (roots.isEmpty()) roots.add(Environment.getExternalStorageDirectory().path)
        return roots.toTypedArray()
    }

    // Asks once per install, at the first launch; after that the app re-asks
    // only when the user picks Play Media without having granted access.
    private fun askForStorageAccessOnce() {
        if (Environment.isExternalStorageManager()) return
        val prefs = getSharedPreferences("pvm", MODE_PRIVATE)
        if (prefs.getBoolean("storage_access_asked", false)) return
        prefs.edit().putBoolean("storage_access_asked", true).apply()
        openAllFilesAccessSettings()
    }

    private fun openAllFilesAccessSettings() {
        try {
            startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName")))
        } catch (e: ActivityNotFoundException) {
            // Some builds only have the list of all apps.
            startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
        }
    }

    /**
     * The connected displays, primary first, for Platform::displayNames().
     * Called from the native thread. Nothing offers them for choosing yet
     * (the app is always fullscreen on the display it was launched on); this
     * is where a later "launch on display N" setting gets its list.
     */
    @Suppress("unused")
    fun displayNames(): Array<String> {
        val manager = getSystemService(DISPLAY_SERVICE) as DisplayManager
        val others = manager.displays
            .filter { it.displayId != Display.DEFAULT_DISPLAY }
            .sortedBy { it.displayId }
            .map { "${it.name} (#${it.displayId})" }
        return (listOf("Primary") + others).toTypedArray()
    }

    // Immersive sticky: bars stay hidden, and a swipe from the edge shows them
    // transiently.
    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
    }
}
