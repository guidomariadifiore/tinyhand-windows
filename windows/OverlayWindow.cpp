#include "OverlayWindow.h"
#include "InputHook.h"
#include "CursorManager.h"
#include "TapSound.h"
#include "Preferences.h"
#include <cmath>

namespace TinyHand {

static const wchar_t* OVERLAY_CLASS = L"TinyHand_OverlayClass";

OverlayWindow::OverlayWindow() {
}

OverlayWindow::~OverlayWindow() {
    Destroy();
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = (OverlayWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool OverlayWindow::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = OVERLAY_CLASS;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassExW(&wc);
    
    m_screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (m_screenW <= 0) m_screenW = 1920;
    if (m_screenH <= 0) m_screenH = 1080;
    
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        OVERLAY_CLASS,
        L"TinyHand Overlay",
        WS_POPUP,
        m_screenX, m_screenY, m_screenW, m_screenH,
        NULL, NULL, hInstance, NULL
    );
    
    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    
    // Create DIB section
    HDC hdcScreen = GetDC(NULL);
    m_hdcMem = CreateCompatibleDC(hdcScreen);
    
    BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER) };
    bmi.bmiHeader.biWidth = m_screenW;
    bmi.bmiHeader.biHeight = -m_screenH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    m_hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_bits, NULL, 0);
    m_hOldBitmap = SelectObject(m_hdcMem, m_hBitmap);
    ReleaseDC(NULL, hdcScreen);
    
    // Create Toolbar
    m_toolbar.Create(hInstance);
    m_toolbar.SetPinnedStyles(m_settings.pinnedStyles);
    m_toolbar.SetCurrentStyle(m_settings.currentStyle);
    m_toolbar.SetSoundEnabled(m_settings.soundEnabled);
    m_toolbar.SetInkChoice(m_settings.inkChoice);
    
    m_toolbar.onSelectStyle = [this](PointerStyleId style) {
        m_settings.currentStyle = style;
        m_toolbar.SetCurrentStyle(style);
        Preferences::Save(m_settings);
        Render();
    };
    
    m_toolbar.onToggleSound = [this]() {
        if (m_settings.currentStyle == PointerStyleId::Laser) {
            m_settings.laserSound = !m_settings.laserSound;
            m_toolbar.SetSoundEnabled(m_settings.laserSound);
        } else {
            m_settings.soundEnabled = !m_settings.soundEnabled;
            m_toolbar.SetSoundEnabled(m_settings.soundEnabled);
        }
        Preferences::Save(m_settings);
    };
    
    m_toolbar.onExit = [this]() {
        Deactivate();
    };
    
    m_toolbar.onSelectInk = [this](InkChoiceId choice) {
        m_settings.inkChoice = choice;
        m_toolbar.SetInkChoice(choice);
        Preferences::Save(m_settings);
    };
    
    // Connect input hook callbacks
    auto& hook = InputHook::Instance();
    hook.SetOverlayHwnd(m_hwnd);
    hook.onMouseDown = [this](int x, int y) { HandleMouseDown(x, y); };
    hook.onMouseUp = [this](int x, int y) { HandleMouseUp(x, y); };
    hook.onMouseMove = [this](int x, int y) { HandleMouseMove(x, y); };
    hook.onInteractStateChanged = [this](bool held) { HandleInteract(held); };
    hook.onEscapePressed = [this]() { Deactivate(); };
    
    return true;
}

void OverlayWindow::Destroy() {
    Deactivate();
    m_toolbar.Destroy();
    
    if (m_hdcMem) {
        SelectObject(m_hdcMem, m_hOldBitmap);
        DeleteDC(m_hdcMem);
        m_hdcMem = NULL;
    }
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
        m_hBitmap = NULL;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

void OverlayWindow::ApplySettings(const Settings& s) {
    m_settings = s;
    m_toolbar.SetPinnedStyles(s.pinnedStyles);
    m_toolbar.SetCurrentStyle(s.currentStyle);
    m_toolbar.SetSoundEnabled(s.currentStyle == PointerStyleId::Laser ? s.laserSound : s.soundEnabled);
    m_toolbar.SetInkChoice(s.inkChoice);
    if (m_isActive) Render();
}

bool OverlayWindow::Activate() {
    if (m_isActive) return true;
    m_isActive = true;
    
    // Prevent screen / display from sleeping while overlay is active (e.g. watching video)
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    
    POINT pt;
    GetCursorPos(&pt);
    m_cursorX = pt.x;
    m_cursorY = pt.y;
    
    m_held = false;
    m_normalCursor = false;
    m_arm.ResetGrip();
    
    // Hide default cursor
    CursorManager::Instance().Hide();
    
    // Show overlay window without activating
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(m_hwnd, HWND_TOPMOST, m_screenX, m_screenY, m_screenW, m_screenH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
                 
    m_toolbar.Show(true);
    m_toolbar.RevealTemporarily(3.0);
    
    // Start pacing timer (60fps ~ 16ms)
    m_timerId = SetTimer(m_hwnd, 1, 16, NULL);
    m_lastMoveTime = GetTickCount64();
    m_stillSince = m_lastMoveTime;
    
    InputHook::Instance().Install();
    InputHook::Instance().SetActive(true);
    
    Render();
    if (onActiveChanged) onActiveChanged(true);
    return true;
}

void OverlayWindow::Deactivate() {
    if (!m_isActive) return;
    m_isActive = false;
    
    // Restore normal power management
    SetThreadExecutionState(ES_CONTINUOUS);
    
    if (m_timerId) {
        KillTimer(m_hwnd, m_timerId);
        m_timerId = 0;
    }
    
    InputHook::Instance().SetActive(false);
    CursorManager::Instance().Show();
    
    m_held = false;
    m_normalCursor = false;
    m_ink.Clear();
    
    ShowWindow(m_hwnd, SW_HIDE);
    m_toolbar.Show(false);
    
    if (onActiveChanged) onActiveChanged(false);
}

void OverlayWindow::Toggle() {
    if (m_isActive) {
        Deactivate();
    } else {
        Activate();
    }
}

void OverlayWindow::ClearInk() {
    m_ink.Clear();
    if (m_isActive) Render();
}

void OverlayWindow::HandleMouseDown(int x, int y) {
    m_held = true;
    m_downX = x;
    m_downY = y;
    m_cursorX = x;
    m_cursorY = y;
    
    m_arm.Press();
    
    bool playSound = (m_settings.currentStyle == PointerStyleId::Laser)
        ? m_settings.laserSound
        : m_settings.soundEnabled;
        
    if (playSound) {
        TapSoundPlayer::Instance().Play(m_settings.tapSound);
    }
    
    m_lastMoveTime = GetTickCount64();
    Render();
}

void OverlayWindow::HandleMouseUp(int x, int y) {
    m_held = false;
    m_ink.End();
    m_lastMoveTime = GetTickCount64();
    Render();
}

void OverlayWindow::HandleMouseMove(int x, int y) {
    m_cursorX = x;
    m_cursorY = y;
    m_lastMoveTime = GetTickCount64();
    
    if (m_held) {
        double dx = x - m_downX;
        double dy = y - m_downY;
        if (!m_ink.IsDrawing()) {
            if (sqrt(dx * dx + dy * dy) >= 4.0) {
                double inkWidth = (std::max)(3.0, m_screenH * 0.0055);
                Gdiplus::Color inkColor = ResolveInkColor(m_settings.inkChoice, m_settings.currentStyle);
                m_ink.Begin(m_downX, m_downY, inkWidth, inkColor);
                m_ink.Extend(x, y);
            }
        } else {
            m_ink.Extend(x, y);
        }
    }
}

void OverlayWindow::HandleInteract(bool held) {
    m_interactHeld = held;
    bool shouldBeNormal = m_interactHeld;
    if (!shouldBeNormal) {
        POINT pt = { (LONG)m_cursorX, (LONG)m_cursorY };
        shouldBeNormal = InputHook::Instance().IsOverInteractiveSystemUI(pt) ||
                         m_toolbar.IsOverInteractive(m_cursorX, m_cursorY);
    }
    if (shouldBeNormal != m_normalCursor) {
        m_normalCursor = shouldBeNormal;
        if (m_normalCursor) {
            CursorManager::Instance().Show();
        } else {
            if (m_isActive) CursorManager::Instance().Hide();
        }
    }
    Render();
}

void OverlayWindow::Tick() {
    if (!m_isActive) return;
    
    ULONGLONG now = GetTickCount64();
    m_arm.StepAnimation(m_held);
    
    m_toolbar.Update(m_cursorX, m_cursorY, m_screenX, m_screenY, m_screenW, m_screenH,
                     m_ink.IsDrawing(), now);
                     
    // Set pass-through rects for Toolbar (only revealed interactive regions, not transparent padding)
    std::vector<RECT> passRects = m_toolbar.GetInteractiveRects();
    InputHook::Instance().SetPassThroughRects(passRects);
    
    // Automatically switch to normal cursor if over Taskbar, system tray, menus, or interactive toolbar
    POINT pt = { (LONG)m_cursorX, (LONG)m_cursorY };
    bool overSystemUI = InputHook::Instance().IsOverInteractiveSystemUI(pt);
    bool overToolbar = m_toolbar.IsOverInteractive(m_cursorX, m_cursorY);
    bool shouldBeNormal = m_interactHeld || overSystemUI || overToolbar;
    if (shouldBeNormal != m_normalCursor) {
        m_normalCursor = shouldBeNormal;
        if (m_normalCursor) {
            CursorManager::Instance().Show();
        } else {
            if (m_isActive) CursorManager::Instance().Hide();
        }
        Render();
    }
    
    bool isMoving = (now - m_lastMoveTime) < 50;
    bool isAnimating = m_arm.IsSettling() || m_ink.IsDrawing() || (m_settings.fadeSeconds > 0 && m_ink.StrokeCount() > 0);
    
    if (isMoving || isAnimating || m_held) {
        Render();
    }
}

void OverlayWindow::Render() {
    if (!m_hwnd || !m_isActive) return;
    
    ULONGLONG now = GetTickCount64();
    HDC hdcScreen = GetDC(NULL);
    
    {
        Gdiplus::Graphics g(m_hdcMem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        
        // 1. Draw Ink
        m_ink.Render(g, m_settings.fadeSeconds, now);
        
        // 2. Draw Arm or Laser (if not in interact/normal cursor mode)
        if (!m_normalCursor) {
            double localCursorX = m_cursorX - m_screenX;
            double localCursorY = m_cursorY - m_screenY;
            m_arm.RenderArm(g, m_settings.currentStyle, localCursorX, localCursorY,
                            m_screenW, m_screenH, m_settings.showShadow, m_settings.pointerSize);
        }
    }
    
    POINT ptDst = { m_screenX, m_screenY };
    SIZE sz = { m_screenW, m_screenH };
    POINT ptSrc = { 0, 0 };
    
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, m_hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, hdcScreen);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        Tick();
        return 0;
        
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
        
    case WM_NCHITTEST:
        return HTTRANSPARENT;
        
    case WM_DISPLAYCHANGE: {
        int newX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int newY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int newW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int newH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (newW <= 0) newW = 1920;
        if (newH <= 0) newH = 1080;
        
        m_screenX = newX;
        m_screenY = newY;
        
        if (newW != m_screenW || newH != m_screenH) {
            m_screenW = newW;
            m_screenH = newH;
            
            // Recreate DIB section with new dimensions
            if (m_hdcMem) {
                SelectObject(m_hdcMem, m_hOldBitmap);
                if (m_hBitmap) DeleteObject(m_hBitmap);
                
                HDC hdcScreen = GetDC(NULL);
                BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER) };
                bmi.bmiHeader.biWidth = m_screenW;
                bmi.bmiHeader.biHeight = -m_screenH;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                
                m_hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_bits, NULL, 0);
                m_hOldBitmap = SelectObject(m_hdcMem, m_hBitmap);
                ReleaseDC(NULL, hdcScreen);
            }
        }
        
        if (m_hwnd) {
            SetWindowPos(m_hwnd, HWND_TOPMOST, m_screenX, m_screenY, m_screenW, m_screenH,
                         SWP_NOACTIVATE);
        }
        if (m_isActive) Render();
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace TinyHand
