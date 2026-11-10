#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// Forward declarations
struct HandAsset;

namespace TinyHand {

enum class PointerStyleId {
    Lavender,
    Glove,
    Coral,
    Plush,
    Robot,
    Pink,
    Blue,
    Mint,
    Lemon,
    Black,
    Chrome,
    Holo,
    Fuzzy,
    Wood,
    Marble,
    Terrazzo,
    Glass,
    Jelly,
    Pastel,
    Pixel,
    Lowpoly,
    Knit,
    Skeleton,
    Astro,
    Space,
    Monster,
    Water,
    Lava,
    Grass,
    Cactus,
    Choco,
    Cloud,
    Mech,
    Neon,
    Leaf,
    Cookie,
    Laser
};

struct StyleInfo {
    PointerStyleId id;
    const wchar_t* rawName;
    const wchar_t* title;
    Gdiplus::Color inkColor;
};

const std::vector<StyleInfo>& GetAllStyles();
const StyleInfo* GetStyleInfo(PointerStyleId id);
const StyleInfo* GetStyleInfo(const std::wstring& rawName);

enum class InkChoiceId {
    Match,
    Red,
    Orange,
    Yellow,
    Green,
    Blue,
    Purple,
    White,
    Black
};

struct InkChoiceInfo {
    InkChoiceId id;
    const wchar_t* title;
    Gdiplus::Color color; // If Match, color is transparent
};

const std::vector<InkChoiceInfo>& GetAllInkChoices();
Gdiplus::Color ResolveInkColor(InkChoiceId choice, PointerStyleId style);

enum class TapSoundChoiceId {
    Thock,
    Click,
    Pop,
    Wood,
    Boop,
    Squeak
};

struct TapSoundInfo {
    TapSoundChoiceId id;
    const wchar_t* rawName;
    const wchar_t* title;
};

const std::vector<TapSoundInfo>& GetAllTapSounds();

struct Settings {
    PointerStyleId currentStyle = PointerStyleId::Lavender;
    std::vector<PointerStyleId> pinnedStyles = {
        PointerStyleId::Lavender,
        PointerStyleId::Glove,
        PointerStyleId::Coral,
        PointerStyleId::Plush,
        PointerStyleId::Robot,
        PointerStyleId::Laser
    };
    double pointerSize = 0.13; // fraction of screen height: 0.08 to 0.20
    bool showShadow = true;
    double fadeSeconds = 3.0; // <= 0 means never fade
    bool soundEnabled = true;
    bool laserSound = false;
    TapSoundChoiceId tapSound = TapSoundChoiceId::Thock;
    InkChoiceId inkChoice = InkChoiceId::Match;
    bool launchAtLogin = false;
    
    // Shortcuts
    UINT toggleMod = MOD_CONTROL | MOD_ALT;
    UINT toggleVk = 'H';
    UINT clearMod = MOD_CONTROL | MOD_ALT;
    UINT clearVk = 'X';
    UINT interactVk = VK_MENU; // Alt key
};

// Helper color utilities
inline Gdiplus::Color ColorFromHex(uint32_t hex, BYTE alpha = 255) {
    BYTE r = (BYTE)((hex >> 16) & 0xFF);
    BYTE g = (BYTE)((hex >> 8) & 0xFF);
    BYTE b = (BYTE)(hex & 0xFF);
    return Gdiplus::Color(alpha, r, g, b);
}

} // namespace TinyHand
