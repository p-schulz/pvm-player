#!/usr/bin/env bash
# Builds the Android APK end to end, cores included: fetches whatever the app
# itself needs that scripts/setup.sh --android doesn't already handle (the
# vendored headers, libmpv, and -- unless told not to -- libretro cores for
# the GAMES menu), then runs the Gradle build. Assumes the Android SDK/NDK are
# already set up; run scripts/setup.sh --android first if they aren't.
#
#   scripts/build_apk.sh                  release APK with the default cores (see below)
#   scripts/build_apk.sh --debug          a debug build instead
#   scripts/build_apk.sh gambatte mgba    specific cores instead of the default two
#   scripts/build_apk.sh --no-cores       no game cores at all (smaller APK, no bundled GPL code)
#   scripts/build_apk.sh --force          redo every fetch step, not just what's missing
#
# Output: dist/pvm-player-<debug|release>.apk
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CONFIG=release
NO_CORES=0
FORCE=0
CORES=()
for arg in "$@"; do
    case "$arg" in
        --debug) CONFIG=debug ;;
        --release) CONFIG=release ;;
        --no-cores) NO_CORES=1 ;;
        --force) FORCE=1 ;;
        --help|-h) sed -n '2,14p' "$0"; exit 0 ;;
        -*) echo "Unknown option: $arg (--help for usage)" >&2; exit 1 ;;
        *) CORES+=("$arg") ;;
    esac
done
[ ${#CORES[@]} -gt 0 ] || CORES=(gambatte bsnes parallel_n64 swanstation)

step() { printf '\n== %s ==\n' "$1"; }
force_flag() { [ "$FORCE" = 1 ] && echo --force || true; }

# ------------------------------------------------------- 1. vendored sources -
step "Vendored sources"
NEED_THIRDPARTY=0
[ -f thirdparty/glad/src/gl.c ] || NEED_THIRDPARTY=1
[ -f thirdparty/imgui/imgui.cpp ] || NEED_THIRDPARTY=1
[ -f thirdparty/libretro/libretro.h ] || NEED_THIRDPARTY=1
[ -f thirdparty/miniaudio/miniaudio.h ] || NEED_THIRDPARTY=1
if [ "$FORCE" = 1 ] || [ "$NEED_THIRDPARTY" = 1 ]; then
    scripts/fetch_thirdparty.sh $(force_flag)
else
    echo "already present"
fi

# ---------------------------------------------------------- 2. libmpv (Android) -
step "libmpv (Android)"
if [ "$FORCE" = 1 ] || [ ! -f thirdparty/libmpv-android/lib/arm64-v8a/libmpv.so ]; then
    android/fetch_libmpv.sh $(force_flag)
else
    echo "already present"
fi

# --------------------------------------------------- 3. libretro cores (Android) -
if [ "$NO_CORES" = 1 ]; then
    step "Game cores"
    echo "skipped (--no-cores): the GAMES menu will report no cores found"
    # A bundle left over from an earlier build would otherwise still get
    # packaged: Gradle's jniLibs source dir is whatever's on disk right now.
    rm -rf thirdparty/cores-android
else
    step "Game cores (${CORES[*]})"
    if [ "$FORCE" = 1 ]; then
        rm -rf thirdparty/cores-android
    fi
    scripts/fetch_cores.sh --android "${CORES[@]}"
    echo
    echo "Note: bundling these into an APK you pass on to someone else is a"
    echo "GPL-covered combination (all four default cores are GPL) -- fine for your"
    echo "own devices; see README.md's Games section for the licensing note."
fi

# -------------------------------------------------------------------- 4. build -
step "Gradle ($CONFIG)"
GRADLE_TASK="assemble$([ "$CONFIG" = release ] && echo Release || echo Debug)"
( cd android && ./gradlew --console=plain ":app:$GRADLE_TASK" )

APK="android/app/build/outputs/apk/$CONFIG/app-$CONFIG.apk"
[ -f "$APK" ] || { echo "Expected APK not found: $APK" >&2; exit 1; }
mkdir -p dist
OUT="dist/pvm-player-$CONFIG.apk"
cp "$APK" "$OUT"

step "Done"
SIZE="$(du -h "$OUT" | cut -f1)"
echo "  $OUT ($SIZE)"
if [ "$NO_CORES" = 0 ]; then
    echo "  cores bundled: ${CORES[*]}"
fi
echo "  Install:  adb install -r $OUT"
