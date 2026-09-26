plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// The native side is the repository's own CMake project, not a copy: the same
// src/ that the desktop build compiles, with the Android branch of the root
// CMakeLists.txt selected by the NDK toolchain.
val repoRoot: File = rootDir.parentFile

android {
    namespace = "app.pvmplayer"
    compileSdk = 36
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "app.pvmplayer"
        minSdk = 30  // all-files access (MANAGE_EXTERNAL_STORAGE) exists from Android 11
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"

        // The AYN Thor (Snapdragon 8 Gen 2) is arm64-only.
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                // GameActivity requires the shared C++ runtime.
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = File(repoRoot, "CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        prefab = true  // games-activity ships its native glue as a prefab package
    }

    // The prebuilt libmpv (android/fetch_libmpv.sh), packaged as-is. Loaded
    // through the DT_NEEDED entry of libpvm_player.so, so it only has to be
    // in the APK's native library directory.
    sourceSets["main"].jniLibs.srcDir(File(repoRoot, "thirdparty/libmpv-android/lib"))

    buildTypes {
        release {
            isMinifyEnabled = false
            // Not a Play Store build: signed with the debug key so the APK
            // installs directly (sideloading), but optimized, unlike debug.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/pvmAssets"))
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation("androidx.games:games-activity:4.4.2")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.appcompat:appcompat:1.7.1")
}

// The read-only files the app loads from disk (shaders, fonts, default news
// configs) are packaged from the repository itself in the same layout the
// desktop build puts next to its executable, so the loaders need no
// Android-specific paths. AssetExtractor unpacks them on first run.
val copyPvmAssets by tasks.registering(Copy::class) {
    into(layout.buildDirectory.dir("generated/pvmAssets"))
    from(File(repoRoot, "src/shaders")) { into("shaders") }
    from(File(repoRoot, "assets")) { into("assets") }
    from(File(repoRoot, "conf")) {
        include("news.cfg", "tagesschau.cfg", "ard.cfg", "zdf.cfg")
        rename { it.replace(".cfg", ".default.cfg") }
    }
    // CA bundle for libmpv's https streams (fetched with libmpv itself).
    from(File(repoRoot, "thirdparty/libmpv-android")) { include("cacert.pem") }
}

tasks.matching { it.name == "preBuild" || (it.name.startsWith("merge") && it.name.endsWith("Assets")) }
    .configureEach { dependsOn(copyPvmAssets) }
