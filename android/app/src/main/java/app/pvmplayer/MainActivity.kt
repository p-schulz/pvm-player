package app.pvmplayer

import android.os.Bundle
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
