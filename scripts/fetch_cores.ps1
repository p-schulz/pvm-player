# Windows counterpart of fetch_cores.sh: downloads libretro cores (emulators)
# for x86-64 Windows into cores\, where the player finds them.
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_cores.ps1 [-Android] [core ...]
# With no names, the defaults: gambatte (GB/GBC), bsnes (SNES), parallel_n64
# (N64), swanstation (PS1). The cores come from
# https://buildbot.libretro.com/nightly/ ("latest" builds, so nothing is
# pinned); each has its own licence -- all four defaults are GPL (see
# THIRD_PARTY_LICENSES.md). N64 and PS1 both need real hardware (OpenGL/GLES)
# rendering support in the player, and PS1 additionally needs a BIOS image of
# your own in retro/system/ next to your saves -- see README.md.
# $env:PVM_CORES_DEST overrides the destination folder.
# -Android fetches the arm64 Android builds for the APK instead (see fetch_cores.sh).
param([switch]$Android, [string[]]$Cores = @('gambatte', 'bsnes', 'parallel_n64', 'swanstation'))
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$Dest = if ($env:PVM_CORES_DEST) { $env:PVM_CORES_DEST }
        elseif ($Android) { Join-Path (Join-Path (Join-Path $Root 'thirdparty') 'cores-android') 'arm64-v8a' }
        else { Join-Path $Root 'cores' }
$Base = 'https://buildbot.libretro.com/nightly'
$Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('pvm-cores-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $Tmp, $Dest | Out-Null

# One core missing for this platform shouldn't stop the others from being
# fetched; only fail the whole run if none land.
$failed = @()
try {
    foreach ($core in $Cores) {
        $file = "${core}_libretro.dll"
        $remote = "windows/x86_64/latest/$file"
        $target = $file
        if ($Android) {
            $file = "${core}_libretro_android.so"
            $remote = "android/latest/arm64-v8a/$file"
            $target = "lib$file"   # Android only unpacks lib*.so
        }
        Write-Host "$core ($(if ($Android) { 'android/arm64-v8a' } else { 'windows/x86_64' }))"
        $zip = Join-Path $Tmp "$file.zip"
        try {
            Invoke-WebRequest -UseBasicParsing -Uri "$Base/$remote.zip" -OutFile $zip
        } catch {
            Write-Warning "no such core on the buildbot: $file"
            $failed += $core
            continue
        }
        Expand-Archive -Path $zip -DestinationPath $Tmp -Force
        Copy-Item (Join-Path $Tmp $file) (Join-Path $Dest $target) -Force
        Write-Host "  -> $(Join-Path $Dest $target)"
    }
} finally {
    Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
}
if ($failed.Count -gt 0) {
    Write-Warning "Not available for this platform: $($failed -join ', ')"
    if ($failed.Count -eq $Cores.Count) { exit 1 }
}
