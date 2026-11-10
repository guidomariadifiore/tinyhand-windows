#pragma once

#include "Common.h"

namespace TinyHand {

class Preferences {
public:
    static Settings Load();
    static void Save(const Settings& settings);
    static void SetLaunchAtLogin(bool enable);
    static bool GetLaunchAtLogin();

private:
    static std::wstring GetConfigPath();
};

} // namespace TinyHand
