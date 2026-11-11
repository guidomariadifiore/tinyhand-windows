#include "SettingsWindow.h"
#include "TapSound.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace TinyHand {

static const wchar_t* SETTINGS_CLASS = L"TinyHand_SettingsClass";

enum ControlIds {
    IDC_COMBO_STYLE = 2001,
    IDC_SLIDER_SIZE = 2002,
    IDC_CHK_SHADOW = 2003,
    IDC_COMBO_INK_COLOR = 2004,
    IDC_COMBO_INK_FADE = 2005,
    IDC_CHK_SOUND = 2006,
    IDC_COMBO_TAP_SOUND = 2007,
    IDC_BTN_TEST_SOUND = 2008,
    IDC_CHK_STARTUP = 2009,
    IDC_BTN_SAVE = 2010,
    IDC_BTN_CANCEL = 2011
};

SettingsWindow::SettingsWindow() {
}

SettingsWindow::~SettingsWindow() {
    Hide();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = (SettingsWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool SettingsWindow::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);
    
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = SETTINGS_CLASS;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    
    int winW = 460;
    int winH = 500;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 2;
    
    m_hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        SETTINGS_CLASS,
        L"TinyHand Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        winX, winY, winW, winH,
        NULL, NULL, hInstance, NULL
    );
    
    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    
    InitControls();
    return true;
}

void SettingsWindow::InitControls() {
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    
    int curY = 16;
    const int labelX = 20;
    const int ctrlX = 160;
    const int ctrlW = 250;
    const int rowH = 34;
    
    auto createLabel = [&](const wchar_t* text, int y) {
        HWND lbl = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 labelX, y + 4, ctrlX - labelX - 10, 20, m_hwnd, NULL, m_hInstance, NULL);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        return lbl;
    };
    
    // 1. Pointer Style
    createLabel(L"Pointer Style:", curY);
    m_comboStyle = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 ctrlX, curY, ctrlW, 200, m_hwnd, (HMENU)IDC_COMBO_STYLE, m_hInstance, NULL);
    SendMessageW(m_comboStyle, WM_SETFONT, (WPARAM)hFont, TRUE);
    for (const auto& s : GetAllStyles()) {
        SendMessageW(m_comboStyle, CB_ADDSTRING, 0, (LPARAM)s.title);
    }
    curY += rowH;
    
    // 2. Pointer Size
    createLabel(L"Pointer Size:", curY);
    m_sliderSize = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                 ctrlX, curY, ctrlW, 30, m_hwnd, (HMENU)IDC_SLIDER_SIZE, m_hInstance, NULL);
    SendMessageW(m_sliderSize, TBM_SETRANGE, TRUE, MAKELPARAM(8, 20));
    curY += rowH;
    
    // 3. Drop Shadow
    m_chkShadow = CreateWindowW(L"BUTTON", L"Show drop shadow", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                ctrlX, curY, ctrlW, 24, m_hwnd, (HMENU)IDC_CHK_SHADOW, m_hInstance, NULL);
    SendMessageW(m_chkShadow, WM_SETFONT, (WPARAM)hFont, TRUE);
    curY += rowH;
    
    // 4. Ink Color
    createLabel(L"Ink Color:", curY);
    m_comboInkColor = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    ctrlX, curY, ctrlW, 200, m_hwnd, (HMENU)IDC_COMBO_INK_COLOR, m_hInstance, NULL);
    SendMessageW(m_comboInkColor, WM_SETFONT, (WPARAM)hFont, TRUE);
    for (const auto& c : GetAllInkChoices()) {
        SendMessageW(m_comboInkColor, CB_ADDSTRING, 0, (LPARAM)c.title);
    }
    curY += rowH;
    
    // 5. Ink Fade
    createLabel(L"Ink Fade:", curY);
    m_comboInkFade = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                   ctrlX, curY, ctrlW, 150, m_hwnd, (HMENU)IDC_COMBO_INK_FADE, m_hInstance, NULL);
    SendMessageW(m_comboInkFade, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(m_comboInkFade, CB_ADDSTRING, 0, (LPARAM)L"3 seconds (Default)");
    SendMessageW(m_comboInkFade, CB_ADDSTRING, 0, (LPARAM)L"5 seconds");
    SendMessageW(m_comboInkFade, CB_ADDSTRING, 0, (LPARAM)L"10 seconds");
    SendMessageW(m_comboInkFade, CB_ADDSTRING, 0, (LPARAM)L"Never fade");
    curY += rowH;
    
    // 6. Tap Sound Checkbox
    m_chkSound = CreateWindowW(L"BUTTON", L"Enable tap sound", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                               ctrlX, curY, ctrlW, 24, m_hwnd, (HMENU)IDC_CHK_SOUND, m_hInstance, NULL);
    SendMessageW(m_chkSound, WM_SETFONT, (WPARAM)hFont, TRUE);
    curY += rowH;
    
    // 7. Tap Sound Choice + Test Button
    createLabel(L"Sound Effect:", curY);
    m_comboTapSound = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    ctrlX, curY, 160, 200, m_hwnd, (HMENU)IDC_COMBO_TAP_SOUND, m_hInstance, NULL);
    SendMessageW(m_comboTapSound, WM_SETFONT, (WPARAM)hFont, TRUE);
    for (const auto& ts : GetAllTapSounds()) {
        SendMessageW(m_comboTapSound, CB_ADDSTRING, 0, (LPARAM)ts.title);
    }
    
    m_btnTestSound = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   ctrlX + 170, curY - 2, 80, 26, m_hwnd, (HMENU)IDC_BTN_TEST_SOUND, m_hInstance, NULL);
    SendMessageW(m_btnTestSound, WM_SETFONT, (WPARAM)hFont, TRUE);
    curY += rowH;
    
    // 8. Launch at Startup
    m_chkLaunchAtStartup = CreateWindowW(L"BUTTON", L"Launch TinyHand when Windows starts", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         labelX, curY, 390, 24, m_hwnd, (HMENU)IDC_CHK_STARTUP, m_hInstance, NULL);
    SendMessageW(m_chkLaunchAtStartup, WM_SETFONT, (WPARAM)hFont, TRUE);
    curY += rowH + 6;
    
    // 9. Shortcuts summary text
    HWND txt = CreateWindowW(L"STATIC",
        L"Shortcuts:\r\n  Ctrl+Alt+H : Turn pointer on/off\r\n  Ctrl+Alt+X : Clear drawing\r\n  Esc        : Exit pointer\r\n  Alt (hold) : Click through to screen underneath",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        labelX, curY, 410, 75, m_hwnd, NULL, m_hInstance, NULL);
    SendMessageW(txt, WM_SETFONT, (WPARAM)hFont, TRUE);
    curY += 85;
    
    // 10. Save and Cancel Buttons
    m_btnSave = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                              250, curY, 80, 28, m_hwnd, (HMENU)IDC_BTN_SAVE, m_hInstance, NULL);
    SendMessageW(m_btnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    m_btnCancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                340, curY, 80, 28, m_hwnd, (HMENU)IDC_BTN_CANCEL, m_hInstance, NULL);
    SendMessageW(m_btnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
}

void SettingsWindow::LoadSettingsIntoUI(const Settings& s) {
    m_currentSettings = s;
    
    const auto& styles = GetAllStyles();
    for (size_t i = 0; i < styles.size(); ++i) {
        if (styles[i].id == s.currentStyle) {
            SendMessageW(m_comboStyle, CB_SETCURSEL, (WPARAM)i, 0);
            break;
        }
    }
    
    int sizeVal = (int)round(s.pointerSize * 100.0);
    SendMessageW(m_sliderSize, TBM_SETPOS, TRUE, sizeVal);
    
    SendMessageW(m_chkShadow, BM_SETCHECK, s.showShadow ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_comboInkColor, CB_SETCURSEL, (WPARAM)s.inkChoice, 0);
    
    int fadeIdx = 0;
    if (s.fadeSeconds <= 0.0) fadeIdx = 3;
    else if (s.fadeSeconds >= 9.0) fadeIdx = 2;
    else if (s.fadeSeconds >= 4.0) fadeIdx = 1;
    SendMessageW(m_comboInkFade, CB_SETCURSEL, (WPARAM)fadeIdx, 0);
    
    SendMessageW(m_chkSound, BM_SETCHECK, s.soundEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    
    const auto& sounds = GetAllTapSounds();
    int selSoundIdx = 0;
    for (size_t i = 0; i < sounds.size(); ++i) {
        if (sounds[i].id == s.tapSound) {
            selSoundIdx = (int)i;
            break;
        }
    }
    SendMessageW(m_comboTapSound, CB_SETCURSEL, (WPARAM)selSoundIdx, 0);
    
    SendMessageW(m_chkLaunchAtStartup, BM_SETCHECK, s.launchAtLogin ? BST_CHECKED : BST_UNCHECKED, 0);
}

Settings SettingsWindow::ReadSettingsFromUI() {
    Settings s = m_currentSettings;
    
    int styleIdx = (int)SendMessageW(m_comboStyle, CB_GETCURSEL, 0, 0);
    const auto& styles = GetAllStyles();
    if (styleIdx >= 0 && styleIdx < (int)styles.size()) {
        s.currentStyle = styles[styleIdx].id;
    }
    
    int sizeVal = (int)SendMessageW(m_sliderSize, TBM_GETPOS, 0, 0);
    s.pointerSize = (double)sizeVal / 100.0;
    
    s.showShadow = (SendMessageW(m_chkShadow, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    int inkIdx = (int)SendMessageW(m_comboInkColor, CB_GETCURSEL, 0, 0);
    if (inkIdx >= 0 && inkIdx <= 8) s.inkChoice = (InkChoiceId)inkIdx;
    
    int fadeIdx = (int)SendMessageW(m_comboInkFade, CB_GETCURSEL, 0, 0);
    if (fadeIdx == 0) s.fadeSeconds = 3.0;
    else if (fadeIdx == 1) s.fadeSeconds = 5.0;
    else if (fadeIdx == 2) s.fadeSeconds = 10.0;
    else if (fadeIdx == 3) s.fadeSeconds = 0.0;
    
    s.soundEnabled = (SendMessageW(m_chkSound, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    int soundIdx = (int)SendMessageW(m_comboTapSound, CB_GETCURSEL, 0, 0);
    const auto& sounds = GetAllTapSounds();
    if (soundIdx >= 0 && soundIdx < (int)sounds.size()) {
        s.tapSound = sounds[soundIdx].id;
    }
    
    s.launchAtLogin = (SendMessageW(m_chkLaunchAtStartup, BM_GETCHECK, 0, 0) == BST_CHECKED);
    return s;
}

void SettingsWindow::Show(const Settings& s) {
    if (!m_hwnd) return;
    LoadSettingsIntoUI(s);
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void SettingsWindow::Hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_TEST_SOUND) {
            int soundIdx = (int)SendMessageW(m_comboTapSound, CB_GETCURSEL, 0, 0);
            const auto& sounds = GetAllTapSounds();
            if (soundIdx >= 0 && soundIdx < (int)sounds.size()) {
                TapSoundPlayer::Instance().Play(sounds[soundIdx].id);
            }
            return 0;
        } else if (id == IDC_BTN_SAVE) {
            Settings s = ReadSettingsFromUI();
            if (onSettingsSaved) onSettingsSaved(s);
            Hide();
            return 0;
        } else if (id == IDC_BTN_CANCEL) {
            Hide();
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        Hide();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace TinyHand
