@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Building TinyHand for Windows
echo ========================================

where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo Setting up MSVC environment...
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist dist mkdir dist

echo Compiling TinyHand.exe...
cl /nologo /O2 /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE ^
    windows\main.cpp ^
    windows\Common.cpp ^
    windows\Preferences.cpp ^
    windows\HandAsset.cpp ^
    windows\TapSound.cpp ^
    windows\Ink.cpp ^
    windows\ArmRenderer.cpp ^
    windows\Toolbar.cpp ^
    windows\CursorManager.cpp ^
    windows\InputHook.cpp ^
    windows\OverlayWindow.cpp ^
    windows\TrayIcon.cpp ^
    windows\SettingsWindow.cpp ^
    windows\RenderTest.cpp ^
    gdiplus.lib user32.lib gdi32.lib shell32.lib winmm.lib comctl32.lib ole32.lib shlwapi.lib advapi32.lib dwmapi.lib ^
    /Fo:dist\ ^
    /Fe:dist\TinyHand.exe ^
    /link /SUBSYSTEM:CONSOLE

set BUILD_STATUS=%errorlevel%
del dist\*.obj >nul 2>&1

if %BUILD_STATUS% neq 0 (
    echo.
    echo TinyHand.exe Build FAILED!
    exit /b 1
)

echo Compiling test_suite.exe...
cl /nologo /O2 /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE ^
    windows\test_suite.cpp ^
    windows\Common.cpp ^
    windows\Preferences.cpp ^
    windows\HandAsset.cpp ^
    windows\TapSound.cpp ^
    windows\Ink.cpp ^
    windows\ArmRenderer.cpp ^
    windows\Toolbar.cpp ^
    windows\CursorManager.cpp ^
    windows\InputHook.cpp ^
    windows\OverlayWindow.cpp ^
    windows\TrayIcon.cpp ^
    windows\SettingsWindow.cpp ^
    windows\RenderTest.cpp ^
    gdiplus.lib user32.lib gdi32.lib shell32.lib winmm.lib comctl32.lib ole32.lib shlwapi.lib advapi32.lib dwmapi.lib ^
    /Fo:dist\ ^
    /Fe:dist\test_suite.exe ^
    /link /SUBSYSTEM:CONSOLE

set TEST_BUILD_STATUS=%errorlevel%
del dist\*.obj >nul 2>&1

if %TEST_BUILD_STATUS% equ 0 (
    echo.
    echo ========================================
    echo Build SUCCESS: TinyHand.exe and test_suite.exe
    echo ========================================
    exit /b 0
) else (
    echo.
    echo test_suite.exe Build FAILED!
    exit /b 1
)
