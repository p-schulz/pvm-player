@echo off
rem PVM Player - development environment setup for Windows.
rem All arguments are passed on to scripts\setup.ps1, which does the work:
rem
rem   scripts\setup.bat                     check, generate presets + VS Code files, configure
rem   scripts\setup.bat --install-deps      also install missing tools (winget, vcpkg)
rem   scripts\setup.bat --build             also build and run the tests
rem   scripts\setup.bat --android           also set up the Android build
rem   scripts\setup.bat --help              all options
setlocal
where powershell >nul 2>nul
if errorlevel 1 (
    echo PowerShell was not found. It ships with Windows 10 and later.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
exit /b %ERRORLEVEL%
