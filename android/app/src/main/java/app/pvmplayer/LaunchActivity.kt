package app.pvmplayer

import android.app.Activity
import android.app.ActivityOptions
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.Display

/**
 * The launcher entry. It has no UI: it starts [MainActivity] and finishes.
 *
 * On a dual-screen handheld, Android puts an app on the display of the
 * launcher that started it, which may be the bottom screen. The app is meant
 * for the primary (top) display, so when this runs on another display it
 * starts MainActivity there instead -- unless the user turned that off in
 * Settings ("Launch On Top Screen"). Doing it here, rather than in MainActivity,
 * means the native code never starts on the wrong display only to be closed
 * again.
 */
class LaunchActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val intent = Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        var options: Bundle? = null

        val displayId = currentDisplayId()
        if (displayId != Display.DEFAULT_DISPLAY && LaunchPreference.launchOnTopScreen(this)) {
            Log.i(TAG, "Started on display $displayId; moving to the primary display")
            options = ActivityOptions.makeBasic().setLaunchDisplayId(Display.DEFAULT_DISPLAY).toBundle()
        } else {
            Log.i(TAG, "Started on display $displayId; staying")
        }
        startActivity(intent, options)
        finish()
    }

    @Suppress("DEPRECATION")
    private fun currentDisplayId(): Int =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            display?.displayId ?: Display.DEFAULT_DISPLAY
        } else {
            windowManager.defaultDisplay.displayId
        }

    private companion object {
        const val TAG = "PVM"
    }
}
