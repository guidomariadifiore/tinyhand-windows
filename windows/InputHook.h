#pragma once

#include <windows.h>
#include <functional>
#include <vector>

namespace TinyHand {

class InputHook {
public:
    static InputHook& Instance();
    
    bool Install();
    void Uninstall();
    
    void SetActive(bool active);
    bool IsActive() const { return m_active; }
    
    void SetPassThroughRects(const std::vector<RECT>& rects);
    void SetInteractVk(UINT vk) { m_interactVk = vk; }
    void SetOverlayHwnd(HWND hwnd) { m_overlayHwnd = hwnd; }
    
    bool IsOverInteractiveSystemUI(POINT pt) const;
    
    // Callbacks
    std::function<void(int, int)> onMouseDown;
    std::function<void(int, int)> onMouseUp;
    std::function<void(int, int)> onMouseMove;
    std::function<void(bool)> onInteractStateChanged;
    std::function<void()> onEscapePressed;
    std::function<void()> onToggleShortcut;
    std::function<void()> onClearShortcut;
    
private:
    InputHook();
    ~InputHook();
    
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    LRESULT HandleMouse(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT HandleKeyboard(int nCode, WPARAM wParam, LPARAM lParam);
    
    HHOOK m_hMouseHook = NULL;
    HHOOK m_hKeyboardHook = NULL;
    HWND m_overlayHwnd = NULL;
    
    bool m_active = false;
    bool m_isInterceptingDown = false;
    bool m_interactHeld = false;
    UINT m_interactVk = VK_MENU; // Default Alt
    
    std::vector<RECT> m_passThroughRects;
};

} // namespace TinyHand
