#pragma once

#include "Common.h"
#include "ArmRenderer.h"
#include "Ink.h"
#include "Toolbar.h"
#include <functional>

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
    
    bool IsActive() const { return m_isActive; }
    
    void ApplySettings(const Settings& s);
    const Settings& GetSettings() const { return m_settings; }
    
    // Callbacks
    std::function<void(bool)> onActiveChanged;
    std::function<void()> onOpenSettings;
    
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void Tick();
    void Render();
    void HandleMouseDown(int x, int y);
    void HandleMouseUp(int x, int y);
    void HandleMouseMove(int x, int y);
    void HandleInteract(bool held);
    
    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    UINT_PTR m_timerId = 0;
    
    Settings m_settings;
    ArmRenderer m_arm;
    InkManager m_ink;
    ToolbarWindow m_toolbar;
    
    bool m_isActive = false;
    bool m_held = false;
    bool m_interactHeld = false;
    bool m_normalCursor = false;
    
    double m_cursorX = 0;
    double m_cursorY = 0;
    double m_downX = 0;
    double m_downY = 0;
    
    int m_screenX = 0;
    int m_screenY = 0;
    int m_screenW = 1920;
    int m_screenH = 1080;
    
    ULONGLONG m_lastMoveTime = 0;
    ULONGLONG m_stillSince = 0;
    
    HDC m_hdcMem = NULL;
    HBITMAP m_hBitmap = NULL;
    HGDIOBJ m_hOldBitmap = NULL;
    void* m_bits = NULL;
};

} // namespace TinyHand
