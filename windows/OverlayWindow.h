#pragma once

#include "Common.h"
#include "ArmRenderer.h"
#include "Ink.h"
#include "Toolbar.h"
#include <dwmapi.h>
#include <functional>
#include <mutex>
#include <atomic>

#pragma comment(lib, "dwmapi.lib")

namespace TinyHand {

class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();
    
    bool Create(HINSTANCE hInstance);
    void Destroy();
    
    bool Activate();
    void Deactivate();
    void Toggle();
    void ClearInk();
    
    bool IsActive() const { return m_isActive.load(); }
    void* GetFrontBufferBits() const { return m_bits[m_frontBuffer]; }
    int GetScreenWidth() const { return m_screenW; }
    int GetScreenHeight() const { return m_screenH; }
    double GetCursorX() const { return m_cursorX.load(); }
    double GetCursorY() const { return m_cursorY.load(); }
    int GetRefreshRate() const { return m_displayRefreshRate.load(); }
    
    void ApplySettings(const Settings& s);
    const Settings& GetSettings() const { return m_settings; }
    
    // Callbacks
    std::function<void(bool)> onActiveChanged;
    std::function<void()> onOpenSettings;
    
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void TickAndRender();
    void StartRenderThread();
    void StopRenderThread();
    void RenderThreadProc();
    int DetectRefreshRate();
    void ExtendInkIfDrawing(double x, double y);
    
    void HandleMouseDown(int x, int y);
    void HandleMouseUp(int x, int y);
    void HandleMouseMove(int x, int y);
    void HandleInteract(bool held);
    
    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    UINT_PTR m_timerId = 0;
    
    HANDLE m_renderThread = NULL;
    HANDLE m_hWakeEvent = NULL;
    std::atomic<bool> m_renderThreadRunning{ false };
    
    std::recursive_mutex m_stateMutex;
    std::atomic<bool> m_needsRedraw{ false };
    
    Settings m_settings;
    ArmRenderer m_arm;
    InkManager m_ink;
    ToolbarWindow m_toolbar;
    
    std::atomic<bool> m_isActive{ false };
    std::atomic<bool> m_held{ false };
    std::atomic<bool> m_interactHeld{ false };
    std::atomic<bool> m_normalCursor{ false };
    
    std::atomic<double> m_cursorX{ 0 };
    std::atomic<double> m_cursorY{ 0 };
    double m_downX = 0;
    double m_downY = 0;
    
    int m_screenX = 0;
    int m_screenY = 0;
    int m_screenW = 1920;
    int m_screenH = 1080;
    std::atomic<int> m_displayRefreshRate{ 144 };
    
    bool m_geometryDirty = true;
    
    std::atomic<ULONGLONG> m_lastMoveTime{ 0 };
    ULONGLONG m_stillSince = 0;
    
    HDC m_hdcMem[2] = { NULL, NULL };
    HBITMAP m_hBitmap[2] = { NULL, NULL };
    HGDIOBJ m_hOldBitmap[2] = { NULL, NULL };
    void* m_bits[2] = { NULL, NULL };
    int m_frontBuffer = 0;
};

} // namespace TinyHand
