# Windows counterpart of fetch_thirdparty.sh: restores the vendored sources under
# thirdparty\ from upstream -- Dear ImGui (only the files the build uses),
# nlohmann/json (single header) and the glad OpenGL 3.3 loader (generated with
# glad2, which needs Python). Only what is missing is fetched; -Force redoes it.
# Keep the pinned versions in sync with fetch_thirdparty.sh.
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_thirdparty.ps1 [-Force]
param([switch]$Force)
$ErrorActionPreference = 'Stop'

$ImguiTag   = 'v1.92.8'
$JsonVersion = 'v3.11.3'
$JsonSha256  = '9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6'

$Root = Split-Path -Parent $PSScriptRoot
$Tp = if ($env:PVM_THIRDPARTY) { $env:PVM_THIRDPARTY } else { Join-Path $Root 'thirdparty' }
$Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('pvm-thirdparty-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $Tmp | Out-Null

function Get-File([string]$url, [string]$out) {
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $out
}
# Path under $Tp from its parts (Join-Path, so the separator is the platform's).
function TP([string[]]$parts) {
    $p = $Tp
    foreach ($part in $parts) { $p = Join-Path $p $part }
    return $p
}

try {
    # ---- imgui
    if (-not $Force -and (Test-Path (TP 'imgui','imgui.cpp')) -and (Test-Path (TP 'imgui','backends','imgui_impl_glfw.cpp')) -and
        (Test-Path (TP 'imgui','backends','imgui_impl_opengl3.cpp')) -and (Test-Path (TP 'imgui','backends','imgui_impl_android.cpp'))) {
        Write-Host 'imgui: present'
    } else {
        Write-Host "imgui $ImguiTag"
        $archive = Join-Path $Tmp 'imgui.tar.gz'
        Get-File "https://github.com/ocornut/imgui/archive/refs/tags/$ImguiTag.tar.gz" $archive
        $src = Join-Path $Tmp 'imgui-src'
        New-Item -ItemType Directory -Force -Path $src | Out-Null
        & tar -xzf $archive -C $src --strip-components=1     # bsdtar ships with Windows 10 and later
        if ($LASTEXITCODE -ne 0) { throw 'could not extract the imgui archive' }
        New-Item -ItemType Directory -Force -Path (TP 'imgui','backends') | Out-Null
        foreach ($f in 'imgui.cpp', 'imgui.h', 'imgui_internal.h', 'imconfig.h', 'imgui_draw.cpp', 'imgui_tables.cpp',
                       'imgui_widgets.cpp', 'imgui_demo.cpp', 'imstb_rectpack.h', 'imstb_textedit.h', 'imstb_truetype.h', 'LICENSE.txt') {
            Copy-Item (Join-Path $src $f) (TP 'imgui',$f) -Force
        }
        foreach ($f in 'imgui_impl_glfw.cpp', 'imgui_impl_glfw.h', 'imgui_impl_opengl3.cpp', 'imgui_impl_opengl3.h',
                       'imgui_impl_opengl3_loader.h', 'imgui_impl_android.cpp', 'imgui_impl_android.h') {
            Copy-Item (Join-Path (Join-Path $src 'backends') $f) (TP 'imgui','backends',$f) -Force
        }
    }

    # ---- nlohmann/json
    if (-not $Force -and (Test-Path (TP 'json','nlohmann','json.hpp'))) {
        Write-Host 'json: present'
    } else {
        Write-Host "nlohmann/json $JsonVersion"
        $header = Join-Path $Tmp 'json.hpp'
        Get-File "https://github.com/nlohmann/json/releases/download/$JsonVersion/json.hpp" $header
        if ((Get-FileHash -Algorithm SHA256 $header).Hash.ToLower() -ne $JsonSha256) { throw 'checksum mismatch for json.hpp' }
        New-Item -ItemType Directory -Force -Path (TP 'json','nlohmann') | Out-Null
        Copy-Item $header (TP 'json','nlohmann','json.hpp') -Force
        Get-File "https://raw.githubusercontent.com/nlohmann/json/$JsonVersion/LICENSE.MIT" (TP 'json','LICENSE.txt')
    }

    # ---- glad
    if (-not $Force -and (Test-Path (TP 'glad','src','gl.c')) -and (Test-Path (TP 'glad','include','glad','gl.h')) -and
        (Test-Path (TP 'glad','include','KHR','khrplatform.h'))) {
        Write-Host 'glad: present'
    } else {
        $python = $null
        foreach ($candidate in 'python', 'python3', 'py') { if (Get-Command $candidate -ErrorAction SilentlyContinue) { $python = $candidate; break } }
        if (-not $python) { throw 'glad: Python is needed to generate it (winget install Python.Python.3.12), or restore thirdparty\glad from version control' }
        Write-Host 'glad (generating with glad2)'
        $venv = Join-Path $Tmp 'venv'
        & $python -m venv $venv
        if ($LASTEXITCODE -ne 0) { throw 'could not create a Python virtualenv' }
        # The virtualenv's scripts live in Scripts\ (*.exe) on Windows, bin/ elsewhere.
        $binDir = Join-Path $venv $(if (Test-Path (Join-Path $venv 'Scripts')) { 'Scripts' } else { 'bin' })
        $ext = if (Test-Path (Join-Path $venv 'Scripts')) { '.exe' } else { '' }
        & (Join-Path $binDir "pip$ext") install --quiet glad2
        if ($LASTEXITCODE -ne 0) { throw 'pip install glad2 failed' }
        $out = Join-Path $Tmp 'glad'
        & (Join-Path $binDir "glad$ext") --api 'gl:core=3.3' --extensions '' --out-path $out c
        if ($LASTEXITCODE -ne 0) { throw 'glad failed' }
        New-Item -ItemType Directory -Force -Path (TP 'glad') | Out-Null
        Copy-Item (Join-Path $out 'include') (TP 'glad','include') -Recurse -Force
        Copy-Item (Join-Path $out 'src') (TP 'glad','src') -Recurse -Force
        Set-Content -Path (TP 'glad','README.md') -Value @(
            '# glad (vendored)', '',
            'Pre-generated OpenGL 3.3 core loader (no extensions), generated with the',
            '`glad2` CLI (`glad --api gl:core=3.3 --extensions "" --out-path . c`).',
            'Regenerate with scripts/fetch_thirdparty.ps1 -Force if the required GL',
            'version or profile changes.')
    }
    Write-Host "thirdparty\ is complete: $Tp"
} finally {
    Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
}
