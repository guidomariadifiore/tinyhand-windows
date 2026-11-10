#include "Common.h"
#include "Preferences.h"
#include "HandAsset.h"
#include "TapSound.h"
#include "OverlayWindow.h"
#include "TrayIcon.h"
#include "SettingsWindow.h"
#include "RenderTest.h"
#include "CursorManager.h"
#include "InputHook.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comctl32.lib")

using namespace TinyHand;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    setvbuf(stdout, NULL, _IONBF, 0);

    // 0. Set DPI Awareness (Per-Monitor V2) so virtual metrics match physical pixels
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        auto pSetContext = (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetContext) {
            pSetContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            typedef BOOL(WINAPI* SetDpiAwareProc)();
            auto pSetAware = (SetDpiAwareProc)GetProcAddress(hUser32, "SetProcessDPIAware");
            if (pSetAware) pSetAware();
        }
    }

    // 1. Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::Status gdiStatus = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    if (gdiStatus != Gdiplus::Ok) {
        MessageBoxW(NULL, L"Failed to initialize GDI+", L"TinyHand Error", MB_ICONERROR);
        return 1;
    }

    // 2. Parse command-line
    bool hasCliArg = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--render-test") == 0) {
                hasCliArg = true;
                std::wstring outPath = (i + 1 < argc) ? argv[i + 1] : L"armtest.png";
                bool ok = RenderTest::Run(outPath);
                wprintf(L"Render test %s: %s\n", ok ? L"succeeded" : L"failed", outPath.c_str());
                LocalFree(argv);
                HandAssetManager::Instance().ClearCache();
                Gdiplus::GdiplusShutdown(gdiplusToken);
                exit(ok ? 0 : 1);
            }
        }
        LocalFree(argv);
    }

    if (!hasCliArg) {
        // Detach console in normal tray mode
        FreeConsole();
    }

    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"TinyHand_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"TinyHand is already running. Check the system tray.", L"TinyHand", MB_ICONINFORMATION);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 0;
    }

    // 3. Load preferences
    Settings settings = Preferences::Load();

    // 4. Create App Components
    OverlayWindow overlay;
    TrayIcon tray;
    SettingsWindow settingsWin;

    overlay.ApplySettings(settings);
    overlay.Create(hInstance);
    tray.Create(hInstance);
    settingsWin.Create(hInstance);

    tray.SetSoundEnabled(settings.currentStyle == PointerStyleId::Laser ? settings.laserSound : settings.soundEnabled);

    // 5. Connect Wire-ups
    tray.onTogglePointer = [&overlay]() {
        overlay.Toggle();
    };
    tray.onClearDrawing = [&overlay]() {
        overlay.ClearInk();
    };
    tray.onToggleSound = [&overlay, &tray]() {
        Settings s = overlay.GetSettings();
        if (s.currentStyle == PointerStyleId::Laser) {
            s.laserSound = !s.laserSound;
            tray.SetSoundEnabled(s.laserSound);
        } else {
            s.soundEnabled = !s.soundEnabled;
            tray.SetSoundEnabled(s.soundEnabled);
        }
        overlay.ApplySettings(s);
        Preferences::Save(s);
    };
    tray.onOpenSettings = [&settingsWin, &overlay]() {
        settingsWin.Show(overlay.GetSettings());
    };
    tray.onExit = []() {
        PostQuitMessage(0);
    };

    overlay.onActiveChanged = [&tray](bool active) {
        tray.SetActive(active);
    };
    overlay.onOpenSettings = [&settingsWin, &overlay]() {
        settingsWin.Show(overlay.GetSettings());
    };

    settingsWin.onSettingsSaved = [&overlay, &tray](const Settings& newSettings) {
        overlay.ApplySettings(newSettings);
        tray.SetSoundEnabled(newSettings.currentStyle == PointerStyleId::Laser ? newSettings.laserSound : newSettings.soundEnabled);
        Preferences::Save(newSettings);
    };

    // 6. Register Global Hotkeys: Ctrl+Alt+H (Toggle) and Ctrl+Alt+X (Clear)
    RegisterHotKey(NULL, 1, MOD_CONTROL | MOD_ALT, 'H');
    RegisterHotKey(NULL, 2, MOD_CONTROL | MOD_ALT, 'X');

    // Preload hand assets in background
    CreateThread(NULL, 0, [](LPVOID) -> DWORD {
        HandAssetManager::Instance().PreloadAll();
        return 0;
    }, NULL, 0, NULL);

    // 7. Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == 1) {
                overlay.Toggle();
            } else if (msg.wParam == 2) {
                overlay.ClearInk();
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 8. Cleanup
    UnregisterHotKey(NULL, 1);
    UnregisterHotKey(NULL, 2);

    overlay.Destroy();
    tray.Destroy();
    settingsWin.Hide();
    CursorManager::Instance().Restore();
    InputHook::Instance().Uninstall();
    SetThreadExecutionState(ES_CONTINUOUS);

    if (hMutex) {
        CloseHandle(hMutex);
    }

    HandAssetManager::Instance().ClearCache();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}

int wmain(int argc, wchar_t* argv[]) {
    return wWinMain(GetModuleHandle(NULL), NULL, GetCommandLineW(), SW_SHOWDEFAULT);
}
