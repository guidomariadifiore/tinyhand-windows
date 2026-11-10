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
    
    // Create double-buffered DIB sections
    HDC hdcScreen = GetDC(NULL);
    BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER) };
    bmi.bmiHeader.biWidth = m_screenW;
    bmi.bmiHeader.biHeight = -m_screenH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    for (int i = 0; i < 2; ++i) {
        m_hdcMem[i] = CreateCompatibleDC(hdcScreen);
        m_hBitmap[i] = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_bits[i], NULL, 0);
        m_hOldBitmap[i] = SelectObject(m_hdcMem[i], m_hBitmap[i]);
    }
    ReleaseDC(NULL, hdcScreen);
    
    m_frontBuffer = 0;
    m_geometryDirty = true;
    
    // Create Toolbar
    m_toolbar.Create(hInstance);
    m_toolbar.SetPinnedStyles(m_settings.pinnedStyles);
    m_toolbar.SetCurrentStyle(m_settings.currentStyle);
    m_toolbar.SetSoundEnabled(m_settings.soundEnabled);
    m_toolbar.SetInkChoice(m_settings.inkChoice);
    
    m_toolbar.onSelectStyle = [this](PointerStyleId style) {
        {
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
            m_settings.currentStyle = style;
            m_toolbar.SetCurrentStyle(style);
            m_needsRedraw.store(true, std::memory_order_relaxed);
        }
        Preferences::Save(m_settings);
        if (m_hWakeEvent) SetEvent(m_hWakeEvent);
    };
    
    m_toolbar.onToggleSound = [this]() {
        {
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
            if (m_settings.currentStyle == PointerStyleId::Laser) {
                m_settings.laserSound = !m_settings.laserSound;
                m_toolbar.SetSoundEnabled(m_settings.laserSound);
            } else {
                m_settings.soundEnabled = !m_settings.soundEnabled;
                m_toolbar.SetSoundEnabled(m_settings.soundEnabled);
            }
        }
        Preferences::Save(m_settings);
    };
    
    m_toolbar.onExit = [this]() {
        Deactivate();
    };
    
    m_toolbar.onSelectInk = [this](InkChoiceId choice) {
        {
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
            m_settings.inkChoice = choice;
            m_toolbar.SetInkChoice(choice);
        }
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
    StopRenderThread();
    m_toolbar.Destroy();
    
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    for (int i = 0; i < 2; ++i) {
        if (m_hdcMem[i]) {
            SelectObject(m_hdcMem[i], m_hOldBitmap[i]);
            DeleteDC(m_hdcMem[i]);
            m_hdcMem[i] = NULL;
        }
        if (m_hBitmap[i]) {
            DeleteObject(m_hBitmap[i]);
            m_hBitmap[i] = NULL;
        }
        m_bits[i] = NULL;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

void OverlayWindow::StartRenderThread() {
    if (m_renderThread) return;
    m_renderThreadRunning.store(true, std::memory_order_relaxed);
    m_hWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    m_renderThread = CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
        OverlayWindow* self = (OverlayWindow*)param;
        self->RenderThreadProc();
        return 0;
    }, this, 0, NULL);
    if (m_renderThread) {
        SetThreadPriority(m_renderThread, THREAD_PRIORITY_HIGHEST);
    }
}

void OverlayWindow::StopRenderThread() {
    if (!m_renderThread) return;
    m_renderThreadRunning.store(false, std::memory_order_relaxed);
    if (m_hWakeEvent) {
        SetEvent(m_hWakeEvent);
    }
    WaitForSingleObject(m_renderThread, 1000);
    CloseHandle(m_renderThread);
    m_renderThread = NULL;
    if (m_hWakeEvent) {
        CloseHandle(m_hWakeEvent);
        m_hWakeEvent = NULL;
    }
}

void OverlayWindow::ExtendInkIfDrawing(double x, double y) {
    if (!m_held.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!m_held.load(std::memory_order_relaxed)) return;
    
    double dx = x - m_downX;
    double dy = y - m_downY;
    if (!m_ink.IsDrawing()) {
        if (dx * dx + dy * dy >= 16.0) {
            double inkWidth = (std::max)(3.0, m_screenH * 0.0055);
            Gdiplus::Color inkColor = ResolveInkColor(m_settings.inkChoice, m_settings.currentStyle);
            m_ink.Begin(m_downX, m_downY, inkWidth, inkColor);
            m_ink.Extend(x, y);
        }
    } else {
        m_ink.Extend(x, y);
    }
}

void OverlayWindow::RenderThreadProc() {
    timeBeginPeriod(1);
    
    BOOL dwmEnabled = FALSE;
    HRESULT hr = DwmIsCompositionEnabled(&dwmEnabled);
    if (FAILED(hr)) dwmEnabled = FALSE;
    
    while (m_renderThreadRunning.load(std::memory_order_relaxed)) {
        if (!m_isActive.load(std::memory_order_relaxed)) {
            WaitForSingleObject(m_hWakeEvent, 50);
            continue;
        }
        
        // 1. Directly query actual OS cursor position in real-time
        POINT curPt;
        if (GetCursorPos(&curPt)) {
            double prevX = m_cursorX.load(std::memory_order_relaxed);
            double prevY = m_cursorY.load(std::memory_order_relaxed);
            if (curPt.x != (int)prevX || curPt.y != (int)prevY) {
                m_cursorX.store((double)curPt.x, std::memory_order_relaxed);
                m_cursorY.store((double)curPt.y, std::memory_order_relaxed);
                m_lastMoveTime.store(GetTickCount64(), std::memory_order_relaxed);
                m_needsRedraw.store(true, std::memory_order_relaxed);
                
                if (m_held.load(std::memory_order_relaxed)) {
                    ExtendInkIfDrawing(curPt.x, curPt.y);
                }
            }
        }
        
        bool needRender = false;
        {
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
            ULONGLONG now = GetTickCount64();
            bool isMoving = (now - m_lastMoveTime.load(std::memory_order_relaxed)) < 100;
            bool isAnimating = m_arm.IsSettling() || m_ink.IsDrawing() || 
                               (m_settings.fadeSeconds > 0 && m_ink.StrokeCount() > 0) ||
                               m_toolbar.IsRevealed() || m_held.load(std::memory_order_relaxed) ||
                               m_needsRedraw.load(std::memory_order_relaxed);
            needRender = isMoving || isAnimating;
        }
        
        if (needRender) {
            {
                std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
                TickAndRender();
            }
            if (dwmEnabled) {
                if (FAILED(DwmFlush())) {
                    Sleep(7);
                }
            } else {
                Sleep(7);
            }
        } else {
            // Polling interval of 8ms ensures ultra-smooth responsiveness (~120Hz)
            WaitForSingleObject(m_hWakeEvent, 8);
        }
    }
    
    timeEndPeriod(1);
}

void OverlayWindow::ApplySettings(const Settings& s) {
    m_toolbar.SetPinnedStyles(s.pinnedStyles);
    m_toolbar.SetCurrentStyle(s.currentStyle);
    m_toolbar.SetSoundEnabled(s.currentStyle == PointerStyleId::Laser ? s.laserSound : s.soundEnabled);
    m_toolbar.SetInkChoice(s.inkChoice);
    
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        m_settings = s;
        m_needsRedraw.store(true, std::memory_order_relaxed);
    }
    if (m_hWakeEvent) SetEvent(m_hWakeEvent);
}

bool OverlayWindow::Activate() {
    if (m_isActive.load(std::memory_order_relaxed)) return true;
    m_isActive.store(true, std::memory_order_relaxed);
    
    // Prevent screen / display from sleeping while overlay is active
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    
    POINT pt = { 0, 0 };
    if (!GetCursorPos(&pt)) {
        pt.x = m_screenX + m_screenW / 2;
        pt.y = m_screenY + m_screenH / 2;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        m_cursorX.store((double)pt.x, std::memory_order_relaxed);
        m_cursorY.store((double)pt.y, std::memory_order_relaxed);
        m_held.store(false, std::memory_order_relaxed);
        m_normalCursor.store(false, std::memory_order_relaxed);
        m_needsRedraw.store(true, std::memory_order_relaxed);
        m_geometryDirty = true;
        m_arm.ResetGrip();
    }
    
    // Hide default cursor
    CursorManager::Instance().Hide();
    
    // Show overlay window without activating
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(m_hwnd, HWND_TOPMOST, m_screenX, m_screenY, m_screenW, m_screenH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
                 
    m_toolbar.Show(true);
    m_toolbar.RevealTemporarily(3.0);
    
    ULONGLONG now = GetTickCount64();
    m_lastMoveTime.store(now, std::memory_order_relaxed);
    m_stillSince = now;
    
    InputHook::Instance().Install();
    InputHook::Instance().SetActive(true);
    
    m_timerId = SetTimer(m_hwnd, 1, 16, NULL);
    StartRenderThread();
    if (m_hWakeEvent) SetEvent(m_hWakeEvent);
    
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        TickAndRender();
    }
    
    if (onActiveChanged) onActiveChanged(true);
    return true;
}

void OverlayWindow::Deactivate() {
    if (!m_isActive.load(std::memory_order_relaxed)) return;
    m_isActive.store(false, std::memory_order_relaxed);
    
    // Restore normal power management
    SetThreadExecutionState(ES_CONTINUOUS);
    
    if (m_timerId) {
        KillTimer(m_hwnd, m_timerId);
        m_timerId = 0;
    }
    StopRenderThread();
    
    InputHook::Instance().SetActive(false);
    CursorManager::Instance().Show();
    
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        m_held.store(false, std::memory_order_relaxed);
        m_normalCursor.store(false, std::memory_order_relaxed);
        m_needsRedraw.store(false, std::memory_order_relaxed);
        m_ink.Clear();
    }
    
    ShowWindow(m_hwnd, SW_HIDE);
    m_toolbar.Show(false);
    
    if (onActiveChanged) onActiveChanged(false);
}

void OverlayWindow::Toggle() {
    if (m_isActive.load(std::memory_order_relaxed)) {
        Deactivate();
    } else {
        Activate();
    }
}

void OverlayWindow::ClearInk() {
    m_ink.Clear();
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        m_needsRedraw.store(true, std::memory_order_relaxed);
    }
    if (m_hWakeEvent) SetEvent(m_hWakeEvent);
}

void OverlayWindow::HandleMouseDown(int x, int y) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        m_held.store(true, std::memory_order_relaxed);
        m_downX = x;
        m_downY = y;
        m_cursorX.store((double)x, std::memory_order_relaxed);
        m_cursorY.store((double)y, std::memory_order_relaxed);
        m_arm.Press();
        m_lastMoveTime.store(GetTickCount64(), std::memory_order_relaxed);
        m_needsRedraw.store(true, std::memory_order_relaxed);
    }
    
    bool playSound = (m_settings.currentStyle == PointerStyleId::Laser)
        ? m_settings.laserSound
        : m_settings.soundEnabled;
        
    if (playSound) {
        TapSoundPlayer::Instance().Play(m_settings.tapSound);
    }
    
    if (m_hWakeEvent) SetEvent(m_hWakeEvent);
}

void OverlayWindow::HandleMouseUp(int x, int y) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        m_held.store(false, std::memory_order_relaxed);
        m_ink.End();
        m_lastMoveTime.store(GetTickCount64(), std::memory_order_relaxed);
        m_needsRedraw.store(true, std::memory_order_relaxed);
    }
    if (m_hWakeEvent) SetEvent(m_hWakeEvent);
}

void OverlayWindow::HandleMouseMove(int x, int y) {
    m_cursorX.store((double)x, std::memory_order_relaxed);
    m_cursorY.store((double)y, std::memory_order_relaxed);
    m_lastMoveTime.store(GetTickCount64(), std::memory_order_relaxed);
    m_needsRedraw.store(true, std::memory_order_relaxed);
    
    if (m_held.load(std::memory_order_relaxed)) {
        ExtendInkIfDrawing((double)x, (double)y);
    }
    
    if (m_hWakeEvent) {
        SetEvent(m_hWakeEvent);
    }
}

void OverlayWindow::HandleInteract(bool held) {
    bool shouldBeNormal = held;
    if (!shouldBeNormal) {
        POINT pt = { (LONG)m_cursorX.load(), (LONG)m_cursorY.load() };
        shouldBeNormal = InputHook::Instance().IsOverInteractiveSystemUI(pt) ||
                         m_toolbar.IsOverInteractive(m_cursorX.load(), m_cursorY.load());
    }
    bool prevNormal = m_normalCursor.load(std::memory_order_relaxed);
    if (shouldBeNormal != prevNormal) {
        m_normalCursor.store(shouldBeNormal, std::memory_order_relaxed);
        if (shouldBeNormal) {
            CursorManager::Instance().Show();
        } else {
            if (m_isActive.load(std::memory_order_relaxed)) CursorManager::Instance().Hide();
        }
    }
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        m_interactHeld.store(held, std::memory_order_relaxed);
        m_needsRedraw.store(true, std::memory_order_relaxed);
    }
    if (m_hWakeEvent) SetEvent(m_hWakeEvent);
}

void OverlayWindow::TickAndRender() {
    if (!m_hwnd || !m_isActive.load(std::memory_order_relaxed)) return;
    
    int backIdx = 1 - m_frontBuffer;
    if (!m_bits[backIdx] || !m_hdcMem[backIdx]) return;
    
    ULONGLONG now = GetTickCount64();
    double curX = m_cursorX.load(std::memory_order_relaxed);
    double curY = m_cursorY.load(std::memory_order_relaxed);
    
    // 1. Step animations
    m_arm.StepAnimation(m_held.load(std::memory_order_relaxed));
    
    m_toolbar.Update(curX, curY, m_screenX, m_screenY, m_screenW, m_screenH,
                     m_ink.IsDrawing(), now);
                     
    // Set pass-through rects for Toolbar (only revealed interactive regions, not transparent padding)
    std::vector<RECT> passRects = m_toolbar.GetInteractiveRects();
    InputHook::Instance().SetPassThroughRects(passRects);
    
    // Automatically switch to normal cursor if over Taskbar, system tray, menus, or interactive toolbar
    POINT pt = { (LONG)curX, (LONG)curY };
    bool overSystemUI = InputHook::Instance().IsOverInteractiveSystemUI(pt);
    bool overToolbar = m_toolbar.IsOverInteractive(curX, curY);
    bool shouldBeNormal = m_interactHeld.load(std::memory_order_relaxed) || overSystemUI || overToolbar;
    bool prevNormal = m_normalCursor.load(std::memory_order_relaxed);
    if (shouldBeNormal != prevNormal) {
        m_normalCursor.store(shouldBeNormal, std::memory_order_relaxed);
        if (shouldBeNormal) {
            CursorManager::Instance().Show();
        } else {
            if (m_isActive.load(std::memory_order_relaxed)) CursorManager::Instance().Hide();
        }
    }
    
    double localCursorX = curX - m_screenX;
    double localCursorY = curY - m_screenY;
    
    // 2. Clear backbuffer screen to 100% transparent black
    memset(m_bits[backIdx], 0, m_screenW * m_screenH * 4);
    
    // 3. Render content into memory DC
    {
        Gdiplus::Graphics g(m_hdcMem[backIdx]);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        
        // 1. Draw Ink (points recorded in screen coordinates, translated to window surface)
        g.TranslateTransform((float)-m_screenX, (float)-m_screenY);
        m_ink.Render(g, m_settings.fadeSeconds, now);
        g.ResetTransform();
        
        // 2. Draw Arm or Laser (if not in interact/normal cursor mode)
        if (!m_normalCursor.load(std::memory_order_relaxed)) {
            m_arm.RenderArm(g, m_settings.currentStyle, localCursorX, localCursorY,
                            m_screenW, m_screenH, m_settings.showShadow, m_settings.pointerSize);
        }
    }
    
    // 4. Present atomic frame to DWM (only pass geometry pointers when dirty)
    POINT ptDst = { m_screenX, m_screenY };
    SIZE sz = { m_screenW, m_screenH };
    POINT ptSrc = { 0, 0 };
    
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    UpdateLayeredWindow(m_hwnd, NULL, &ptDst, &sz, m_hdcMem[backIdx], &ptSrc, 0, &blend, ULW_ALPHA);
    m_geometryDirty = false;
    m_frontBuffer = backIdx;
    m_needsRedraw.store(false, std::memory_order_relaxed);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER: {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        TickAndRender();
        return 0;
    }
        
    case WM_ERASEBKGND:
        return 1;
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
        
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
        
    case WM_NCHITTEST:
        return HTTRANSPARENT;
        
    case WM_DISPLAYCHANGE: {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
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
            
            // Recreate double-buffered DIB sections with new dimensions
            HDC hdcScreen = GetDC(NULL);
            for (int i = 0; i < 2; ++i) {
                if (m_hdcMem[i]) {
                    SelectObject(m_hdcMem[i], m_hOldBitmap[i]);
                    if (m_hBitmap[i]) DeleteObject(m_hBitmap[i]);
                    
                    BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER) };
                    bmi.bmiHeader.biWidth = m_screenW;
                    bmi.bmiHeader.biHeight = -m_screenH;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    
                    m_hBitmap[i] = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_bits[i], NULL, 0);
                    m_hOldBitmap[i] = SelectObject(m_hdcMem[i], m_hBitmap[i]);
                }
            }
            ReleaseDC(NULL, hdcScreen);
            m_frontBuffer = 0;
        }
        
        m_geometryDirty = true;
        if (m_hwnd) {
            SetWindowPos(m_hwnd, HWND_TOPMOST, m_screenX, m_screenY, m_screenW, m_screenH,
                         SWP_NOACTIVATE);
        }
        m_needsRedraw.store(true, std::memory_order_relaxed);
        if (m_hWakeEvent) SetEvent(m_hWakeEvent);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace TinyHand
