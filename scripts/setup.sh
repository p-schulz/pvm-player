#!/usr/bin/env bash
# PVM Player -- development environment setup for macOS and Linux (including
# Raspberry Pi OS). Windows: scripts\setup.bat.
#
# What it does, in order:
#   1. checks the tools and libraries the build needs and says what is missing
#      (--install-deps installs them with Homebrew / apt / dnf / pacman)
#   2. restores thirdparty/ (Dear ImGui, glad, nlohmann/json) if it is missing
#   3. writes CMakeUserPresets.json: the "local-debug" / "local-release" build
#      presets for this machine (Ninja if installed, Wayland only if usable)
#   4. writes .vscode/{tasks,launch,settings,extensions}.json from
#      scripts/templates/vscode/ (existing files are kept; --force replaces them)
#   5. configures the project (cmake --preset local-debug)
#   6. --android additionally sets up the Android SDK/NDK, libmpv for Android
#      and android/local.properties
#
# Usage: scripts/setup.sh [options]
#   --install-deps      install missing packages (asks first; uses sudo on Linux)
#   -y, --yes           don't ask before installing
#   --build [debug|release]
#                       also build (default: debug) and run the tests
#   --android           also set up the Android build
#   --no-vscode         don't write .vscode/
#   --force             overwrite existing .vscode/ files and hand-edited presets
#   --check             only report what is present and missing; change nothing
#   --dry-run           print the install commands instead of running them
#   -h, --help
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Versions the Android build is pinned to (see android/app/build.gradle.kts).
ANDROID_NDK_VERSION="27.0.12077973"
ANDROID_CMAKE_VERSION="3.22.1"
ANDROID_PLATFORM="android-36"
ANDROID_BUILD_TOOLS="36.0.0"
ANDROID_CMDLINE_TOOLS_ZIP_MAC="commandlinetools-mac-11076708_latest.zip"
ANDROID_CMDLINE_TOOLS_ZIP_LINUX="commandlinetools-linux-11076708_latest.zip"
MIN_CMAKE="3.21"   # 3.20 builds the project; presets need 3.21

INSTALL_DEPS=0 ASSUME_YES=0 DO_BUILD=0 BUILD_CONFIG=debug ANDROID=0 VSCODE=1 FORCE=0 CHECK_ONLY=0 DRY_RUN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --install-deps) INSTALL_DEPS=1 ;;
        -y|--yes) ASSUME_YES=1 ;;
        --build)
            DO_BUILD=1
            if [ "${2:-}" = debug ] || [ "${2:-}" = release ]; then BUILD_CONFIG="$2"; shift; fi ;;
        --android) ANDROID=1 ;;
        --no-vscode) VSCODE=0 ;;
        --force) FORCE=1 ;;
        --check) CHECK_ONLY=1 ;;
        --dry-run) DRY_RUN=1 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

# ------------------------------------------------------------------ output --
if [ -t 1 ]; then
    C_OK=$'\033[32m'; C_WARN=$'\033[33m'; C_ERR=$'\033[31m'; C_BOLD=$'\033[1m'; C_OFF=$'\033[0m'
else
    C_OK=''; C_WARN=''; C_ERR=''; C_BOLD=''; C_OFF=''
fi
step()  { printf '\n%s== %s%s\n' "$C_BOLD" "$*" "$C_OFF"; }
ok()    { printf '  %s[ ok ]%s %s\n' "$C_OK" "$C_OFF" "$*"; }
warn()  { printf '  %s[warn]%s %s\n' "$C_WARN" "$C_OFF" "$*"; }
bad()   { printf '  %s[miss]%s %s\n' "$C_ERR" "$C_OFF" "$*"; }
die()   { printf '%serror:%s %s\n' "$C_ERR" "$C_OFF" "$*" >&2; exit 1; }

MISSING_REQUIRED=()   # things the build cannot do without
MISSING_OPTIONAL=()   # nice to have
need()   { MISSING_REQUIRED+=("$1"); bad "$1"; }
want()   { MISSING_OPTIONAL+=("$1"); warn "$1"; }

have() { command -v "$1" >/dev/null 2>&1; }
run()  { if [ "$DRY_RUN" = 1 ]; then printf '  $ %s\n' "$*"; else "$@"; fi; }

# $1 >= $2 as dotted version numbers
version_ge() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        na = split(a, x, "."); nb = split(b, y, ".");
        for (i = 1; i <= (na > nb ? na : nb); i++) {
            if ((x[i] + 0) > (y[i] + 0)) exit 0
            if ((x[i] + 0) < (y[i] + 0)) exit 1
        }
        exit 0 }'
}

confirm() {
    [ "$ASSUME_YES" = 1 ] && return 0
    [ -t 0 ] || return 1
    printf '%s [y/N] ' "$1"; read -r reply; [ "$reply" = y ] || [ "$reply" = Y ]
}

# ---------------------------------------------------------------- platform --
OS="$(printf '%s' "${PVM_SETUP_OS:-$(uname -s)}" | tr '[:upper:]' '[:lower:]')"   # the override lets the Linux logic be dry-run on a Mac
case "$OS" in
    darwin) PLATFORM=macos ;;
    linux)  PLATFORM=linux ;;
    *) die "unsupported OS '$OS'. On Windows run scripts\\setup.bat." ;;
esac

PM="${PVM_SETUP_PM:-}"
if [ -z "$PM" ]; then
    if [ "$PLATFORM" = macos ]; then have brew && PM=brew
    elif have apt-get; then PM=apt
    elif have dnf; then PM=dnf
    elif have pacman; then PM=pacman
    fi
fi

SUDO=""
if [ "$PLATFORM" = linux ] && [ "$(id -u)" != 0 ] && have sudo; then SUDO="sudo"; fi

printf '%sPVM Player setup%s  (%s%s, repository: %s)\n' "$C_BOLD" "$C_OFF" "$PLATFORM" "${PM:+, packages via $PM}" "$ROOT"

# ----------------------------------------------------------------- 1. tools --
step "Build tools"

if [ "$PLATFORM" = macos ] && ! xcode-select -p >/dev/null 2>&1; then
    need "Xcode command line tools (run: xcode-select --install)"
fi

if have cmake; then
    CMAKE_VERSION="$(cmake --version | awk 'NR==1 {print $3}')"
    if version_ge "$CMAKE_VERSION" "$MIN_CMAKE"; then ok "cmake $CMAKE_VERSION"
    else need "cmake >= $MIN_CMAKE (found $CMAKE_VERSION)"; fi
else
    need "cmake >= $MIN_CMAKE"
fi

CXX_BIN=""
for c in "${CXX:-}" c++ clang++ g++; do
    [ -n "$c" ] && have "$c" && { CXX_BIN="$c"; break; }
done
if [ -n "$CXX_BIN" ]; then
    CXX_TMP="$(mktemp -d)"
    printf 'int main() { return 0; }\n' > "$CXX_TMP/t.cpp"
    if "$CXX_BIN" -std=c++17 "$CXX_TMP/t.cpp" -o "$CXX_TMP/t" >/dev/null 2>&1; then
        ok "C++17 compiler: $CXX_BIN ($("$CXX_BIN" --version 2>&1 | head -1))"
    else
        need "a working C++17 compiler ($CXX_BIN does not compile a test program)"
    fi
    rm -rf "$CXX_TMP"
else
    need "a C++17 compiler (clang++ or g++)"
fi

for t in git curl; do
    if have "$t"; then ok "$t"; else need "$t"; fi
done
if have ninja; then ok "ninja (used for builds)"; else want "ninja (optional; faster builds -- Makefiles are used without it)"; fi

# ----------------------------------------------------------- libraries ------
step "Libraries"

pkg() { have pkg-config && pkg-config --exists "$@" 2>/dev/null; }
WAYLAND_OK=0

if [ "$PLATFORM" = macos ]; then
    if [ -f /opt/homebrew/include/mpv/client.h ] || [ -f /usr/local/include/mpv/client.h ] ||
       [ -f thirdparty/libmpv/include/mpv/client.h ]; then
        ok "libmpv"
    else
        need "libmpv (brew install mpv)"
    fi
    ok "libcurl (part of the macOS SDK)"
else
    if pkg mpv || [ -f /usr/include/mpv/client.h ] || [ -f /usr/local/include/mpv/client.h ] ||
       [ -f thirdparty/libmpv/include/mpv/client.h ]; then ok "libmpv"; else need "libmpv development files"; fi
    if pkg libcurl; then ok "libcurl development files"; else need "libcurl development files"; fi
    if have pkg-config; then
        if pkg x11 xrandr xinerama xcursor xi gl; then ok "X11 + OpenGL development files (for GLFW)"
        else need "X11 / OpenGL development files (for GLFW)"; fi
        if pkg wayland-client wayland-cursor wayland-egl xkbcommon && pkg wayland-protocols && have wayland-scanner; then
            WAYLAND_OK=1; ok "Wayland development files (GLFW builds its Wayland backend too)"
        else
            warn "Wayland development files not found; GLFW is built for X11 only (still works under XWayland)"
        fi
    else
        need "pkg-config"
    fi
fi
if pkg pugixml 2>/dev/null || [ -f /opt/homebrew/include/pugixml.hpp ] || [ -f /usr/include/pugixml.hpp ]; then
    ok "pugixml"
else
    warn "pugixml not installed; CMake fetches and builds it (needs network)"
fi

# --------------------------------------------------------- install missing ---
install_packages() {
    case "$PM" in
        brew)
            run brew install cmake ninja mpv pugixml ;;
        apt)
            run $SUDO apt-get update
            run $SUDO apt-get install -y build-essential cmake ninja-build git curl pkg-config \
                libmpv-dev libcurl4-openssl-dev libpugixml-dev \
                libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev \
                libwayland-dev libxkbcommon-dev wayland-protocols libwayland-bin ;;
        dnf)
            run $SUDO dnf install -y gcc-c++ make cmake ninja-build git curl pkgconf-pkg-config \
                mpv-libs-devel libcurl-devel pugixml-devel \
                libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel mesa-libGL-devel \
                wayland-devel libxkbcommon-devel wayland-protocols-devel
            [ "$DRY_RUN" = 1 ] || warn "mpv-libs-devel comes from RPM Fusion; enable it if dnf could not find it" ;;
        pacman)
            run $SUDO pacman -S --needed --noconfirm base-devel cmake ninja git curl pkgconf mpv pugixml \
                libx11 libxrandr libxinerama libxcursor libxi mesa wayland libxkbcommon wayland-protocols ;;
        *)
            die "no supported package manager found (Homebrew, apt, dnf or pacman); install the missing items by hand" ;;
    esac
}

if [ "${#MISSING_REQUIRED[@]}" -gt 0 ] || { [ "${#MISSING_OPTIONAL[@]}" -gt 0 ] && [ "$INSTALL_DEPS" = 1 ]; }; then
    if [ "$CHECK_ONLY" = 1 ]; then
        :
    elif [ "$INSTALL_DEPS" = 1 ]; then
        step "Installing packages ($PM)"
        if [ "$DRY_RUN" = 1 ] || confirm "Install the packages listed above with $PM?"; then
            [ "$PLATFORM" = macos ] && [ -z "$PM" ] && die "Homebrew is required on macOS: https://brew.sh"
            install_packages
        else
            die "cancelled"
        fi
        [ "$DRY_RUN" = 1 ] || { echo; echo "Packages installed; run scripts/setup.sh again to re-check and continue."; exit 0; }
    else
        echo
        echo "Required items are missing. Re-run with --install-deps to install them"
        echo "with ${PM:-your package manager}, or install them yourself."
        exit 1
    fi
fi
[ "$CHECK_ONLY" = 1 ] && [ "${#MISSING_REQUIRED[@]}" -gt 0 ] && { echo; echo "(--check: nothing changed)"; exit 1; }

# ---------------------------------------------------------- Android (used by --android) -------
setup_android() {
    step "Android"
    # JDK
    if have java && java -version 2>&1 | head -1 | grep -Eq '"(1[7-9]|[2-9][0-9])[.\"]'; then
        ok "JDK: $(java -version 2>&1 | head -1)"
    else
        need "JDK 17 or newer (Gradle and the Android plugin need it)"
        if [ "$INSTALL_DEPS" = 1 ] && [ "$CHECK_ONLY" = 0 ]; then
            case "$PM" in
                brew) run brew install openjdk@17 ;;
                apt) run $SUDO apt-get install -y openjdk-17-jdk ;;
                dnf) run $SUDO dnf install -y java-17-openjdk-devel ;;
                pacman) run $SUDO pacman -S --needed --noconfirm jdk17-openjdk ;;
            esac
        fi
    fi

    # SDK location
    local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
    if [ -z "$sdk" ]; then
        for d in "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do [ -d "$d" ] && { sdk="$d"; break; }; done
    fi
    [ -z "$sdk" ] && { [ "$PLATFORM" = macos ] && sdk="$HOME/Library/Android/sdk" || sdk="$HOME/Android/Sdk"; }

    local sdkmanager="$sdk/cmdline-tools/latest/bin/sdkmanager"
    if [ ! -x "$sdkmanager" ]; then
        if [ "$INSTALL_DEPS" = 1 ] && [ "$CHECK_ONLY" = 0 ] && { [ "$DRY_RUN" = 1 ] || confirm "Download the Android command-line tools into $sdk?"; }; then
            local zip="$ANDROID_CMDLINE_TOOLS_ZIP_LINUX"
            [ "$PLATFORM" = macos ] && zip="$ANDROID_CMDLINE_TOOLS_ZIP_MAC"
            run mkdir -p "$sdk/cmdline-tools"
            if [ "$DRY_RUN" = 1 ]; then
                echo "  \$ download https://dl.google.com/android/repository/$zip and unpack to $sdk/cmdline-tools/latest"
            else
                local tmp; tmp="$(mktemp -d)"
                curl -fL --progress-bar -o "$tmp/tools.zip" "https://dl.google.com/android/repository/$zip"
                unzip -q "$tmp/tools.zip" -d "$tmp"
                rm -rf "$sdk/cmdline-tools/latest"; mv "$tmp/cmdline-tools" "$sdk/cmdline-tools/latest"
                rm -rf "$tmp"
            fi
        else
            need "Android SDK command-line tools in $sdk (or set ANDROID_HOME; --install-deps downloads them)"
        fi
    else
        ok "Android SDK: $sdk"
    fi

    # Packages the Gradle project is pinned to
    local missing_pkgs=()
    [ -d "$sdk/platforms/$ANDROID_PLATFORM" ] || [ -d "$sdk/platforms/${ANDROID_PLATFORM}.0" ] || missing_pkgs+=("platforms;$ANDROID_PLATFORM")
    [ -d "$sdk/build-tools/$ANDROID_BUILD_TOOLS" ] || missing_pkgs+=("build-tools;$ANDROID_BUILD_TOOLS")
    [ -d "$sdk/ndk/$ANDROID_NDK_VERSION" ] || missing_pkgs+=("ndk;$ANDROID_NDK_VERSION")
    [ -d "$sdk/cmake/$ANDROID_CMAKE_VERSION" ] || missing_pkgs+=("cmake;$ANDROID_CMAKE_VERSION")
    [ -d "$sdk/platform-tools" ] || missing_pkgs+=("platform-tools")
    if [ "${#missing_pkgs[@]}" = 0 ]; then
        ok "SDK packages: platform $ANDROID_PLATFORM, build-tools $ANDROID_BUILD_TOOLS, NDK $ANDROID_NDK_VERSION, CMake $ANDROID_CMAKE_VERSION, platform-tools"
    elif [ -x "$sdkmanager" ] || [ "$DRY_RUN" = 1 ]; then
        if [ "$INSTALL_DEPS" = 1 ] && [ "$CHECK_ONLY" = 0 ]; then
            echo "  installing: ${missing_pkgs[*]}"
            if [ "$DRY_RUN" = 1 ]; then
                echo "  \$ yes | $sdkmanager --licenses; $sdkmanager ${missing_pkgs[*]}"
            else
                yes | "$sdkmanager" --licenses >/dev/null 2>&1 || true
                "$sdkmanager" "${missing_pkgs[@]}"
            fi
        else
            for p in "${missing_pkgs[@]}"; do need "Android SDK package $p (--install-deps installs it)"; done
        fi
    fi

    # Gradle finds the SDK through local.properties (git-ignored)
    if [ -d "$sdk" ] && [ "$DRY_RUN" = 0 ] && [ "$CHECK_ONLY" = 0 ]; then
        printf 'sdk.dir=%s\n' "$sdk" > android/local.properties
        ok "wrote android/local.properties (sdk.dir=$sdk)"
    fi

    # libmpv for Android + headers + CA bundle
    if [ "$CHECK_ONLY" = 1 ]; then
        [ -f thirdparty/libmpv-android/lib/arm64-v8a/libmpv.so ] && ok "libmpv for Android" || warn "libmpv for Android not fetched yet (android/fetch_libmpv.sh)"
    elif [ "$DRY_RUN" = 1 ]; then echo "  \$ android/fetch_libmpv.sh"
    else ./android/fetch_libmpv.sh; fi

    if [ "$DO_BUILD" = 1 ] && [ "$DRY_RUN" = 0 ] && [ "${#MISSING_REQUIRED[@]}" = 0 ]; then
        ( cd android && ./gradlew --console=plain ":app:assemble$([ "$BUILD_CONFIG" = release ] && echo Release || echo Debug)" )
    fi
}

# ------------------------------------------------------------ 2. thirdparty --
step "Vendored sources (thirdparty/)"
NEED_THIRDPARTY=0
[ -f thirdparty/glad/src/gl.c ] || NEED_THIRDPARTY=1
[ -f thirdparty/imgui/imgui.cpp ] && [ -f thirdparty/imgui/backends/imgui_impl_glfw.cpp ] || NEED_THIRDPARTY=1
if [ "$NEED_THIRDPARTY" = 0 ]; then
    ok "glad, Dear ImGui"
    [ -f thirdparty/json/nlohmann/json.hpp ] && ok "nlohmann/json" || warn "nlohmann/json not vendored; CMake uses a system copy or fetches it"
elif [ "$CHECK_ONLY" = 1 ]; then
    bad "thirdparty/ is incomplete (setup.sh without --check restores it)"
elif [ "$DRY_RUN" = 1 ]; then
    echo "  \$ scripts/fetch_thirdparty.sh"
else
    echo "  restoring from upstream (scripts/fetch_thirdparty.sh) ..."
    "$ROOT/scripts/fetch_thirdparty.sh"
fi

if [ "$CHECK_ONLY" = 1 ]; then
    if [ "$ANDROID" = 1 ]; then setup_android; fi
    echo; echo "(--check: nothing changed)"
    [ "${#MISSING_REQUIRED[@]}" = 0 ]; exit $?
fi

# ----------------------------------------------------------- 3. presets ------
write_user_presets() {
    local target="CMakeUserPresets.json"
    if [ -f "$target" ] && ! grep -q '"pvm-player/setup"' "$target" && [ "$FORCE" = 0 ]; then
        warn "kept your own $target (no generated marker; --force replaces it)"
        return
    fi
    local generator="" wayland=""
    have ninja && generator='"generator": "Ninja",'
    if [ "$PLATFORM" = linux ] && [ "$WAYLAND_OK" = 0 ]; then
        wayland='"GLFW_BUILD_WAYLAND": "OFF"'
    fi
    local cache=""
    [ -n "$wayland" ] && cache=", \"cacheVariables\": { $wayland }"
    cat > "$target" <<EOF
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "vendor": { "pvm-player/setup": { "generatedBy": "scripts/setup.sh", "note": "regenerated by the setup script; edit freely, it will not overwrite a file without this marker" } },
  "configurePresets": [
    { "name": "local-debug", "displayName": "Debug (this machine)", "inherits": "debug", ${generator} "hidden": false${cache} },
    { "name": "local-release", "displayName": "Release (this machine)", "inherits": "release", ${generator} "hidden": false${cache} }
  ],
  "buildPresets": [
    { "name": "local-debug", "configurePreset": "local-debug", "configuration": "Debug" },
    { "name": "local-release", "configurePreset": "local-release", "configuration": "Release" }
  ],
  "testPresets": [
    { "name": "local-debug", "configurePreset": "local-debug", "configuration": "Debug", "output": { "outputOnFailure": true } },
    { "name": "local-release", "configurePreset": "local-release", "configuration": "Release", "output": { "outputOnFailure": true } }
  ]
}
EOF
    ok "wrote $target (${generator:+Ninja, }${wayland:+X11-only GLFW, }presets local-debug / local-release)"
}

step "CMake presets"
write_user_presets

# ---------------------------------------------------------- 4. VS Code -------
step "VS Code"
if [ "$VSCODE" = 0 ]; then
    echo "  skipped (--no-vscode)"
else
    mkdir -p .vscode
    for f in tasks.json launch.json settings.json extensions.json; do
        src="scripts/templates/vscode/$f"
        dst=".vscode/$f"
        if [ ! -f "$dst" ] || [ "$FORCE" = 1 ]; then
            cp "$src" "$dst"; ok "wrote $dst"
        elif cmp -s "$src" "$dst"; then
            ok "$dst is up to date"
        else
            warn "kept your $dst (differs from the template; --force replaces it)"
        fi
    done
fi

# --------------------------------------------------------- 5. configure ------
step "Configure"
cmake --preset local-debug
[ "$BUILD_CONFIG" = release ] && cmake --preset local-release

if [ "$ANDROID" = 1 ]; then setup_android; fi

# ------------------------------------------------------------- 7. build ------
if [ "$DO_BUILD" = 1 ]; then
    step "Build ($BUILD_CONFIG)"
    cmake --build --preset "local-$BUILD_CONFIG" --parallel
    step "Tests"
    ctest --preset "local-$BUILD_CONFIG"
fi

# ---------------------------------------------------------------- summary ----
step "Done"
if [ "${#MISSING_REQUIRED[@]}" -gt 0 ]; then
    warn "still missing: ${MISSING_REQUIRED[*]}"
fi
cat <<EOF
  Build (CLI):   cmake --build --preset local-$BUILD_CONFIG
  Run:           ./build/local-$BUILD_CONFIG/pvm_player [media folder ...]
  Test:          ctest --preset local-$BUILD_CONFIG
  Other config:  cmake --preset local-release && cmake --build --preset local-release
  VS Code:       open this folder and install the recommended extensions, then
                 Ctrl/Cmd+Shift+B builds, F5 debugs; Terminal > Run Task lists the rest
                 (including the "Android: ..." build and adb tasks)
EOF
if [ "$ANDROID" = 1 ]; then
    cat <<EOF
  Android:       cd android && ./gradlew :app:assembleRelease
                 adb install -r app/build/outputs/apk/release/app-release.apk
EOF
fi
