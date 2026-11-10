#pragma once

#include "Common.h"
#include <functional>

namespace TinyHand {

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();
    
    bool Create(HINSTANCE hInstance);
    void Destroy();
    
    void SetActive(bool active);
    void SetSoundEnabled(bool enabled);
    
    // Callbacks
    std::function<void()> onTogglePointer;
    std::function<void()> onClearDrawing;
    std::function<void()> onToggleSound;
    std::function<void()> onOpenSettings;
    std::function<void()> onExit;
    
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void ShowContextMenu();
    HICON CreateIconFromPng(const std::wstring& filename);
    
    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    NOTIFYICONDATAW m_nid = { sizeof(NOTIFYICONDATAW) };
    
    HICON m_hIconOn = NULL;
    HICON m_hIconOff = NULL;
    
    bool m_active = false;
    bool m_soundEnabled = true;
};

} // namespace TinyHand
