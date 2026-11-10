#include "Preferences.h"
#include <shlobj.h>
#include <sstream>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace TinyHand {

std::wstring Preferences::GetConfigPath() {
    WCHAR appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\TinyHand";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\settings.ini";
    }
    return L"settings.ini";
}

Settings Preferences::Load() {
    Settings s;
    std::wstring path = GetConfigPath();
    
    // Style
    WCHAR buf[256];
    GetPrivateProfileStringW(L"TinyHand", L"Style", L"lavender", buf, 256, path.c_str());
    const StyleInfo* si = GetStyleInfo(buf);
    if (si) s.currentStyle = si->id;
    
    // Pinned
    GetPrivateProfileStringW(L"TinyHand", L"Pinned", L"lavender,glove,coral,plush,robot,laser", buf, 256, path.c_str());
    std::wstringstream wss(buf);
    std::wstring item;
    std::vector<PointerStyleId> pinned;
    while (std::getline(wss, item, L',')) {
        const StyleInfo* psi = GetStyleInfo(item);
        if (psi) pinned.push_back(psi->id);
    }
    if (!pinned.empty()) s.pinnedStyles = pinned;
    
    // Pointer size
    GetPrivateProfileStringW(L"TinyHand", L"PointerSize", L"0.13", buf, 256, path.c_str());
    s.pointerSize = _wtof(buf);
    if (s.pointerSize < 0.05 || s.pointerSize > 0.30) s.pointerSize = 0.13;
    
    // Shadow
    s.showShadow = GetPrivateProfileIntW(L"TinyHand", L"ShowShadow", 1, path.c_str()) != 0;
    
    // Fade
    GetPrivateProfileStringW(L"TinyHand", L"FadeSeconds", L"3.0", buf, 256, path.c_str());
    s.fadeSeconds = _wtof(buf);
    
    // Sound
    s.soundEnabled = GetPrivateProfileIntW(L"TinyHand", L"SoundEnabled", 1, path.c_str()) != 0;
    s.laserSound = GetPrivateProfileIntW(L"TinyHand", L"LaserSound", 0, path.c_str()) != 0;
    
    // Tap Sound
    GetPrivateProfileStringW(L"TinyHand", L"TapSound", L"thock", buf, 256, path.c_str());
    for (const auto& ts : GetAllTapSounds()) {
        if (ts.rawName == std::wstring(buf)) {
            s.tapSound = ts.id;
            break;
        }
    }
    
    // Ink choice
    int inkIdx = GetPrivateProfileIntW(L"TinyHand", L"InkChoice", 0, path.c_str());
    if (inkIdx >= 0 && inkIdx <= 8) s.inkChoice = (InkChoiceId)inkIdx;
    
    s.launchAtLogin = GetLaunchAtLogin();
    return s;
}

void Preferences::Save(const Settings& s) {
    std::wstring path = GetConfigPath();
    
    const StyleInfo* si = GetStyleInfo(s.currentStyle);
    if (si) WritePrivateProfileStringW(L"TinyHand", L"Style", si->rawName, path.c_str());
    
    std::wstring pinnedStr;
    for (size_t i = 0; i < s.pinnedStyles.size(); ++i) {
        const StyleInfo* psi = GetStyleInfo(s.pinnedStyles[i]);
        if (psi) {
            if (!pinnedStr.empty()) pinnedStr += L",";
            pinnedStr += psi->rawName;
        }
    }
    WritePrivateProfileStringW(L"TinyHand", L"Pinned", pinnedStr.c_str(), path.c_str());
    
    WCHAR buf[64];
    swprintf_s(buf, L"%.4f", s.pointerSize);
    WritePrivateProfileStringW(L"TinyHand", L"PointerSize", buf, path.c_str());
    
    WritePrivateProfileStringW(L"TinyHand", L"ShowShadow", s.showShadow ? L"1" : L"0", path.c_str());
    
    swprintf_s(buf, L"%.2f", s.fadeSeconds);
    WritePrivateProfileStringW(L"TinyHand", L"FadeSeconds", buf, path.c_str());
    
    WritePrivateProfileStringW(L"TinyHand", L"SoundEnabled", s.soundEnabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"TinyHand", L"LaserSound", s.laserSound ? L"1" : L"0", path.c_str());
    
    for (const auto& ts : GetAllTapSounds()) {
        if (ts.id == s.tapSound) {
            WritePrivateProfileStringW(L"TinyHand", L"TapSound", ts.rawName, path.c_str());
            break;
        }
    }
    
    swprintf_s(buf, L"%d", (int)s.inkChoice);
    WritePrivateProfileStringW(L"TinyHand", L"InkChoice", buf, path.c_str());
    
    SetLaunchAtLogin(s.launchAtLogin);
}

void Preferences::SetLaunchAtLogin(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            RegSetValueExW(hKey, L"TinyHand", 0, REG_SZ, (const BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKey, L"TinyHand");
        }
        RegCloseKey(hKey);
    }
}

bool Preferences::GetLaunchAtLogin() {
    HKEY hKey;
    bool exists = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        exists = (RegQueryValueExW(hKey, L"TinyHand", NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
        RegCloseKey(hKey);
    }
    return exists;
}

} // namespace TinyHand
