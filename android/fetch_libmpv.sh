#!/bin/sh
# Downloads what the Android build needs to play media into
# thirdparty/libmpv-android/ (git-ignored):
#
#   lib/arm64-v8a/libmpv.so   prebuilt libmpv with FFmpeg inside (media-kit's
#                             "full" build: mpv 0.36, MediaCodec, mbedTLS)
#   include/mpv/*.h           the matching mpv 0.36.0 client/render headers
#   cacert.pem                Mozilla's CA bundle, for HTTPS streams
#
# Without these the Android build still compiles, with playback disabled.
# Run it once, from anywhere:  android/fetch_libmpv.sh
# PVM_THIRDPARTY=<dir> changes where thirdparty/ is (default: the repository's).
set -eu

MEDIA_KIT_TAG=v1.1.11
MEDIA_KIT_JAR=full-arm64-v8a.jar
MEDIA_KIT_SHA256=cdb54c5cf24725623ca717bbbd6d991031d625a377460bd128f19c2dffe189bd
MPV_TAG=v0.36.0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${PVM_THIRDPARTY:-$ROOT/thirdparty}/libmpv-android"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$DEST/lib/arm64-v8a" "$DEST/include/mpv"

echo "libmpv $MEDIA_KIT_TAG ($MEDIA_KIT_JAR)"
curl -fL --progress-bar -o "$TMP/libmpv.jar" \
    "https://github.com/media-kit/libmpv-android-video-build/releases/download/$MEDIA_KIT_TAG/$MEDIA_KIT_JAR"
ACTUAL="$(shasum -a 256 "$TMP/libmpv.jar" | cut -d' ' -f1)"
if [ "$ACTUAL" != "$MEDIA_KIT_SHA256" ]; then
    echo "checksum mismatch for $MEDIA_KIT_JAR: got $ACTUAL" >&2
    exit 1
fi
unzip -qo "$TMP/libmpv.jar" lib/arm64-v8a/libmpv.so -d "$TMP/jar"
cp "$TMP/jar/lib/arm64-v8a/libmpv.so" "$DEST/lib/arm64-v8a/libmpv.so"

for header in client.h render.h render_gl.h; do
    echo "mpv $MPV_TAG headers: $header"
    curl -fsL -o "$DEST/include/mpv/$header" \
        "https://raw.githubusercontent.com/mpv-player/mpv/$MPV_TAG/libmpv/$header"
done

echo "CA bundle"
curl -fsL -o "$DEST/cacert.pem" https://curl.se/ca/cacert.pem

echo "done: $DEST"
