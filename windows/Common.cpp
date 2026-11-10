#include "Common.h"

namespace TinyHand {

static const std::vector<StyleInfo> g_styles = {
    { PointerStyleId::Lavender, L"lavender", L"Lavender toy", ColorFromHex(0x8754F2) },
    { PointerStyleId::Glove, L"glove", L"Cursor glove", ColorFromHex(0x8754F2) },
    { PointerStyleId::Coral, L"coral", L"Coral vinyl", ColorFromHex(0xFF291F) },
    { PointerStyleId::Plush, L"plush", L"Plush", ColorFromHex(0x2F86F0) },
    { PointerStyleId::Robot, L"robot", L"Robot", ColorFromHex(0x16A87A) },
    { PointerStyleId::Pink, L"pink", L"Bubblegum", ColorFromHex(0xEE4F98) },
    { PointerStyleId::Blue, L"blue", L"Sky", ColorFromHex(0x2F86F0) },
    { PointerStyleId::Mint, L"mint", L"Mint", ColorFromHex(0x16A87A) },
    { PointerStyleId::Lemon, L"lemon", L"Lemon", ColorFromHex(0xF2B705) },
    { PointerStyleId::Black, L"black", L"Midnight", ColorFromHex(0x1D1D21) },
    { PointerStyleId::Chrome, L"chrome", L"Chrome", ColorFromHex(0x6B7380) },
    { PointerStyleId::Holo, L"holo", L"Holo", ColorFromHex(0x8754F2) },
    { PointerStyleId::Fuzzy, L"fuzzy", L"Fuzzy", ColorFromHex(0x8754F2) },
    { PointerStyleId::Wood, L"wood", L"Wood", ColorFromHex(0x9A5E2C) },
    { PointerStyleId::Marble, L"marble", L"Marble", ColorFromHex(0x6B7380) },
    { PointerStyleId::Terrazzo, L"terrazzo", L"Terrazzo", ColorFromHex(0xF0561D) },
    { PointerStyleId::Glass, L"glass", L"Glass", ColorFromHex(0x1597E5) },
    { PointerStyleId::Jelly, L"jelly", L"Jelly", ColorFromHex(0xA33FE6) },
    { PointerStyleId::Pastel, L"pastel", L"Pastel", ColorFromHex(0xEE4F98) },
    { PointerStyleId::Pixel, L"pixel", L"Pixel", ColorFromHex(0x1D1D21) },
    { PointerStyleId::Lowpoly, L"lowpoly", L"Low poly", ColorFromHex(0x8754F2) },
    { PointerStyleId::Knit, L"knit", L"Knit", ColorFromHex(0x8754F2) },
    { PointerStyleId::Skeleton, L"skeleton", L"Skeleton", ColorFromHex(0x9A5E2C) },
    { PointerStyleId::Astro, L"astro", L"Astronaut", ColorFromHex(0x2F86F0) },
    { PointerStyleId::Space, L"space", L"Space", ColorFromHex(0xFF291F) },
    { PointerStyleId::Monster, L"monster", L"Monster", ColorFromHex(0x3E9E2E) },
    { PointerStyleId::Water, L"water", L"Water", ColorFromHex(0x1597E5) },
    { PointerStyleId::Lava, L"lava", L"Lava", ColorFromHex(0xF0561D) },
    { PointerStyleId::Grass, L"grass", L"Grass", ColorFromHex(0x3E9E2E) },
    { PointerStyleId::Cactus, L"cactus", L"Cactus", ColorFromHex(0x3E9E2E) },
    { PointerStyleId::Choco, L"choco", L"Chocolate", ColorFromHex(0x9A5E2C) },
    { PointerStyleId::Cloud, L"cloud", L"Cloud", ColorFromHex(0x2F86F0) },
    { PointerStyleId::Mech, L"mech", L"Mech", ColorFromHex(0xF0561D) },
    { PointerStyleId::Neon, L"neon", L"Neon", ColorFromHex(0xE0309A) },
    { PointerStyleId::Leaf, L"leaf", L"Leaf", ColorFromHex(0x3E9E2E) },
    { PointerStyleId::Cookie, L"cookie", L"Gingerbread", ColorFromHex(0x9A5E2C) },
    { PointerStyleId::Laser, L"laser", L"Laser dot", ColorFromHex(0xFF291F) }
};

const std::vector<StyleInfo>& GetAllStyles() {
    return g_styles;
}

const StyleInfo* GetStyleInfo(PointerStyleId id) {
    for (const auto& s : g_styles) {
        if (s.id == id) return &s;
    }
    return &g_styles[0];
}

const StyleInfo* GetStyleInfo(const std::wstring& rawName) {
    for (const auto& s : g_styles) {
        if (s.rawName == rawName) return &s;
    }
    return nullptr;
}

static const std::vector<InkChoiceInfo> g_inkChoices = {
    { InkChoiceId::Match, L"Match the pointer", Gdiplus::Color(0, 0, 0, 0) },
    { InkChoiceId::Red, L"Red", ColorFromHex(0xFF291F) },
    { InkChoiceId::Orange, L"Orange", ColorFromHex(0xFF8A00) },
    { InkChoiceId::Yellow, L"Yellow", ColorFromHex(0xFFCC00) },
    { InkChoiceId::Green, L"Green", ColorFromHex(0x30C25A) },
    { InkChoiceId::Blue, L"Blue", ColorFromHex(0x0A84FF) },
    { InkChoiceId::Purple, L"Purple", ColorFromHex(0x8754F2) },
    { InkChoiceId::White, L"White", ColorFromHex(0xFFFFFF) },
    { InkChoiceId::Black, L"Black", ColorFromHex(0x1D1D21) }
};

const std::vector<InkChoiceInfo>& GetAllInkChoices() {
    return g_inkChoices;
}

Gdiplus::Color ResolveInkColor(InkChoiceId choice, PointerStyleId style) {
    if (choice == InkChoiceId::Match) {
        return GetStyleInfo(style)->inkColor;
    }
    for (const auto& c : g_inkChoices) {
        if (c.id == choice) return c.color;
    }
    return GetStyleInfo(style)->inkColor;
}

static const std::vector<TapSoundInfo> g_tapSounds = {
    { TapSoundChoiceId::Thock, L"thock", L"Thock" },
    { TapSoundChoiceId::Click, L"click", L"Click" },
    { TapSoundChoiceId::Pop, L"pop", L"Pop" },
    { TapSoundChoiceId::Wood, L"wood", L"Wood block" },
    { TapSoundChoiceId::Boop, L"boop", L"Boop" },
    { TapSoundChoiceId::Squeak, L"squeak", L"Squeak" }
};

const std::vector<TapSoundInfo>& GetAllTapSounds() {
    return g_tapSounds;
}

} // namespace TinyHand
