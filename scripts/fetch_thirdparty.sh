#!/usr/bin/env bash
# Restores the vendored third-party sources under thirdparty/ from upstream:
#
#   imgui/  Dear ImGui 1.92.8: the core, the demo window and the GLFW,
#           OpenGL 3 and Android backends (only the files the build uses)
#   json/   nlohmann/json 3.11.3, the single header
#   glad/   the OpenGL 3.3 core loader, generated with the glad2 tool
#           (needs python3; installed into a throwaway virtualenv)
#
# Only what is missing is fetched (--force redoes everything). setup.sh runs
# this for you; it is only needed by hand if you skipped setup.
set -euo pipefail

IMGUI_TAG=v1.92.8
JSON_VERSION=v3.11.3
JSON_SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="${PVM_THIRDPARTY:-$ROOT/thirdparty}"
FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
download() { curl -fL --retry 3 --progress-bar -o "$2" "$1"; }

fetch_imgui() {
    if [ "$FORCE" = 0 ] && [ -f "$TP/imgui/imgui.cpp" ] && [ -f "$TP/imgui/backends/imgui_impl_glfw.cpp" ] &&
       [ -f "$TP/imgui/backends/imgui_impl_opengl3.cpp" ] && [ -f "$TP/imgui/backends/imgui_impl_android.cpp" ]; then
        echo "imgui: present"; return
    fi
    echo "imgui $IMGUI_TAG"
    download "https://github.com/ocornut/imgui/archive/refs/tags/$IMGUI_TAG.tar.gz" "$TMP/imgui.tar.gz"
    mkdir -p "$TMP/imgui-src" && tar -xzf "$TMP/imgui.tar.gz" -C "$TMP/imgui-src" --strip-components=1
    mkdir -p "$TP/imgui/backends"
    for f in imgui.cpp imgui.h imgui_internal.h imconfig.h imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
             imgui_demo.cpp imstb_rectpack.h imstb_textedit.h imstb_truetype.h LICENSE.txt; do
        cp "$TMP/imgui-src/$f" "$TP/imgui/$f"
    done
    for f in imgui_impl_glfw.cpp imgui_impl_glfw.h imgui_impl_opengl3.cpp imgui_impl_opengl3.h \
             imgui_impl_opengl3_loader.h imgui_impl_android.cpp imgui_impl_android.h; do
        cp "$TMP/imgui-src/backends/$f" "$TP/imgui/backends/$f"
    done
}

fetch_json() {
    if [ "$FORCE" = 0 ] && [ -f "$TP/json/nlohmann/json.hpp" ]; then echo "json: present"; return; fi
    echo "nlohmann/json $JSON_VERSION"
    download "https://github.com/nlohmann/json/releases/download/$JSON_VERSION/json.hpp" "$TMP/json.hpp"
    if [ "$(sha256 "$TMP/json.hpp")" != "$JSON_SHA256" ]; then
        echo "checksum mismatch for json.hpp" >&2; exit 1
    fi
    mkdir -p "$TP/json/nlohmann"
    cp "$TMP/json.hpp" "$TP/json/nlohmann/json.hpp"
    download "https://raw.githubusercontent.com/nlohmann/json/$JSON_VERSION/LICENSE.MIT" "$TP/json/LICENSE.txt"
}

fetch_glad() {
    if [ "$FORCE" = 0 ] && [ -f "$TP/glad/src/gl.c" ] && [ -f "$TP/glad/include/glad/gl.h" ] &&
       [ -f "$TP/glad/include/KHR/khrplatform.h" ]; then
        echo "glad: present"; return
    fi
    command -v python3 >/dev/null 2>&1 || { echo "glad: python3 is needed to generate it (or restore thirdparty/glad from version control)" >&2; exit 1; }
    echo "glad (generating with glad2)"
    python3 -m venv "$TMP/venv"
    "$TMP/venv/bin/pip" install --quiet glad2
    "$TMP/venv/bin/glad" --api gl:core=3.3 --extensions "" --out-path "$TMP/glad" c
    mkdir -p "$TP/glad"
    cp -R "$TMP/glad/include" "$TMP/glad/src" "$TP/glad/"
    cat > "$TP/glad/README.md" <<'README'
# glad (vendored)

Pre-generated OpenGL 3.3 core loader (no extensions), generated once via the
`glad2` CLI (`pip install glad2`, then
`glad --api gl:core=3.3 --extensions "" --out-path . c`) and committed here
so the build doesn't need Python/network access to regenerate it.

Regenerate only if the required GL version/profile changes
(scripts/fetch_thirdparty.sh --force).
README
}

fetch_imgui
fetch_json
fetch_glad
echo "thirdparty/ is complete: $TP"
