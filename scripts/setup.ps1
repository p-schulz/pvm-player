# PVM Player -- development environment setup for Windows.
# Run it through scripts\setup.bat (or: powershell -ExecutionPolicy Bypass -File scripts\setup.ps1).
# Same options as scripts/setup.sh, in either style (--install-deps or -InstallDeps):
#
#   --install-deps      install what is missing (winget, vcpkg); asks first
#   -y, --yes           don't ask before installing
#   --build [debug|release]
#                       also build (default: debug) and run the tests
#   --android           also set up the Android build (JDK check, SDK/NDK, libmpv)
#   --no-vscode         don't write .vscode\
#   --force             overwrite existing .vscode\ files and hand-edited presets
#   --check             only report what is present and missing; change nothing
#   --dry-run           print the install commands instead of running them
#   -h, --help
#
# What it sets up: Visual Studio (C++ workload) as the compiler, vcpkg's curl,
# the libmpv SDK (thirdparty\libmpv, plus an MSVC import library), the vendored
# sources under thirdparty\, CMakeUserPresets.json ("local-debug" /
# "local-release") and the .vscode\ files, then configures the project.
#
# NOTE: written without a Windows machine at hand; it has not been run.
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$MinCMake = [version]'3.21'
$AndroidNdk = '27.0.12077973'
$AndroidCMake = '3.22.1'
$AndroidPlatform = 'android-36'
$AndroidBuildTools = '36.0.0'
$AndroidCmdlineToolsZip = 'commandlinetools-win-11076708_latest.zip'

# ------------------------------------------------------------------ options --
$InstallDeps = $false; $Yes = $false; $DoBuild = $false; $BuildConfig = 'debug'
$Android = $false; $VSCode = $true; $Force = $false; $Check = $false; $DryRun = $false
for ($i = 0; $i -lt $args.Count; $i++) {
    $a = ([string]$args[$i]).ToLower()
    if ($a -eq '--install-deps' -or $a -eq '-installdeps') { $InstallDeps = $true }
    elseif ($a -eq '-y' -or $a -eq '--yes' -or $a -eq '-yes') { $Yes = $true }
    elseif ($a -eq '--build' -or $a -eq '-build') {
        $DoBuild = $true
        if (($i + 1) -lt $args.Count -and ([string]$args[$i + 1]).ToLower() -match '^(debug|release)$') {
            $BuildConfig = ([string]$args[$i + 1]).ToLower(); $i++
        }
    }
    elseif ($a -eq '--android' -or $a -eq '-android') { $Android = $true }
    elseif ($a -eq '--no-vscode' -or $a -eq '-novscode') { $VSCode = $false }
    elseif ($a -eq '--force' -or $a -eq '-force') { $Force = $true }
    elseif ($a -eq '--check' -or $a -eq '-check') { $Check = $true }
    elseif ($a -eq '--dry-run' -or $a -eq '-dryrun') { $DryRun = $true }
    elseif ($a -eq '-h' -or $a -eq '--help' -or $a -eq '-help') {
        Get-Content $PSCommandPath | Select-Object -First 18 | ForEach-Object { $_ -replace '^# ?', '' }
        exit 0
    }
    else { Write-Host "unknown option: $($args[$i]) (see --help)"; exit 2 }
}

# ------------------------------------------------------------------- output --
$script:MissingRequired = @()
function Write-Step([string]$m) { Write-Host ''; Write-Host "== $m" -ForegroundColor Cyan }
function Write-Ok([string]$m) { Write-Host "  [ ok ] $m" -ForegroundColor Green }
function Write-Warn([string]$m) { Write-Host "  [warn] $m" -ForegroundColor Yellow }
function Write-Need([string]$m) { $script:MissingRequired += $m; Write-Host "  [miss] $m" -ForegroundColor Red }
function Test-Command([string]$name) { return [bool](Get-Command $name -ErrorAction SilentlyContinue) }

# Runs a native command (or just prints it with --dry-run); throws if it fails.
function Invoke-Native([string]$file, [string[]]$arguments) {
    if ($DryRun) { Write-Host "  > $file $($arguments -join ' ')"; return }
    & $file @arguments
    if ($LASTEXITCODE -ne 0) { throw "$file failed (exit code $LASTEXITCODE)" }
}

# Output of a native command that may write to stderr (java -version does).
function Get-NativeOutput([string]$commandLine) {
    return (cmd /c "$commandLine 2>&1" | Out-String)
}

function Confirm-Action([string]$question) {
    if ($Yes) { return $true }
    if (-not [Environment]::UserInteractive) { return $false }
    $reply = Read-Host "$question [y/N]"
    return ($reply -eq 'y' -or $reply -eq 'Y')
}

function Write-Utf8NoBom([string]$path, [string]$text) {
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

Write-Host "PVM Player setup  (Windows, repository: $Root)" -ForegroundColor White

# ------------------------------------------------------------------ 1. tools --
Write-Step 'Build tools'

if (Test-Command git) { Write-Ok 'git' } else { Write-Need 'git (winget install Git.Git)' }

if (Test-Command cmake) {
    $cmakeText = (cmake --version | Select-Object -First 1)
    if ($cmakeText -match '(\d+\.\d+(\.\d+)?)') {
        $cmakeVersion = [version]$Matches[1]
        if ($cmakeVersion -ge $MinCMake) { Write-Ok "cmake $cmakeVersion" }
        else { Write-Need "cmake >= $MinCMake (found $cmakeVersion; winget install Kitware.CMake)" }
    }
} else {
    Write-Need "cmake >= $MinCMake (winget install Kitware.CMake)"
}

# Visual Studio with the C++ workload, found through vswhere.
function Find-VisualStudio {
    if (-not ${env:ProgramFiles(x86)}) { return $null }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $found = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    if (-not $found) { return $null }
    return ($found | Select-Object -First 1)
}
$vs = Find-VisualStudio
$Generator = $null
if ($vs) {
    $major = [int]($vs.installationVersion.Split('.')[0])
    if ($major -eq 18) { $Generator = 'Visual Studio 18 2026' }
    elseif ($major -eq 17) { $Generator = 'Visual Studio 17 2022' }
    elseif ($major -eq 16) { $Generator = 'Visual Studio 16 2019' }
    if ($Generator) { Write-Ok "$($vs.displayName) (CMake generator: $Generator)" }
    else { Write-Need "a supported Visual Studio (found version $($vs.installationVersion); need 2019, 2022 or 2026)" }
} else {
    Write-Need 'Visual Studio or Build Tools with the "Desktop development with C++" workload'
}

# ---------------------------------------------------------------- libraries --
Write-Step 'Libraries'

$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $env:USERPROFILE 'vcpkg' }
$VcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
$VcpkgToolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
$Triplet = 'x64-windows'
$vcpkgHasCurl = $false
if (Test-Path $VcpkgExe) {
    Write-Ok "vcpkg: $VcpkgRoot"
    $listed = (& $VcpkgExe list "curl:$Triplet" | Out-String)
    if ($listed -match "curl:$Triplet") { $vcpkgHasCurl = $true; Write-Ok "libcurl ($Triplet, via vcpkg)" }
    else { Write-Need "libcurl for vcpkg ($Triplet) -- vcpkg install curl:$Triplet" }
} else {
    Write-Need "vcpkg (for libcurl); --install-deps clones it to $VcpkgRoot"
}

$MpvDir = Join-Path $Root 'thirdparty\libmpv'
$mpvHeader = Join-Path $MpvDir 'include\mpv\client.h'
$mpvImportLib = (Test-Path (Join-Path $MpvDir 'mpv.lib')) -or (Test-Path (Join-Path $MpvDir 'libmpv.dll.a'))
if ((Test-Path $mpvHeader) -and $mpvImportLib) { Write-Ok 'libmpv SDK (thirdparty\libmpv)' }
else { Write-Warn 'libmpv SDK not in thirdparty\libmpv yet; it is downloaded below' }

# ---------------------------------------------------------- install missing --
if ($script:MissingRequired.Count -gt 0) {
    if ($Check) {
        # report only
    } elseif ($InstallDeps) {
        Write-Step 'Installing'
        if (-not $DryRun -and -not (Confirm-Action 'Install the missing tools listed above?')) { throw 'cancelled' }
        $haveWinget = Test-Command winget
        if (-not (Test-Command git)) {
            if ($haveWinget) { Invoke-Native 'winget' @('install', '--id', 'Git.Git', '-e', '--accept-package-agreements', '--accept-source-agreements') }
            else { Write-Warn 'install Git from https://git-scm.com/download/win' }
        }
        if (-not (Test-Command cmake)) {
            if ($haveWinget) { Invoke-Native 'winget' @('install', '--id', 'Kitware.CMake', '-e', '--accept-package-agreements', '--accept-source-agreements') }
            else { Write-Warn 'install CMake from https://cmake.org/download/' }
        }
        if (-not $vs) {
            if ($haveWinget) {
                Invoke-Native 'winget' @('install', '--id', 'Microsoft.VisualStudio.2022.BuildTools', '-e', '--accept-package-agreements',
                    '--accept-source-agreements', '--override',
                    '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
            } else { Write-Warn 'install "Build Tools for Visual Studio" with the C++ workload from https://visualstudio.microsoft.com/downloads/' }
        }
        if (-not (Test-Path $VcpkgExe)) {
            if (Test-Command git) {
                Invoke-Native 'git' @('clone', 'https://github.com/microsoft/vcpkg', $VcpkgRoot)
                Invoke-Native (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') @('-disableMetrics')
            } else { Write-Warn 'vcpkg needs git; install git and run this again' }
        }
        if ((Test-Path $VcpkgExe) -and -not $vcpkgHasCurl) {
            Invoke-Native $VcpkgExe @('install', "curl:$Triplet")
        }
        if (-not $DryRun) {
            Write-Host ''
            Write-Host 'Tools installed. Open a NEW terminal (so PATH is refreshed) and run scripts\setup.bat again.'
            exit 0
        }
    } else {
        Write-Host ''
        Write-Host 'Required items are missing. Re-run with --install-deps to install them,'
        Write-Host 'or install them yourself.'
        exit 1
    }
}
if ($Check -and $script:MissingRequired.Count -gt 0) { Write-Host ''; Write-Host '(--check: nothing changed)'; exit 1 }

# -------------------------------------------------------------- 2. thirdparty --
Write-Step 'Vendored sources (thirdparty\)'
$needThirdparty = -not ((Test-Path 'thirdparty\glad\src\gl.c') -and (Test-Path 'thirdparty\imgui\imgui.cpp') -and
                        (Test-Path 'thirdparty\imgui\backends\imgui_impl_glfw.cpp'))
if (-not $needThirdparty) {
    Write-Ok 'glad, Dear ImGui'
    if (Test-Path 'thirdparty\json\nlohmann\json.hpp') { Write-Ok 'nlohmann/json' }
    else { Write-Warn 'nlohmann/json not vendored; CMake fetches it' }
} elseif ($Check) {
    Write-Need 'thirdparty\ is incomplete (setup without --check restores it)'
} elseif ($DryRun) {
    Write-Host '  > scripts\fetch_thirdparty.ps1'
} else {
    Write-Host '  restoring from upstream (scripts\fetch_thirdparty.ps1) ...'
    & (Join-Path $PSScriptRoot 'fetch_thirdparty.ps1')
}

# -------------------------------------------------------------- libmpv SDK ---
# Downloads the newest dev package from shinchiro's mpv builds (a .7z) and
# turns its mpv.def into an MSVC import library.
function Install-LibmpvSdk {
    if (-not (Test-Path $MpvDir)) { New-Item -ItemType Directory -Force -Path $MpvDir | Out-Null }
    if (-not (Test-Path $mpvHeader) -or -not ((Test-Path (Join-Path $MpvDir 'libmpv.dll.a')) -or (Test-Path (Join-Path $MpvDir 'mpv.lib')))) {
        Write-Host '  downloading the libmpv SDK (shinchiro/mpv-winbuild-cmake) ...'
        $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest' -Headers @{ 'User-Agent' = 'pvm-player-setup' }
        $asset = $release.assets | Where-Object { $_.name -match '^mpv-dev-x86_64-\d{8}.*\.7z$' } | Select-Object -First 1
        if (-not $asset) { throw 'no mpv-dev-x86_64 package found in the latest release' }
        $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('pvm-libmpv-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $tmp | Out-Null
        try {
            $archive = Join-Path $tmp $asset.name
            Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $archive
            $sevenZip = $null
            foreach ($candidate in @('7z', '7za')) { if (Test-Command $candidate) { $sevenZip = $candidate; break } }
            if (-not $sevenZip -and (Test-Path "$env:ProgramFiles\7-Zip\7z.exe")) { $sevenZip = "$env:ProgramFiles\7-Zip\7z.exe" }
            $out = Join-Path $tmp 'sdk'
            New-Item -ItemType Directory -Force -Path $out | Out-Null
            if ($sevenZip) { & $sevenZip x $archive "-o$out" -y | Out-Null }
            else { & tar -xf $archive -C $out }   # Windows' bundled bsdtar reads .7z as well
            if ($LASTEXITCODE -ne 0) { throw 'could not extract the .7z (install 7-Zip: winget install 7zip.7zip)' }
            Copy-Item -Path (Join-Path $out '*') -Destination $MpvDir -Recurse -Force
        } finally {
            Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        }
    }
    # An MSVC-native import library from the SDK's .def file.
    $def = Join-Path $MpvDir 'mpv.def'
    $dll = Get-ChildItem -Path $MpvDir -Filter '*mpv*.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ((Test-Path $def) -and $dll -and -not (Test-Path (Join-Path $MpvDir 'mpv.lib')) -and $vs) {
        $libExe = Get-ChildItem -Path (Join-Path $vs.installationPath 'VC\Tools\MSVC\*\bin\Hostx64\x64\lib.exe') -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($libExe) {
            & $libExe.FullName "/def:$def" "/name:$($dll.Name)" "/out:$(Join-Path $MpvDir 'mpv.lib')" /machine:x64 | Out-Null
            Write-Ok "generated mpv.lib for $($dll.Name)"
        } else {
            Write-Warn 'lib.exe not found; linking will use libmpv.dll.a'
        }
    }
}
if (-not $Check) {
    Write-Step 'libmpv SDK'
    if ($DryRun) { Write-Host '  > download the libmpv SDK into thirdparty\libmpv' }
    else {
        Install-LibmpvSdk
        if ((Test-Path $mpvHeader)) { Write-Ok "libmpv SDK in $MpvDir" } else { Write-Need 'libmpv SDK (download failed)' }
    }
}
if ($Check) { Write-Host ''; Write-Host '(--check: nothing changed)'; exit ($(if ($script:MissingRequired.Count -eq 0) { 0 } else { 1 })) }

# --------------------------------------------------------------- 3. presets --
Write-Step 'CMake presets'
$presetsPath = Join-Path $Root 'CMakeUserPresets.json'
$writePresets = $true
if ((Test-Path $presetsPath) -and -not $Force -and -not ((Get-Content $presetsPath -Raw) -match '"pvm-player/setup"')) {
    Write-Warn 'kept your own CMakeUserPresets.json (no generated marker; --force replaces it)'
    $writePresets = $false
}
if ($writePresets) {
    $common = [ordered]@{}
    if ($Generator) {
        $common['generator'] = $Generator
        $common['architecture'] = [ordered]@{ value = 'x64'; strategy = 'set' }
    }
    if (Test-Path $VcpkgToolchain) {
        $common['toolchainFile'] = $VcpkgToolchain.Replace('\', '/')
    }
    $cache = [ordered]@{}
    if (Test-Path $VcpkgToolchain) { $cache['VCPKG_TARGET_TRIPLET'] = $Triplet }
    function New-ConfigurePreset([string]$name, [string]$display, [string]$inherits) {
        $preset = [ordered]@{ name = $name; displayName = $display; inherits = $inherits }
        foreach ($k in $common.Keys) { $preset[$k] = $common[$k] }
        if ($cache.Count -gt 0) { $preset['cacheVariables'] = $cache }
        return $preset
    }
    $user = [ordered]@{
        version = 3
        cmakeMinimumRequired = [ordered]@{ major = 3; minor = 21; patch = 0 }
        vendor = [ordered]@{ 'pvm-player/setup' = [ordered]@{ generatedBy = 'scripts/setup.ps1'; note = 'regenerated by the setup script; it will not overwrite a file without this marker' } }
        configurePresets = @(
            (New-ConfigurePreset 'local-debug' 'Debug (this machine)' 'debug'),
            (New-ConfigurePreset 'local-release' 'Release (this machine)' 'release')
        )
        buildPresets = @(
            [ordered]@{ name = 'local-debug'; configurePreset = 'local-debug'; configuration = 'Debug' },
            [ordered]@{ name = 'local-release'; configurePreset = 'local-release'; configuration = 'Release' }
        )
        testPresets = @(
            [ordered]@{ name = 'local-debug'; configurePreset = 'local-debug'; configuration = 'Debug'; output = [ordered]@{ outputOnFailure = $true } },
            [ordered]@{ name = 'local-release'; configurePreset = 'local-release'; configuration = 'Release'; output = [ordered]@{ outputOnFailure = $true } }
        )
    }
    Write-Utf8NoBom $presetsPath (($user | ConvertTo-Json -Depth 10) + "`n")
    Write-Ok "wrote CMakeUserPresets.json ($Generator, presets local-debug / local-release)"
}

# --------------------------------------------------------------- 4. VS Code --
Write-Step 'VS Code'
if (-not $VSCode) {
    Write-Host '  skipped (--no-vscode)'
} else {
    New-Item -ItemType Directory -Force -Path '.vscode' | Out-Null
    foreach ($f in @('tasks.json', 'launch.json', 'settings.json', 'extensions.json')) {
        $src = Join-Path $Root "scripts\templates\vscode\$f"
        $dst = Join-Path $Root ".vscode\$f"
        if (-not (Test-Path $dst) -or $Force) { Copy-Item $src $dst -Force; Write-Ok "wrote .vscode\$f" }
        elseif ((Get-FileHash $src).Hash -eq (Get-FileHash $dst).Hash) { Write-Ok ".vscode\$f is up to date" }
        else { Write-Warn "kept your .vscode\$f (differs from the template; --force replaces it)" }
    }
}

# --------------------------------------------------------------- 5. configure --
Write-Step 'Configure'
Invoke-Native 'cmake' @('--preset', 'local-debug')
if ($BuildConfig -eq 'release') { Invoke-Native 'cmake' @('--preset', 'local-release') }

# ---------------------------------------------------------------- 6. Android --
if ($Android) {
    Write-Step 'Android'
    $javaOut = Get-NativeOutput 'java -version'
    if ($javaOut -match '"(1[7-9]|[2-9]\d)[.\"]') { Write-Ok "JDK: $(($javaOut -split "`n")[0].Trim())" }
    else {
        Write-Need 'JDK 17 or newer (winget install EclipseAdoptium.Temurin.17.JDK)'
        if ($InstallDeps -and (Test-Command winget)) { Invoke-Native 'winget' @('install', '--id', 'EclipseAdoptium.Temurin.17.JDK', '-e') }
    }

    $sdk = $env:ANDROID_HOME
    if (-not $sdk) { $sdk = $env:ANDROID_SDK_ROOT }
    if (-not $sdk) { $sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
    $sdkmanager = Join-Path $sdk 'cmdline-tools\latest\bin\sdkmanager.bat'
    if (-not (Test-Path $sdkmanager)) {
        if ($InstallDeps -and (Confirm-Action "Download the Android command-line tools into $sdk?")) {
            if ($DryRun) { Write-Host "  > download $AndroidCmdlineToolsZip and unpack to $sdk\cmdline-tools\latest" }
            else {
                $zip = Join-Path ([System.IO.Path]::GetTempPath()) $AndroidCmdlineToolsZip
                Invoke-WebRequest -UseBasicParsing -Uri "https://dl.google.com/android/repository/$AndroidCmdlineToolsZip" -OutFile $zip
                $unpack = Join-Path ([System.IO.Path]::GetTempPath()) ('pvm-cmdline-' + [guid]::NewGuid().ToString('N'))
                Expand-Archive -Path $zip -DestinationPath $unpack -Force
                New-Item -ItemType Directory -Force -Path (Join-Path $sdk 'cmdline-tools') | Out-Null
                if (Test-Path (Join-Path $sdk 'cmdline-tools\latest')) { Remove-Item -Recurse -Force (Join-Path $sdk 'cmdline-tools\latest') }
                Move-Item (Join-Path $unpack 'cmdline-tools') (Join-Path $sdk 'cmdline-tools\latest')
            }
        } else {
            Write-Need "Android SDK command-line tools in $sdk (or set ANDROID_HOME; --install-deps downloads them)"
        }
    } else {
        Write-Ok "Android SDK: $sdk"
    }

    $missingPackages = @()
    if (-not ((Test-Path (Join-Path $sdk "platforms\$AndroidPlatform")) -or (Test-Path (Join-Path $sdk "platforms\$AndroidPlatform.0")))) { $missingPackages += "platforms;$AndroidPlatform" }
    if (-not (Test-Path (Join-Path $sdk "build-tools\$AndroidBuildTools"))) { $missingPackages += "build-tools;$AndroidBuildTools" }
    if (-not (Test-Path (Join-Path $sdk "ndk\$AndroidNdk"))) { $missingPackages += "ndk;$AndroidNdk" }
    if (-not (Test-Path (Join-Path $sdk "cmake\$AndroidCMake"))) { $missingPackages += "cmake;$AndroidCMake" }
    if (-not (Test-Path (Join-Path $sdk 'platform-tools'))) { $missingPackages += 'platform-tools' }
    if ($missingPackages.Count -eq 0) {
        Write-Ok "SDK packages: platform $AndroidPlatform, build-tools $AndroidBuildTools, NDK $AndroidNdk, CMake $AndroidCMake, platform-tools"
    } elseif ((Test-Path $sdkmanager) -or $DryRun) {
        if ($InstallDeps) {
            Write-Host "  installing: $($missingPackages -join ' ')"
            if ($DryRun) { Write-Host "  > sdkmanager --licenses; sdkmanager $($missingPackages -join ' ')" }
            else {
                1..50 | ForEach-Object { 'y' } | & $sdkmanager --licenses | Out-Null
                & $sdkmanager @missingPackages
            }
        } else {
            foreach ($p in $missingPackages) { Write-Need "Android SDK package $p (--install-deps installs it)" }
        }
    }

    if ((Test-Path $sdk) -and -not $DryRun) {
        # Gradle reads the SDK location from local.properties (git-ignored); forward slashes avoid escaping.
        Write-Utf8NoBom (Join-Path $Root 'android\local.properties') ("sdk.dir=" + $sdk.Replace('\', '/') + "`n")
        Write-Ok "wrote android\local.properties (sdk.dir=$sdk)"
    }
    if ($DryRun) { Write-Host '  > android\fetch_libmpv.ps1' }
    else { & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'android\fetch_libmpv.ps1') }

    if ($DoBuild -and -not $DryRun -and $script:MissingRequired.Count -eq 0) {
        $task = if ($BuildConfig -eq 'release') { ':app:assembleRelease' } else { ':app:assembleDebug' }
        Push-Location (Join-Path $Root 'android')
        try { & .\gradlew.bat --console=plain $task; if ($LASTEXITCODE -ne 0) { throw 'Gradle build failed' } }
        finally { Pop-Location }
    }
}

# ------------------------------------------------------------------ 7. build --
if ($DoBuild) {
    Write-Step "Build ($BuildConfig)"
    Invoke-Native 'cmake' @('--build', '--preset', "local-$BuildConfig", '--parallel')
    Write-Step 'Tests'
    Invoke-Native 'ctest' @('--preset', "local-$BuildConfig")
}

# ------------------------------------------------------------------ summary --
Write-Step 'Done'
if ($script:MissingRequired.Count -gt 0) { Write-Warn "still missing: $($script:MissingRequired -join ', ')" }
Write-Host "  Build (CLI):   cmake --build --preset local-$BuildConfig"
Write-Host "  Run:           build\local-$BuildConfig\pvm_player.exe [media folder ...]"
Write-Host "  Test:          ctest --preset local-$BuildConfig"
Write-Host '  VS Code:       open this folder and install the recommended extensions, then'
Write-Host '                 Ctrl+Shift+B builds, F5 debugs; Terminal > Run Task lists the rest'
if ($Android) {
    Write-Host '  Android:       cd android; .\gradlew.bat :app:assembleRelease'
    Write-Host '                 adb install -r app\build\outputs\apk\release\app-release.apk'
}
