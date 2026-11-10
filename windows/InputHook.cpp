#include "InputHook.h"

namespace TinyHand {

InputHook& InputHook::Instance() {
    static InputHook instance;
    return instance;
}

InputHook::InputHook() {
}

InputHook::~InputHook() {
    Uninstall();
}

bool InputHook::Install() {
    if (!m_hMouseHook) {
        m_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(NULL), 0);
    }
    if (!m_hKeyboardHook) {
        m_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    }
    return (m_hMouseHook != NULL && m_hKeyboardHook != NULL);
}

void InputHook::Uninstall() {
    if (m_hMouseHook) {
        UnhookWindowsHookEx(m_hMouseHook);
        m_hMouseHook = NULL;
    }
    if (m_hKeyboardHook) {
        UnhookWindowsHookEx(m_hKeyboardHook);
        m_hKeyboardHook = NULL;
    }
    m_active = false;
    m_isInterceptingDown = false;
}

void InputHook::SetActive(bool active) {
    m_active = active;
    m_isInterceptingDown = false;
    m_interactHeld = false;
}

void InputHook::SetPassThroughRects(const std::vector<RECT>& rects) {
    m_passThroughRects = rects;
}

LRESULT CALLBACK InputHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        return InputHook::Instance().HandleMouse(nCode, wParam, lParam);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK InputHook::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        return InputHook::Instance().HandleKeyboard(nCode, wParam, lParam);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

bool InputHook::IsOverInteractiveSystemUI(POINT pt) const {
    std::lock_guard<std::mutex> lock(m_sysUIMutex);
    ULONGLONG now = GetTickCount64();
    if (std::abs(pt.x - m_cachedSysUIPt.x) < 8 && std::abs(pt.y - m_cachedSysUIPt.y) < 8 && (now - m_cachedSysUITime) < 40) {
        return m_cachedSysUIResult;
    }
    
    m_cachedSysUIPt = pt;
    m_cachedSysUITime = now;
    
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd) {
        m_cachedSysUIResult = false;
        return false;
    }
    
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root) root = hwnd;
    
    wchar_t rootCls[64] = { 0 };
    GetClassNameW(root, rootCls, 64);
    
    // Check if window belongs to TinyHand (SettingsWindow or Toolbar)
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) {
        if (wcscmp(rootCls, L"TinyHand_SettingsClass") == 0 ||
            wcscmp(rootCls, L"TinyHand_ToolbarClass") == 0) {
            m_cachedSysUIResult = true;
            return true;
        }
        m_cachedSysUIResult = false;
        return false;
    }
    
    // Check if root or window is Taskbar, Notification Area, or Standard Menu
    if (wcscmp(rootCls, L"Shell_TrayWnd") == 0 ||
        wcscmp(rootCls, L"Shell_SecondaryTrayWnd") == 0 ||
        wcscmp(rootCls, L"NotifyIconOverflowWindow") == 0 ||
        wcscmp(rootCls, L"#32768") == 0) {
        m_cachedSysUIResult = true;
        return true;
    }
    
    // Also check hwnd itself
    wchar_t cls[64] = { 0 };
    GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, L"#32768") == 0 ||
        wcscmp(cls, L"TrayNotifyWnd") == 0) {
        m_cachedSysUIResult = true;
        return true;
    }
    
    m_cachedSysUIResult = false;
    return false;
}

LRESULT InputHook::HandleMouse(int nCode, WPARAM wParam, LPARAM lParam) {
    if (!m_active) {
        return CallNextHookEx(m_hMouseHook, nCode, wParam, lParam);
    }
    
    MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
    
    // Always notify mouse move and return immediately to keep hook processing ultra-fast (<0.01ms)
    if (wParam == WM_MOUSEMOVE) {
        if (onMouseMove) onMouseMove(ms->pt.x, ms->pt.y);
        return CallNextHookEx(m_hMouseHook, nCode, wParam, lParam);
    }
    
    // Check Interact Key (Alt)
    bool interactDown = (m_interactVk != 0) && ((GetAsyncKeyState(m_interactVk) & 0x8000) != 0);
    if (interactDown != m_interactHeld) {
        m_interactHeld = interactDown;
        if (onInteractStateChanged) onInteractStateChanged(interactDown);
    }
    
    if (interactDown) {
        // Pass clicks through when interact key is held!
        return CallNextHookEx(m_hMouseHook, nCode, wParam, lParam);
    }
    
    // Check if in pass-through rect (e.g. Toolbar interactive buttons)
    for (const auto& r : m_passThroughRects) {
        if (PtInRect(&r, ms->pt)) {
            return CallNextHookEx(m_hMouseHook, nCode, wParam, lParam);
        }
    }
    
    // Check interactive system UI (Taskbar, Tray icon, menus, TinyHand settings)
    if (IsOverInteractiveSystemUI(ms->pt)) {
        return CallNextHookEx(m_hMouseHook, nCode, wParam, lParam);
    }
    
    // TinyHand click interception: Consume Left, Right, Middle clicks so underlying video/app is NEVER clicked or stopped
    if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN) {
        m_isInterceptingDown = true;
        if (onMouseDown) onMouseDown(ms->pt.x, ms->pt.y);
        // CONSUME CLICK: Window underneath (e.g. video) does NOT get clicked!
        return 1;
    }
    
    if (wParam == WM_LBUTTONUP || wParam == WM_RBUTTONUP || wParam == WM_MBUTTONUP) {
        if (m_isInterceptingDown) {
            m_isInterceptingDown = false;
            if (onMouseUp) onMouseUp(ms->pt.x, ms->pt.y);
            return 1;
        }
    }
    
    return CallNextHookEx(m_hMouseHook, nCode, wParam, lParam);
}

LRESULT InputHook::HandleKeyboard(int nCode, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
    
    if (m_active) {
        // Escape key to dismiss TinyHand
        if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && kb->vkCode == VK_ESCAPE) {
            if (onEscapePressed) onEscapePressed();
            return 1; // Consume Esc
        }
        
        // Track Alt/Interact key
        if (m_interactVk != 0 && (kb->vkCode == VK_MENU || kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU)) {
            bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            if (down != m_interactHeld) {
                m_interactHeld = down;
                if (!down) {
                    // Suppress menu bar activation on Alt release by injecting a null key
                    keybd_event(0xFF, 0, 0, 0);
                    keybd_event(0xFF, 0, KEYEVENTF_KEYUP, 0);
                }
                if (onInteractStateChanged) onInteractStateChanged(down);
            }
        }
    }
    
    return CallNextHookEx(m_hKeyboardHook, nCode, wParam, lParam);
}

} // namespace TinyHand
