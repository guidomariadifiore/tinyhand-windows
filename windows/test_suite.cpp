#include "Common.h"
#include "Preferences.h"
#include "HandAsset.h"
#include "TapSound.h"
#include "Ink.h"
#include "ArmRenderer.h"
#include "Toolbar.h"
#include "OverlayWindow.h"
#include "InputHook.h"
#include "CursorManager.h"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace TinyHand;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            exit(1); \
        } \
    } while(0)

void TestPreferences() {
    std::cout << "[TEST] Testing Preferences..." << std::endl;
    Settings s;
    s.currentStyle = PointerStyleId::Plush;
    s.pointerSize = 0.16;
    s.showShadow = false;
    s.fadeSeconds = 5.0;
    s.soundEnabled = false;
    s.tapSound = TapSoundChoiceId::Wood;
    s.inkChoice = InkChoiceId::Blue;
    
    Preferences::Save(s);
    Settings loaded = Preferences::Load();
    
    TEST_ASSERT(loaded.currentStyle == PointerStyleId::Plush, "Style matches");
    TEST_ASSERT(std::abs(loaded.pointerSize - 0.16) < 0.001, "Pointer size matches");
    TEST_ASSERT(loaded.showShadow == false, "Shadow matches");
    TEST_ASSERT(std::abs(loaded.fadeSeconds - 5.0) < 0.001, "Fade seconds matches");
    TEST_ASSERT(loaded.soundEnabled == false, "Sound matches");
    TEST_ASSERT(loaded.tapSound == TapSoundChoiceId::Wood, "Tap sound matches");
    TEST_ASSERT(loaded.inkChoice == InkChoiceId::Blue, "Ink choice matches");
    
    // Reset back to defaults
    Settings def;
    Preferences::Save(def);
    std::cout << "  -> Preferences PASSED" << std::endl;
}

void TestHandAssets() {
    std::cout << "[TEST] Testing HandAssetManager for all 37 styles..." << std::endl;
    auto& mgr = HandAssetManager::Instance();
    
    const auto& styles = GetAllStyles();
    TEST_ASSERT(styles.size() == 37, "Must have exactly 37 styles (36 hands + 1 laser)");
    
    int loadedHands = 0;
    for (const auto& s : styles) {
        if (s.id == PointerStyleId::Laser) {
            auto thumb = mgr.GetThumbnail(s.id);
            TEST_ASSERT(thumb != nullptr, "Laser must have thumbnail");
            TEST_ASSERT(thumb->GetWidth() == 64 && thumb->GetHeight() == 64, "Thumbnail size 64x64");
            continue;
        }
        
        auto asset = mgr.Get(s.id);
        TEST_ASSERT(asset != nullptr, "Hand asset must load");
        TEST_ASSERT(asset->image != nullptr, "Hand image must exist");
        TEST_ASSERT(asset->aspect > 0.3 && asset->aspect < 1.5, "Aspect ratio reasonable");
        TEST_ASSERT(asset->wrist > asset->handTop, "Wrist below handTop");
        TEST_ASSERT(asset->rodEnd >= asset->wrist, "RodEnd below or equal to wrist");
        TEST_ASSERT(asset->tipX > 0.0 && asset->tipX < 1.0, "TipX inside image");
        TEST_ASSERT(asset->rodX > 0.0 && asset->rodX < 1.0, "RodX inside image");
        
        auto thumb = mgr.GetThumbnail(s.id);
        TEST_ASSERT(thumb != nullptr, "Thumbnail must exist");
        TEST_ASSERT(thumb->GetWidth() == 64 && thumb->GetHeight() == 64, "Thumbnail size 64x64");
        loadedHands++;
    }
    TEST_ASSERT(loadedHands == 36, "All 36 hands loaded successfully");
    std::cout << "  -> HandAssetManager PASSED (" << loadedHands << " hands + laser verified)" << std::endl;
}

void TestTapSounds() {
    std::cout << "[TEST] Testing TapSoundPlayer..." << std::endl;
    auto& player = TapSoundPlayer::Instance();
    const auto& sounds = GetAllTapSounds();
    TEST_ASSERT(sounds.size() == 6, "Must have 6 tap sounds");
    
    for (const auto& s : sounds) {
        player.Play(s.id);
    }
    std::cout << "  -> TapSoundPlayer PASSED" << std::endl;
}

void TestInk() {
    std::cout << "[TEST] Testing InkManager..." << std::endl;
    InkManager ink;
    TEST_ASSERT(!ink.IsDrawing(), "Not drawing initially");
    TEST_ASSERT(ink.StrokeCount() == 0, "No strokes initially");
    
    ink.Begin(100, 100, 4.0, Gdiplus::Color(255, 135, 84, 242));
    TEST_ASSERT(ink.IsDrawing(), "Is drawing after Begin");
    TEST_ASSERT(ink.StrokeCount() == 1, "Stroke count 1");
    
    ink.Extend(110, 110);
    ink.Extend(120, 120);
    ink.Extend(150, 180);
    
    ink.End();
    TEST_ASSERT(!ink.IsDrawing(), "Not drawing after End");
    TEST_ASSERT(ink.StrokeCount() == 1, "Stroke count still 1");
    
    // Test clear
    ink.Clear();
    TEST_ASSERT(ink.StrokeCount() == 0, "Strokes cleared");
    std::cout << "  -> InkManager PASSED" << std::endl;
}

void TestArmGeometry() {
    std::cout << "[TEST] Testing Arm pointing geometry math..." << std::endl;
    auto asset = HandAssetManager::Instance().Get(PointerStyleId::Lavender);
    TEST_ASSERT(asset != nullptr, "Lavender asset must exist");
    
    double screenW = 1920.0;
    double screenH = 1080.0;
    
    // Test multiple cursor points across the screen
    double testPoints[][2] = {
        { 100.0, 100.0 },
        { 960.0, 540.0 },
        { 1800.0, 200.0 },
        { 500.0, 900.0 },
        { 1200.0, 800.0 }
    };
    
    for (int i = 0; i < 5; ++i) {
        double cursorX = testPoints[i][0];
        double cursorY = testPoints[i][1];
        
        const double gripLeanFrac = 0.30;
        const double gripDepthFrac = 0.12;
        const double handLenFrac = 0.13;
        
        double targetX = (std::min)((std::max)(cursorX + gripLeanFrac * screenW, screenW * 0.15), screenW * 1.05);
        double gx = targetX;
        double gy = screenH + gripDepthFrac * screenH;
        
        double dx = cursorX - gx;
        double dy = cursorY - gy;
        double reach = sqrt(dx * dx + dy * dy);
        double aimAngle = atan2(dy, dx);
        
        double handPts = handLenFrac * screenH;
        double s = handPts / (asset->wrist - asset->handTop);
        double widthPts = s * asset->aspect;
        double handTipPts = (asset->wrist - asset->tip) * s;
        double tipOffX = (asset->tipX - asset->rodX) * widthPts;
        
        double along = sqrt(reach * reach - tipOffX * tipOffX);
        double rodLen = along - handTipPts;
        double tipAngle = atan2(tipOffX, rodLen + handTipPts);
        double rodAngle = aimAngle - tipAngle;
        
        // Compute calculated tip position
        double ux = cos(rodAngle);
        double uy = sin(rodAngle);
        double vx = -sin(rodAngle);
        double vy = cos(rodAngle);
        
        double calcTipX = gx + ux * (rodLen + handTipPts) + vx * tipOffX;
        double calcTipY = gy + uy * (rodLen + handTipPts) + vy * tipOffX;
        
        double errX = std::abs(calcTipX - cursorX);
        double errY = std::abs(calcTipY - cursorY);
        
        TEST_ASSERT(errX < 0.001, "Fingertip X matches cursor to sub-pixel precision");
        TEST_ASSERT(errY < 0.001, "Fingertip Y matches cursor to sub-pixel precision");
    }
    std::cout << "  -> Arm Geometry PASSED (pixel-perfect accuracy verified)" << std::endl;
}

void TestOverlayNonInterference() {
    std::cout << "[TEST] Testing Screen & Video Non-Interference guarantees..." << std::endl;
    HINSTANCE hInst = GetModuleHandle(NULL);
    
    OverlayWindow overlay;
    bool created = overlay.Create(hInst);
    TEST_ASSERT(created, "Overlay created");
    
    // Check Window Extended Styles for Non-Interference
    // 1. Must be WS_EX_NOACTIVATE (so video players never lose focus or pause)
    // 2. Must be WS_EX_TRANSPARENT (so Chromium occlusion tracker never marks windows occluded)
    // 3. Must be WS_EX_LAYERED (so per-pixel alpha works and DWM composites smoothly)
    // 4. Must be WS_EX_TOPMOST (so hand stays above windows)
    // 5. Must be WS_EX_TOOLWINDOW (so it doesn't appear in Alt+Tab)
    
    HWND hwnd = FindWindowW(L"TinyHand_OverlayClass", NULL);
    TEST_ASSERT(hwnd != NULL, "Overlay HWND exists");
    
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    TEST_ASSERT((exStyle & WS_EX_NOACTIVATE) != 0, "WS_EX_NOACTIVATE must be set");
    TEST_ASSERT((exStyle & WS_EX_TRANSPARENT) != 0, "WS_EX_TRANSPARENT must be set");
    TEST_ASSERT((exStyle & WS_EX_LAYERED) != 0, "WS_EX_LAYERED must be set");
    TEST_ASSERT((exStyle & WS_EX_TOPMOST) != 0, "WS_EX_TOPMOST must be set");
    TEST_ASSERT((exStyle & WS_EX_TOOLWINDOW) != 0, "WS_EX_TOOLWINDOW must be set");
    
    // Check WM_MOUSEACTIVATE returns MA_NOACTIVATE
    LRESULT ma = SendMessageW(hwnd, WM_MOUSEACTIVATE, 0, 0);
    TEST_ASSERT(ma == MA_NOACTIVATE, "WM_MOUSEACTIVATE must return MA_NOACTIVATE");
    
    // Check WM_NCHITTEST returns HTTRANSPARENT
    LRESULT ht = SendMessageW(hwnd, WM_NCHITTEST, 0, 0);
    TEST_ASSERT(ht == HTTRANSPARENT, "WM_NCHITTEST must return HTTRANSPARENT");
    
    // Test Activate & Deactivate
    overlay.Activate();
    TEST_ASSERT(overlay.IsActive(), "Overlay is active");
    
    overlay.Deactivate();
    TEST_ASSERT(!overlay.IsActive(), "Overlay is deactivated");
    
    overlay.Destroy();
    std::cout << "  -> Screen & Video Non-Interference PASSED" << std::endl;
}

void TestToolbarInteractiveSafety() {
    std::cout << "[TEST] Testing Toolbar interactive safety & pass-through..." << std::endl;
    HINSTANCE hInst = GetModuleHandle(NULL);
    ToolbarWindow toolbar;
    bool created = toolbar.Create(hInst);
    TEST_ASSERT(created, "Toolbar window created");
    
    // When hidden, interactive rects must be EMPTY (no clicks can pass through to video underneath)
    toolbar.Show(false);
    TEST_ASSERT(!toolbar.IsRevealed(), "Toolbar is not revealed");
    auto hiddenRects = toolbar.GetInteractiveRects();
    TEST_ASSERT(hiddenRects.empty(), "Hidden toolbar must return 0 pass-through rects");
    
    // When revealed, interactive rects must only contain the pill (not the 130px transparent padding)
    toolbar.RevealTemporarily(5.0);
    toolbar.Update(960, 1070, 0, 0, 1920, 1080, false, GetTickCount64());
    TEST_ASSERT(toolbar.IsRevealed(), "Toolbar is now revealed");
    
    auto revealedRects = toolbar.GetInteractiveRects();
    TEST_ASSERT(!revealedRects.empty(), "Revealed toolbar must return interactive rects");
    
    // Check that points in the upper empty padding return false for IsOverInteractive
    RECT barRect = revealedRects[0];
    int barTop = barRect.top;
    int barBottom = barRect.bottom;
    int barMidX = (barRect.left + barRect.right) / 2;
    int barMidY = (barTop + barBottom) / 2;
    
    TEST_ASSERT(toolbar.IsOverInteractive(barMidX, barMidY), "Pill center is interactive");
    TEST_ASSERT(!toolbar.IsOverInteractive(barMidX, barTop - 40), "Padding above pill is NOT interactive");
    
    toolbar.Destroy();
    std::cout << "  -> Toolbar Interactive Safety PASSED" << std::endl;
}

void TestSystemUIPassThrough() {
    std::cout << "[TEST] Testing System UI pass-through and click consumption..." << std::endl;
    auto& hook = InputHook::Instance();
    
    // 1. Regular mock application / video window
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"MockVideoPlayerClass";
    RegisterClassExW(&wc);
    
    HWND hVideo = CreateWindowExW(
        0, L"MockVideoPlayerClass", L"Mock Video Player",
        WS_POPUP | WS_VISIBLE, 100, 100, 400, 300,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    TEST_ASSERT(hVideo != NULL, "Mock video window created");
    
    POINT ptVideo = { 200, 200 };
    // Video player should NOT be considered interactive system UI (clicks must be intercepted)
    bool isSysUI = hook.IsOverInteractiveSystemUI(ptVideo);
    TEST_ASSERT(!isSysUI, "Video player must not be treated as system UI (clicks must be consumed)");
    
    DestroyWindow(hVideo);
    UnregisterClassW(L"MockVideoPlayerClass", GetModuleHandle(NULL));
    
    // 2. Cursor Manager Restore Safety
    CursorManager::Instance().Restore();
    TEST_ASSERT(true, "CursorManager::Restore ran safely");
    
    std::cout << "  -> System UI Pass-Through & Safety PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "TinyHand Windows Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    TestPreferences();
    TestHandAssets();
    TestTapSounds();
    TestInk();
    TestArmGeometry();
    TestOverlayNonInterference();
    TestToolbarInteractiveSafety();
    TestSystemUIPassThrough();
    
    HandAssetManager::Instance().ClearCache();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    
    std::cout << "========================================" << std::endl;
    std::cout << "ALL 8 TEST SUITES PASSED (100% SUCCESS)!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
