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
    if (!GetClassInfoExW(hInstance, OVERLAY_CLASS, &wc)) {
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = OVERLAY_CLASS;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
    }
    
    m_screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (m_screenW <= 0) m_screenW = 1920;
    if (m_screenH <= 0) m_screenH = 1080;
    m_displayRefreshRate.store(DetectRefreshRate(), std::memory_order_relaxed);
    
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
    RecreateBuffers(m_screenW, m_screenH);
    
    // Watchdog timer (every 500ms)
    m_watchdogTimerId = SetTimer(m_hwnd, 202, 500, NULL);
    
    // Create Toolbar on UI thread
    m_toolbar.Create(hInstance);
    m_toolbar.SetScreenGeometry(m_screenX, m_screenY, m_screenW, m_screenH);
    m_toolbar.SetPinnedStyles(m_settings.pinnedStyles);
    m_toolbar.SetCurrentStyle(m_settings.currentStyle);
    m_toolbar.SetSoundEnabled(m_settings.soundEnabled);
    m_toolbar.SetInkChoice(m_settings.inkChoice);
    
    m_toolbar.onSelectStyle = [this](PointerStyleId style) {
        Settings s = GetSettings();
        s.currentStyle = style;
        ApplySettings(s);
        Preferences::Save(s);
    };
    
    m_toolbar.onToggleSound = [this]() {
        Settings s = GetSettings();
        if (s.currentStyle == PointerStyleId::Laser) {
            s.laserSound = !s.laserSound;
        } else {
            s.soundEnabled = !s.soundEnabled;
        }
        ApplySettings(s);
        Preferences::Save(s);
    };
    
    m_toolbar.onExit = [this]() {
        Deactivate();
    };
    
    m_toolbar.onSelectInk = [this](InkChoiceId choice) {
        Settings s = GetSettings();
        s.inkChoice = choice;
        ApplySettings(s);
        Preferences::Save(s);
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
    if (m_watchdogTimerId) {
        KillTimer(m_hwnd, m_watchdogTimerId);
        m_watchdogTimerId = 0;
    }
    Deactivate();
    m_toolbar.Destroy();
    
    std::lock_guard<std::recursive_mutex> pLock(m_presentMutex);
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    for (int i = 0; i < 2; ++i) {
        m_graphics[i].reset();
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

void OverlayWindow::WakeRenderThread() {
    HANDLE hWake = m_hWakeEvent.load(std::memory_order_relaxed);
    if (hWake) {
        SetEvent(hWake);
    }
}

void OverlayWindow::StartRenderThread() {
    std::lock_guard<std::mutex> lock(m_threadLifecycleMutex);
    if (m_renderThread) {
        DWORD code = 0;
        if (GetExitCodeThread(m_renderThread, &code) && code == STILL_ACTIVE) {
            return;
        }
        CloseHandle(m_renderThread);
        m_renderThread = NULL;
    }
    m_renderThreadRunning.store(true, std::memory_order_release);
    if (!m_hWakeEvent.load(std::memory_order_relaxed)) {
        m_hWakeEvent.store(CreateEventW(NULL, FALSE, FALSE, NULL), std::memory_order_release);
    }
    m_renderThread = CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
        OverlayWindow* self = (OverlayWindow*)param;
        self->RenderThreadProc();
        return 0;
    }, this, 0, NULL);
    if (m_renderThread) {
        SetThreadPriority(m_renderThread, THREAD_PRIORITY_ABOVE_NORMAL);
    }
}

void OverlayWindow::RecreateBuffers(int newW, int newH) {
    if (newW <= 0) newW = 1920;
    if (newH <= 0) newH = 1080;
    
    std::lock_guard<std::recursive_mutex> pLock(m_presentMutex);
    std::lock_guard<std::recursive_mutex> sLock(m_stateMutex);
    
    m_screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_screenW = newW;
    m_screenH = newH;
    
    HDC hdcScreen = GetDC(NULL);
    BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER) };
    bmi.bmiHeader.biWidth = m_screenW;
    bmi.bmiHeader.biHeight = -m_screenH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    for (int i = 0; i < 2; ++i) {
        m_graphics[i].reset();
        if (m_hdcMem[i]) {
            if (m_hOldBitmap[i]) SelectObject(m_hdcMem[i], m_hOldBitmap[i]);
            if (m_hBitmap[i]) DeleteObject(m_hBitmap[i]);
            DeleteDC(m_hdcMem[i]);
            m_hdcMem[i] = NULL;
            m_hBitmap[i] = NULL;
            m_hOldBitmap[i] = NULL;
            m_bits[i] = NULL;
        }
        
        m_hdcMem[i] = CreateCompatibleDC(hdcScreen);
        m_hBitmap[i] = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_bits[i], NULL, 0);
        m_hOldBitmap[i] = SelectObject(m_hdcMem[i], m_hBitmap[i]);
        m_graphics[i] = std::make_unique<Gdiplus::Graphics>(m_hdcMem[i]);
        m_graphics[i]->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        m_graphics[i]->SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
        m_graphics[i]->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    }
    ReleaseDC(NULL, hdcScreen);
    
    m_frontBuffer = 0;
    m_geometryDirty.store(true, std::memory_order_release);
    m_needsRedraw.store(true, std::memory_order_relaxed);
}

void OverlayWindow::StopRenderThread() {
    std::lock_guard<std::mutex> lock(m_threadLifecycleMutex);
    if (!m_renderThread) return;
    m_renderThreadRunning.store(false, std::memory_order_release);
    HANDLE hWake = m_hWakeEvent.exchange(NULL, std::memory_order_acq_rel);
    if (hWake) {
        SetEvent(hWake);
    }
    WaitForSingleObject(m_renderThread, 2000);
    CloseHandle(m_renderThread);
    m_renderThread = NULL;
    if (hWake) {
        CloseHandle(hWake);
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

int OverlayWindow::DetectRefreshRate() {
    // 1. Query DWM composition timing (exact compositor VSync presentation rate, e.g. 144.001 Hz)
    DWM_TIMING_INFO ti = { sizeof(DWM_TIMING_INFO) };
    if (SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &ti))) {
        if (ti.rateRefresh.uiDenominator > 0 && ti.rateRefresh.uiNumerator > 0) {
            int rate = (int)((double)ti.rateRefresh.uiNumerator / (double)ti.rateRefresh.uiDenominator + 0.5);
            if (rate >= 30) return rate;
        }
    }
    
    // 2. Query specific monitor where the overlay window is located (supports mixed 60Hz/144Hz multi-monitors)
    HMONITOR hMon = MonitorFromWindow(m_hwnd ? m_hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    if (hMon) {
        MONITORINFOEXW mi = { sizeof(MONITORINFOEXW) };
        if (GetMonitorInfoW(hMon, &mi)) {
            DEVMODEW dm = { sizeof(DEVMODEW) };
            if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                if (dm.dmDisplayFrequency > 1) {
                    return (int)dm.dmDisplayFrequency;
                }
            }
        }
    }
    
    // 3. Fallback to primary display settings
    DEVMODEW dm = { sizeof(DEVMODEW) };
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        if (dm.dmDisplayFrequency > 1) {
            return (int)dm.dmDisplayFrequency;
        }
    }
    return 144;
}

void OverlayWindow::RenderThreadProc() {
    timeBeginPeriod(1);
    
    int targetFps = DetectRefreshRate();
    if (targetFps < 30) targetFps = 144;
    m_displayRefreshRate.store(targetFps, std::memory_order_relaxed);
    
    BOOL dwmEnabled = FALSE;
    HRESULT hr = DwmIsCompositionEnabled(&dwmEnabled);
    if (FAILED(hr)) dwmEnabled = FALSE;
    
    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);
    
    POINT lastPolledPt = { -9999, -9999 };
    
    while (m_renderThreadRunning.load(std::memory_order_relaxed)) {
        try {
            m_lastRenderTick.store(GetTickCount64(), std::memory_order_relaxed);
            
            if (!m_isActive.load(std::memory_order_relaxed)) {
                HANDLE hWake = m_hWakeEvent.load(std::memory_order_relaxed);
                if (hWake) WaitForSingleObject(hWake, 50);
                else Sleep(50);
                continue;
            }
            
            // 1. Directly query actual OS cursor position in real-time
            POINT curPt;
            if (GetCursorPos(&curPt)) {
                if (curPt.x != lastPolledPt.x || curPt.y != lastPolledPt.y) {
                    lastPolledPt = curPt;
                    m_cursorX.store((double)curPt.x, std::memory_order_relaxed);
                    m_cursorY.store((double)curPt.y, std::memory_order_relaxed);
                    m_lastMoveTime.store(GetTickCount64(), std::memory_order_relaxed);
                    m_needsRedraw.store(true, std::memory_order_relaxed);
                    
                    if (m_held.load(std::memory_order_relaxed)) {
                        ExtendInkIfDrawing(curPt.x, curPt.y);
                    }
                    
                    m_toolbar.NotifyCursorPos((double)curPt.x, (double)curPt.y);
                    InputHook::Instance().CheckAndReinstallWatchdog();
                    
                    // Automatically switch to normal cursor if over Taskbar, system tray, menus
                    bool overSys = InputHook::Instance().IsOverInteractiveSystemUI(curPt);
                    bool shouldBeNormal = m_interactHeld.load(std::memory_order_relaxed) || overSys;
                    bool prevNormal = m_normalCursor.load(std::memory_order_relaxed);
                    if (shouldBeNormal != prevNormal) {
                        m_normalCursor.store(shouldBeNormal, std::memory_order_relaxed);
                        m_needsRedraw.store(true, std::memory_order_relaxed);
                        if (m_hwnd) {
                            PostMessageW(m_hwnd, WM_USER_CURSOR_STATE, shouldBeNormal ? 1 : 0, 0);
                        }
                    }
                }
            }
            
            bool needRender = false;
            {
                std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
                ULONGLONG now = GetTickCount64();
                bool isMoving = (now - m_lastMoveTime.load(std::memory_order_relaxed)) < 150;
                bool isAnimating = m_arm.IsSettling() || m_ink.IsDrawing() || 
                                   (m_settings.fadeSeconds > 0 && m_ink.StrokeCount() > 0) ||
                                   m_held.load(std::memory_order_relaxed) ||
                                   m_needsRedraw.load(std::memory_order_relaxed);
                needRender = isMoving || isAnimating;
            }
            
            if (needRender) {
                LARGE_INTEGER frameStart;
                QueryPerformanceCounter(&frameStart);
                
                int backIdx = 0;
                bool hasFrame = false;
                {
                    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
                    hasFrame = RenderFrame(backIdx);
                }
                
                // Present frame to DWM outside m_stateMutex
                if (hasFrame) {
                    PresentFrame(backIdx);
                }
                
                int curFps = m_displayRefreshRate.load(std::memory_order_relaxed);
                if (curFps < 30) curFps = 144;
                double targetFrameMs = 1000.0 / curFps;
                
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                double elapsedMs = (double)(now.QuadPart - frameStart.QuadPart) * 1000.0 / qpcFreq.QuadPart;
                if (elapsedMs < targetFrameMs) {
                    DWORD sleepMs = (DWORD)(targetFrameMs - elapsedMs);
                    if (sleepMs > 0) {
                        Sleep(sleepMs);
                    }
                }
            } else {
                // Polling interval of 4ms ensures ultra-smooth responsiveness (250Hz OS cursor polling)
                // and instantaneous wakeup when input hook fires (<0.05ms)
                HANDLE hWake = m_hWakeEvent.load(std::memory_order_relaxed);
                if (hWake) WaitForSingleObject(hWake, 4);
                else Sleep(4);
            }
        } catch (...) {
            // Defensive recovery against unexpected faults
            Sleep(5);
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
        m_needsRedraw.store(true, std::memory_order_release);
        m_geometryDirty.store(true, std::memory_order_release);
    }
    WakeRenderThread();
    
    // Force immediate presentation tick even if render thread was idle or sleeping
    if (m_isActive.load(std::memory_order_relaxed)) {
        TickAndRender();
    }
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
        m_geometryDirty.store(true, std::memory_order_release);
        m_arm.ResetGrip();
    }
    
    // Hide default cursor
    CursorManager::Instance().Hide();
    
    // Show overlay window without activating
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(m_hwnd, HWND_TOPMOST, m_screenX, m_screenY, m_screenW, m_screenH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
                 
    m_toolbar.SetScreenGeometry(m_screenX, m_screenY, m_screenW, m_screenH);
    m_toolbar.Show(true);
    m_toolbar.RevealTemporarily(3.0);
    
    ULONGLONG now = GetTickCount64();
    m_lastMoveTime.store(now, std::memory_order_relaxed);
    m_stillSince = now;
    
    InputHook::Instance().Install();
    InputHook::Instance().SetActive(true);
    
    // Presentation loop is driven by RenderThreadProc at 144Hz VSync without 16ms timer judder
    StartRenderThread();
    WakeRenderThread();
    
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
    WakeRenderThread();
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
    
    m_toolbar.SetDrawing(true);
    WakeRenderThread();
}

void OverlayWindow::HandleMouseUp(int x, int y) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        m_held.store(false, std::memory_order_relaxed);
        m_ink.End();
        m_lastMoveTime.store(GetTickCount64(), std::memory_order_relaxed);
        m_needsRedraw.store(true, std::memory_order_relaxed);
    }
    m_toolbar.SetDrawing(false);
    WakeRenderThread();
}

void OverlayWindow::HandleMouseMove(int x, int y) {
    m_cursorX.store((double)x, std::memory_order_relaxed);
    m_cursorY.store((double)y, std::memory_order_relaxed);
    m_lastMoveTime.store(GetTickCount64(), std::memory_order_relaxed);
    m_needsRedraw.store(true, std::memory_order_relaxed);
    
    if (m_held.load(std::memory_order_relaxed)) {
        ExtendInkIfDrawing((double)x, (double)y);
    }
    
    m_toolbar.NotifyCursorPos((double)x, (double)y);
    WakeRenderThread();
}

void OverlayWindow::HandleInteract(bool held) {
    m_interactHeld.store(held, std::memory_order_relaxed);
    POINT pt = { (LONG)m_cursorX.load(), (LONG)m_cursorY.load() };
    bool shouldBeNormal = held || InputHook::Instance().IsOverInteractiveSystemUI(pt);
    bool prevNormal = m_normalCursor.load(std::memory_order_relaxed);
    if (shouldBeNormal != prevNormal) {
        m_normalCursor.store(shouldBeNormal, std::memory_order_relaxed);
        if (shouldBeNormal) {
            CursorManager::Instance().Show();
        } else {
            if (m_isActive.load(std::memory_order_relaxed)) CursorManager::Instance().Hide();
        }
    }
    m_needsRedraw.store(true, std::memory_order_relaxed);
    WakeRenderThread();
}

bool OverlayWindow::RenderFrame(int& outBackIdx) {
    if (!m_hwnd || !m_isActive.load(std::memory_order_relaxed)) return false;
    
    int backIdx = 1 - m_frontBuffer;
    if (!m_bits[backIdx] || !m_hdcMem[backIdx]) return false;
    
    ULONGLONG now = GetTickCount64();
    double curX = m_cursorX.load(std::memory_order_relaxed);
    double curY = m_cursorY.load(std::memory_order_relaxed);
    
    // 1. Step animations
    m_arm.StepAnimation(m_held.load(std::memory_order_relaxed));
    
    double localCursorX = curX - m_screenX;
    double localCursorY = curY - m_screenY;
    
    // 2. Clear backbuffer screen to 100% transparent black
    memset(m_bits[backIdx], 0, m_screenW * m_screenH * 4);
    
    // 3. Render content into memory DC
    {
        Gdiplus::Graphics* g = m_graphics[backIdx].get();
        if (g) {
            if (g->GetLastStatus() != Gdiplus::Ok) {
                m_graphics[backIdx] = std::make_unique<Gdiplus::Graphics>(m_hdcMem[backIdx]);
                m_graphics[backIdx]->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                m_graphics[backIdx]->SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
                m_graphics[backIdx]->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                g = m_graphics[backIdx].get();
            }
            
            // 1. Draw Ink (points recorded in screen coordinates, translated to window surface)
            if (m_ink.StrokeCount() > 0) {
                g->TranslateTransform((float)-m_screenX, (float)-m_screenY);
                m_ink.Render(*g, m_settings.fadeSeconds, now);
                g->ResetTransform();
            }
            
            // 2. Draw Arm or Laser (if not in interact/normal cursor mode)
            if (!m_normalCursor.load(std::memory_order_relaxed)) {
                m_arm.RenderArm(*g, m_settings.currentStyle, localCursorX, localCursorY,
                                m_screenW, m_screenH, m_settings.showShadow, m_settings.pointerSize);
            }
            g->Flush(Gdiplus::FlushIntentionSync);
        }
    }
    
    m_frontBuffer = backIdx;
    m_needsRedraw.store(false, std::memory_order_relaxed);
    outBackIdx = backIdx;
    return true;
}

void OverlayWindow::PresentFrame(int backIdx) {
    if (!m_hwnd || !m_bits[backIdx] || !m_hdcMem[backIdx]) return;
    
    std::lock_guard<std::recursive_mutex> pLock(m_presentMutex);
    
    POINT ptDst = { m_screenX, m_screenY };
    SIZE sz = { m_screenW, m_screenH };
    POINT ptSrc = { 0, 0 };
    
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    BOOL ok = UpdateLayeredWindow(m_hwnd, NULL, &ptDst,
                                  &sz,
                                  m_hdcMem[backIdx], &ptSrc, 0, &blend, ULW_ALPHA);
    if (!ok) {
        DWORD err = GetLastError();
        m_geometryDirty.store(true, std::memory_order_release);
        
        LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_LAYERED)) {
            SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        }
        
        if (err == ERROR_INVALID_HANDLE || err == ERROR_DC_NOT_FOUND || err == ERROR_INVALID_PARAMETER) {
            RecreateBuffers(m_screenW, m_screenH);
        }
    } else {
        m_geometryDirty.store(false, std::memory_order_release);
    }
}

void OverlayWindow::TickAndRender() {
    int backIdx = 0;
    bool hasFrame = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        hasFrame = RenderFrame(backIdx);
    }
    if (hasFrame) {
        PresentFrame(backIdx);
    }
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER: {
        if (wParam == 202) {
            // Watchdog tick (500ms)
            if (m_isActive.load(std::memory_order_relaxed)) {
                InputHook::Instance().CheckAndReinstallWatchdog();
                
                DWORD exitCode = 0;
                if (m_renderThread && GetExitCodeThread(m_renderThread, &exitCode)) {
                    if (exitCode != STILL_ACTIVE) {
                        StartRenderThread();
                    }
                }
                
                ULONGLONG lastTick = m_lastRenderTick.load(std::memory_order_relaxed);
                ULONGLONG now = GetTickCount64();
                if (lastTick > 0 && (now - lastTick) > 1000) {
                    WakeRenderThread();
                }
                if (lastTick > 0 && (now - lastTick) > 2000) {
                    TickAndRender();
                }
                if (lastTick > 0 && (now - lastTick) > 3500) {
                    StopRenderThread();
                    StartRenderThread();
                    m_lastRenderTick.store(now, std::memory_order_relaxed);
                }
            }
            return 0;
        } else {
            TickAndRender();
            return 0;
        }
    }
    
    case WM_USER_CURSOR_STATE: {
        bool shouldBeNormal = (wParam != 0);
        if (shouldBeNormal) {
            CursorManager::Instance().Show();
        } else {
            if (m_isActive.load(std::memory_order_relaxed)) {
                CursorManager::Instance().Hide();
            }
        }
        return 0;
    }
    
    case WM_USER_REINSTALL_HOOKS: {
        InputHook::Instance().Reinstall();
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
        StopRenderThread();
        
        m_displayRefreshRate.store(DetectRefreshRate(), std::memory_order_relaxed);
        int newX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int newY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int newW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int newH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (newW <= 0) newW = 1920;
        if (newH <= 0) newH = 1080;
        
        m_screenX = newX;
        m_screenY = newY;
        
        RecreateBuffers(newW, newH);
        
        if (m_hwnd) {
            SetWindowPos(m_hwnd, HWND_TOPMOST, m_screenX, m_screenY, m_screenW, m_screenH,
                         SWP_NOACTIVATE);
        }
        
        m_toolbar.SetScreenGeometry(m_screenX, m_screenY, m_screenW, m_screenH);
        m_needsRedraw.store(true, std::memory_order_relaxed);
        
        if (m_isActive.load(std::memory_order_relaxed)) {
            StartRenderThread();
            WakeRenderThread();
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace TinyHand
