#include "TrayIcon.h"
#include <shellapi.h>

namespace TinyHand {

static const wchar_t* TRAY_CLASS = L"TinyHand_TrayMsgClass";
static const UINT WM_TRAYICON = WM_USER + 101;

enum TrayMenuIds {
    ID_TRAY_TOGGLE = 1001,
    ID_TRAY_CLEAR = 1002,
    ID_TRAY_SOUND = 1003,
    ID_TRAY_SETTINGS = 1004,
    ID_TRAY_EXIT = 1005
};

TrayIcon::TrayIcon() {
}

TrayIcon::~TrayIcon() {
    Destroy();
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TrayIcon* self = (TrayIcon*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HICON TrayIcon::CreateIconFromPng(const std::wstring& filename) {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring p = path;
    size_t lastSlash = p.find_last_of(L"\\/");
    std::wstring exeDir = (lastSlash != std::wstring::npos) ? p.substr(0, lastSlash) : L"";
    
    std::vector<std::wstring> candidates = {
        L"assets\\" + filename,
        exeDir + L"\\assets\\" + filename,
        exeDir + L"\\..\\assets\\" + filename,
        exeDir + L"\\" + filename,
        filename
    };
    
    for (const auto& c : candidates) {
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) {
            Gdiplus::Bitmap bmp(c.c_str());
            if (bmp.GetLastStatus() == Gdiplus::Ok) {
                HICON hIcon = NULL;
                bmp.GetHICON(&hIcon);
                if (hIcon) return hIcon;
            }
        }
    }
    
    // Fallback: system hand icon
    return LoadCursorW(NULL, IDC_HAND);
}

bool TrayIcon::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TRAY_CLASS;
    RegisterClassExW(&wc);
    
    m_hwnd = CreateWindowExW(0, TRAY_CLASS, L"TinyHand TrayMsg", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    
    m_hIconOff = CreateIconFromPng(L"menubar_off.png");
    m_hIconOn = CreateIconFromPng(L"menubar_on.png");
    
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = m_hIconOff;
    wcscpy_s(m_nid.szTip, L"TinyHand — click to turn on, right-click for the menu");
    
    Shell_NotifyIconW(NIM_ADD, &m_nid);
    return true;
}

void TrayIcon::Destroy() {
    if (m_hwnd) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
    if (m_hIconOn) { DestroyIcon(m_hIconOn); m_hIconOn = NULL; }
    if (m_hIconOff) { DestroyIcon(m_hIconOff); m_hIconOff = NULL; }
}

void TrayIcon::SetActive(bool active) {
    m_active = active;
    m_nid.hIcon = active ? m_hIconOn : m_hIconOff;
    wcscpy_s(m_nid.szTip, active ? L"TinyHand is on — click to turn off, right-click for the menu"
                                 : L"TinyHand — click to turn on, right-click for the menu");
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::SetSoundEnabled(bool enabled) {
    m_soundEnabled = enabled;
}

void TrayIcon::ShowContextMenu() {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE, m_active ? L"Turn Pointer Off\tCtrl+Alt+H" : L"Turn Pointer On\tCtrl+Alt+H");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_CLEAR, L"Clear Drawing\tCtrl+Alt+X");
    AppendMenuW(hMenu, MF_STRING | (m_soundEnabled ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_SOUND, L"Tap Sound");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Quit TinyHand");
    
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, m_hwnd, NULL);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

LRESULT TrayIcon::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAYICON) {
        if (lParam == WM_LBUTTONUP) {
            if (onTogglePointer) onTogglePointer();
            return 0;
        } else if (lParam == WM_RBUTTONUP) {
            ShowContextMenu();
            return 0;
        }
    } else if (msg == WM_COMMAND) {
        int id = LOWORD(wParam);
        switch (id) {
        case ID_TRAY_TOGGLE:
            if (onTogglePointer) onTogglePointer();
            break;
        case ID_TRAY_CLEAR:
            if (onClearDrawing) onClearDrawing();
            break;
        case ID_TRAY_SOUND:
            if (onToggleSound) onToggleSound();
            break;
        case ID_TRAY_SETTINGS:
            if (onOpenSettings) onOpenSettings();
            break;
        case ID_TRAY_EXIT:
            if (onExit) onExit();
            break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace TinyHand
