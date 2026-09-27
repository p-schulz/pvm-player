# Windows counterpart of build_apk.sh: builds the Android APK end to end,
# cores included. Fetches whatever the app itself needs that
# scripts\setup.ps1 -Android doesn't already handle (the vendored headers,
# libmpv, and -- unless told not to -- libretro cores for the GAMES menu),
# then runs the Gradle build. Assumes the Android SDK/NDK are already set up;
# run scripts\setup.ps1 -Android first if they aren't.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build_apk.ps1
#       release APK with gambatte + bsnes
#   ... -Debug                    a debug build instead
#   ... -Cores gambatte,mgba      specific cores instead of the default two
#   ... -NoCores                  no game cores at all (smaller APK, no bundled GPL code)
#   ... -Force                    redo every fetch step, not just what's missing
#
# Output: dist\pvm-player-<debug|release>.apk
param(
    [switch]$Debug,
    [switch]$NoCores,
    [switch]$Force,
    [string[]]$Cores = @('gambatte', 'bsnes')
)
$ErrorActionPreference = 'Stop'
$Config = if ($Debug) { 'debug' } else { 'release' }

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Write-Step([string]$name) { Write-Host ''; Write-Host "== $name ==" }

# ------------------------------------------------------- 1. vendored sources -
Write-Step 'Vendored sources'
$needThirdparty = -not ((Test-Path 'thirdparty\glad\src\gl.c') -and (Test-Path 'thirdparty\imgui\imgui.cpp') -and
                        (Test-Path 'thirdparty\libretro\libretro.h') -and (Test-Path 'thirdparty\miniaudio\miniaudio.h'))
if ($Force -or $needThirdparty) {
    $fetchArgs = @{}
    if ($Force) { $fetchArgs['Force'] = $true }
    & (Join-Path $PSScriptRoot 'fetch_thirdparty.ps1') @fetchArgs
} else {
    Write-Host 'already present'
}

# ---------------------------------------------------------- 2. libmpv (Android) -
Write-Step 'libmpv (Android)'
if ($Force -or -not (Test-Path 'thirdparty\libmpv-android\lib\arm64-v8a\libmpv.so')) {
    $libmpvArgs = @{}
    if ($Force) { $libmpvArgs['Force'] = $true }
    & (Join-Path $Root 'android\fetch_libmpv.ps1') @libmpvArgs
} else {
    Write-Host 'already present'
}

# --------------------------------------------------- 3. libretro cores (Android) -
if ($NoCores) {
    Write-Step 'Game cores'
    Write-Host 'skipped (-NoCores): the GAMES menu will report no cores found'
    # A bundle left over from an earlier build would otherwise still get
    # packaged: Gradle's jniLibs source dir is whatever's on disk right now.
    Remove-Item -Recurse -Force 'thirdparty\cores-android' -ErrorAction SilentlyContinue
} else {
    Write-Step "Game cores ($($Cores -join ', '))"
    if ($Force) {
        Remove-Item -Recurse -Force 'thirdparty\cores-android' -ErrorAction SilentlyContinue
    }
    & (Join-Path $PSScriptRoot 'fetch_cores.ps1') -Android -Cores $Cores
    Write-Host ''
    Write-Host 'Note: bundling these into an APK you pass on to someone else is a'
    Write-Host 'GPL-covered combination (gambatte and bsnes are GPL) -- fine for your'
    Write-Host "own devices; see README.md's Games section for the licensing note."
}

# -------------------------------------------------------------------- 4. build -
Write-Step "Gradle ($Config)"
$task = if ($Config -eq 'release') { 'assembleRelease' } else { 'assembleDebug' }
Push-Location (Join-Path $Root 'android')
try {
    & .\gradlew.bat --console=plain ":app:$task"
    if ($LASTEXITCODE -ne 0) { throw "gradlew failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}

$apk = Join-Path $Root "android\app\build\outputs\apk\$Config\app-$Config.apk"
if (-not (Test-Path $apk)) { throw "Expected APK not found: $apk" }
$distDir = Join-Path $Root 'dist'
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$out = Join-Path $distDir "pvm-player-$Config.apk"
Copy-Item $apk $out -Force

Write-Step 'Done'
$sizeMb = [math]::Round((Get-Item $out).Length / 1MB, 1)
Write-Host "  $out ($sizeMb MB)"
if (-not $NoCores) {
    Write-Host "  cores bundled: $($Cores -join ', ')"
}
Write-Host "  Install:  adb install -r $out"
