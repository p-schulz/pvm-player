package app.pvmplayer

import android.content.Context
import android.content.res.AssetManager
import java.io.File

/**
 * Unpacks the APK's assets (shaders, fonts, default configs) to
 * filesDir/assets, which is what the native side reads through
 * Platform::assetDir(). The existing loaders open ordinary files, so this
 * spares them any AAssetManager code. Re-extracted whenever the app has been
 * (re)installed.
 */
object AssetExtractor {
    // Entries the framework reports under the asset root that aren't ours.
    private val systemEntries = setOf("images", "sounds", "webkit")

    fun extractIfNeeded(context: Context) {
        val target = File(context.filesDir, "assets")
        val stamp = File(context.filesDir, "assets.stamp")
        val installed = context.packageManager.getPackageInfo(context.packageName, 0).lastUpdateTime.toString()

        if (target.isDirectory && stamp.isFile && stamp.readText() == installed) {
            return
        }
        target.deleteRecursively()
        copyTree(context.assets, "", target)
        stamp.writeText(installed)
    }

    private fun copyTree(assets: AssetManager, path: String, target: File) {
        val children = assets.list(path) ?: return
        if (children.isEmpty()) {
            // A file: `path` was reached through its parent's listing.
            target.parentFile?.mkdirs()
            assets.open(path).use { input -> target.outputStream().use { input.copyTo(it) } }
            return
        }
        target.mkdirs()
        for (name in children) {
            if (path.isEmpty() && name in systemEntries) continue
            copyTree(assets, if (path.isEmpty()) name else "$path/$name", File(target, name))
        }
    }
}
