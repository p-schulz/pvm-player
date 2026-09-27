#!/usr/bin/env bash
# Downloads libretro cores (emulators) for this machine into cores/, where the
# player finds them (a development build looks there; a copy of the player
# looks in a "cores" folder next to itself, or in $PVM_CORES_DIR).
#
#   scripts/fetch_cores.sh                 # gambatte (Game Boy / Color) and bsnes (SNES)
#   scripts/fetch_cores.sh gambatte mgba   # any core from the libretro buildbot
#   scripts/fetch_cores.sh --list          # what is already in cores/
#   scripts/fetch_cores.sh --android       # the arm64 Android builds, for the APK
#
# --android puts them in thirdparty/cores-android/arm64-v8a/ as lib<name>.so,
# where the Gradle project packages them into the APK (Android only loads code
# from the app's own native library directory).
#
# The cores come from https://buildbot.libretro.com/nightly/ ("latest" builds,
# so nothing here is pinned); each has its own licence -- gambatte and bsnes
# are GPL. They are downloaded for you to run, not bundled with the player.
# PVM_CORES_DEST overrides the destination folder.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${PVM_CORES_DEST:-$ROOT/cores}"
BASE="https://buildbot.libretro.com/nightly"

if [ "${1:-}" = "--list" ]; then
    ls -1 "$DEST" 2>/dev/null || echo "(no cores yet: $DEST)"
    exit 0
fi
ANDROID=0
if [ "${1:-}" = "--android" ]; then
    ANDROID=1
    shift
    DEST="${PVM_CORES_DEST:-$ROOT/thirdparty/cores-android/arm64-v8a}"
fi
CORES=("$@")
[ ${#CORES[@]} -gt 0 ] || CORES=(gambatte bsnes)

if [ "$ANDROID" = 1 ]; then
    PLATFORM="android"
    EXT="so"
else
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)              PLATFORM="apple/osx/arm64";    EXT="dylib" ;;
    Darwin-x86_64)             PLATFORM="apple/osx/x86_64";   EXT="dylib" ;;
    Linux-x86_64)              PLATFORM="linux/x86_64";       EXT="so" ;;
    Linux-aarch64|Linux-arm64) PLATFORM="linux/aarch64";      EXT="so" ;;
    # 32-bit Raspberry Pi OS; bsnes has no build here, gambatte does.
    Linux-armv7l|Linux-armv6l) PLATFORM="linux/armv7-neon-hf"; EXT="so" ;;
    MINGW*-x86_64|MSYS*-x86_64) PLATFORM="windows/x86_64";    EXT="dll" ;;
    *) echo "No libretro buildbot builds for $(uname -s) $(uname -m)" >&2; exit 1 ;;
esac
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$DEST"

# One core missing for this platform (e.g. bsnes on a 32-bit Pi) shouldn't
# stop the others from being fetched; only fail the whole run if none land.
FAILED=()
for core in "${CORES[@]}"; do
    file="${core}_libretro.${EXT}"
    remote="$PLATFORM/latest/$file"
    target="$file"
    if [ "$ANDROID" = 1 ]; then
        file="${core}_libretro_android.so"
        remote="android/latest/arm64-v8a/$file"
        target="lib$file"  # Android only unpacks lib*.so
    fi
    echo "$core ($PLATFORM)"
    if ! curl -fL --retry 3 --progress-bar -o "$TMP/$file.zip" "$BASE/$remote.zip"; then
        echo "  no such core on the buildbot: $file" >&2
        FAILED+=("$core")
        continue
    fi
    unzip -qo "$TMP/$file.zip" -d "$TMP"
    mv "$TMP/$file" "$DEST/$target"
    echo "  -> $DEST/$target"
done

if [ ${#FAILED[@]} -gt 0 ]; then
    echo "Not available for this platform: ${FAILED[*]}" >&2
    if [ ${#FAILED[@]} -eq ${#CORES[@]} ]; then
        exit 1
    fi
fi
