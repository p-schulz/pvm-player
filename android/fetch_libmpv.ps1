# Windows counterpart of fetch_libmpv.sh: downloads libmpv for Android, the
# matching mpv headers and a CA bundle into thirdparty\libmpv-android\.
# Keep the pinned versions in sync with fetch_libmpv.sh.
#   powershell -ExecutionPolicy Bypass -File android\fetch_libmpv.ps1 [-Force]
param([switch]$Force)
$ErrorActionPreference = 'Stop'

$MediaKitTag    = 'v1.1.11'
$MediaKitJar    = 'full-arm64-v8a.jar'
$MediaKitSha256 = 'cdb54c5cf24725623ca717bbbd6d991031d625a377460bd128f19c2dffe189bd'
$MpvTag         = 'v0.36.0'

$Root = Split-Path -Parent $PSScriptRoot
$ThirdParty = if ($env:PVM_THIRDPARTY) { $env:PVM_THIRDPARTY } else { Join-Path $Root 'thirdparty' }
$Dest = Join-Path $ThirdParty 'libmpv-android'

$LibDir = Join-Path (Join-Path $Dest 'lib') 'arm64-v8a'
$Lib = Join-Path $LibDir 'libmpv.so'
$Inc = Join-Path (Join-Path $Dest 'include') 'mpv'
$Ca  = Join-Path $Dest 'cacert.pem'
if (-not $Force -and (Test-Path $Lib) -and (Test-Path (Join-Path $Inc 'client.h')) -and
    (Test-Path (Join-Path $Inc 'render.h')) -and (Test-Path (Join-Path $Inc 'render_gl.h')) -and (Test-Path $Ca)) {
    Write-Host "libmpv for Android is already in $Dest (use -Force to download again)"
    exit 0
}

New-Item -ItemType Directory -Force -Path $LibDir, $Inc | Out-Null
$Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("pvm-libmpv-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $Tmp | Out-Null
try {
    Write-Host "libmpv $MediaKitTag ($MediaKitJar)"
    $Jar = Join-Path $Tmp 'libmpv.zip'   # a .jar is a zip; Expand-Archive wants the extension
    Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/media-kit/libmpv-android-video-build/releases/download/$MediaKitTag/$MediaKitJar" -OutFile $Jar
    $Actual = (Get-FileHash -Algorithm SHA256 $Jar).Hash.ToLower()
    if ($Actual -ne $MediaKitSha256) { throw "checksum mismatch for ${MediaKitJar}: got $Actual" }
    Expand-Archive -Path $Jar -DestinationPath (Join-Path $Tmp 'jar') -Force
    Copy-Item (Join-Path (Join-Path (Join-Path (Join-Path $Tmp 'jar') 'lib') 'arm64-v8a') 'libmpv.so') $Lib -Force

    foreach ($h in 'client.h', 'render.h', 'render_gl.h') {
        Write-Host "mpv $MpvTag headers: $h"
        Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/mpv-player/mpv/$MpvTag/libmpv/$h" -OutFile (Join-Path $Inc $h)
    }
    Write-Host 'CA bundle'
    Invoke-WebRequest -UseBasicParsing -Uri 'https://curl.se/ca/cacert.pem' -OutFile $Ca
    Write-Host "done: $Dest"
} finally {
    Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
}
