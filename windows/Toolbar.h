#pragma once

#include "Common.h"
#include <functional>

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
    bool IsRevealed() const { return m_alpha > 0.05f; }
    RECT GetTotalRect() const;
    std::vector<RECT> GetInteractiveRects() const;
    bool IsOverInteractive(double screenX, double screenY) const;
    
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
    void RecalculateLayout();
    ToolbarItem* HitTest(int x, int y);
    
    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    
    PointerStyleId m_currentStyle = PointerStyleId::Lavender;
    std::vector<PointerStyleId> m_pinnedStyles;
    bool m_soundEnabled = true;
    InkChoiceId m_inkChoice = InkChoiceId::Match;
    bool m_inkOpen = false;
    
    float m_alpha = 0.0f;
    float m_targetAlpha = 0.0f;
    ULONGLONG m_revealUntil = 0;
    
    double m_screenX = 0;
    double m_screenY = 0;
    double m_screenW = 1920;
    double m_screenH = 1080;
    
    int m_windowW = 400;
    int m_windowH = 120;
    int m_windowX = 0;
    int m_windowY = 0;
    
    Gdiplus::RectF m_barRect;
    Gdiplus::RectF m_trayRect;
    std::vector<ToolbarItem> m_items;
    
    int m_hoveredIndex = -1;
};

} // namespace TinyHand
