#pragma once

#include "Common.h"
#include <functional>
#include <mutex>
#include <atomic>
#include <vector>

namespace TinyHand {

enum class ToolbarButtonType {
    None,
    Style,
    InkToggle,
    InkChoice,
    SoundToggle,
    Exit
};

struct ToolbarItem {
    ToolbarButtonType type = ToolbarButtonType::None;
    PointerStyleId styleId = PointerStyleId::Lavender;
    InkChoiceId inkId = InkChoiceId::Match;
    Gdiplus::RectF rect;
};

class ToolbarWindow {
public:
    ToolbarWindow();
    ~ToolbarWindow();
    
    bool Create(HINSTANCE hInstance);
    void Destroy();
    
    void Update(double cursorX, double cursorY, double screenX, double screenY,
                double screenW, double screenH, bool isDrawing, ULONGLONG now);
                
    void Show(bool show);
    bool IsRevealed() const { return m_alpha.load(std::memory_order_relaxed) > 0.05f || m_targetAlpha > 0.5f; }
    RECT GetTotalRect() const;
    std::vector<RECT> GetInteractiveRects() const;
    bool IsOverInteractive(double screenX, double screenY) const;
    
    void NotifyCursorPos(double cursorX, double cursorY);
    void SetDrawing(bool isDrawing);
    void SetScreenGeometry(double screenX, double screenY, double screenW, double screenH);
    HWND GetHwnd() const { return m_hwnd; }
    
    // Callbacks
    std::function<void(PointerStyleId)> onSelectStyle;
    std::function<void()> onToggleSound;
    std::function<void()> onExit;
    std::function<void(InkChoiceId)> onSelectInk;
    
    void SetCurrentStyle(PointerStyleId style);
    void SetPinnedStyles(const std::vector<PointerStyleId>& pinned);
    void SetSoundEnabled(bool enabled);
    void SetInkChoice(InkChoiceId choice);
    
    void RevealTemporarily(double seconds);
    
    // Message handling
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void Render();
    void RenderContent();
    void RecalculateLayout();
    ToolbarItem* HitTest(int x, int y);
    void StartFadeTimer();
    void StopFadeTimer();
    void OnTimerTick();
    void UpdatePassThroughRects();
    
    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    HDC m_hdcMem = NULL;
    HBITMAP m_hBitmap = NULL;
    HGDIOBJ m_hOldBitmap = NULL;
    void* m_bits = NULL;
    bool m_contentDirty = true;
    mutable std::recursive_mutex m_toolbarMutex;
    
    PointerStyleId m_currentStyle = PointerStyleId::Lavender;
    std::vector<PointerStyleId> m_pinnedStyles;
    bool m_soundEnabled = true;
    InkChoiceId m_inkChoice = InkChoiceId::Match;
    bool m_inkOpen = false;
    
    std::atomic<float> m_alpha{ 0.0f };
    float m_targetAlpha = 0.0f;
    std::atomic<ULONGLONG> m_revealUntil{ 0 };
    std::atomic<bool> m_isDrawing{ false };
    bool m_timerActive = false;
    bool m_trackingMouse = false;
    
    double m_screenX = 0;
    double m_screenY = 0;
    double m_screenW = 1920;
    double m_screenH = 1080;
    
    int m_windowW = 400;
    int m_windowH = 130;
    int m_windowX = 0;
    int m_windowY = 0;
    
    Gdiplus::RectF m_barRect;
    Gdiplus::RectF m_trayRect;
    std::vector<ToolbarItem> m_items;
    
    int m_hoveredIndex = -1;
};

} // namespace TinyHand
