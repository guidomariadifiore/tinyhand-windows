#pragma once

#include "Common.h"
#include <functional>

namespace TinyHand {

class SettingsWindow {
public:
    SettingsWindow();
    ~SettingsWindow();
    
    bool Create(HINSTANCE hInstance);
    void Show(const Settings& currentSettings);
    void Hide();
    
    std::function<void(const Settings&)> onSettingsSaved;
    
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void InitControls();
    void LoadSettingsIntoUI(const Settings& s);
    Settings ReadSettingsFromUI();
    
    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    Settings m_currentSettings;
    
    // Control HWNDs
    HWND m_comboStyle = NULL;
    HWND m_sliderSize = NULL;
    HWND m_chkShadow = NULL;
    HWND m_comboInkColor = NULL;
    HWND m_comboInkFade = NULL;
    HWND m_chkSound = NULL;
    HWND m_comboTapSound = NULL;
    HWND m_btnTestSound = NULL;
    HWND m_chkLaunchAtStartup = NULL;
    HWND m_btnSave = NULL;
    HWND m_btnCancel = NULL;
};

} // namespace TinyHand
