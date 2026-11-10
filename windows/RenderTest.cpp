#include "RenderTest.h"
#include "Common.h"
#include "HandAsset.h"
#include "ArmRenderer.h"
#include "Ink.h"
#include <vector>
#include <cmath>

namespace TinyHand {

static int GetEncoderClsidHelper(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    std::vector<BYTE> memory(size);
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)memory.data();
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return j;
        }
    }
    return -1;
}

bool RenderTest::Run(const std::wstring& outPath) {
    const auto& styles = GetAllStyles();
    wprintf(L"RenderTest: starting for %zu styles...\n", styles.size());
    const int panelW = 420;
    const int panelH = 560;
    const int totalW = panelW * (int)styles.size();
    const int totalH = panelH;
    
    // Windows coords: macOS 380 from bottom of 560 is 180 from top
    const double targetX = panelW - 230.0; // 190.0
    const double targetY = 180.0;
    
    auto fullBitmap = std::make_unique<Gdiplus::Bitmap>(totalW, totalH, PixelFormat32bppARGB);
    if (fullBitmap->GetLastStatus() != Gdiplus::Ok) {
        wprintf(L"RenderTest: failed to allocate bitmap %dx%d status=%d\n", totalW, totalH, (int)fullBitmap->GetLastStatus());
        return false;
    }
    Gdiplus::Graphics g(fullBitmap.get());
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    
    for (size_t i = 0; i < styles.size(); ++i) {
        PointerStyleId style = styles[i].id;
        float panelOffsetX = (float)(i * panelW);
        
        Gdiplus::GraphicsState state = g.Save();
        g.TranslateTransform(panelOffsetX, 0.0f);
        
        // Panel background
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 237, 240, 235));
        g.FillRectangle(&bg, 0, 0, panelW, panelH);
        
        // Ink Spiral
        InkManager ink;
        double inkW = 4.0;
        Gdiplus::Color inkColor = styles[i].inkColor;
        
        double centerX = targetX + 40.0;
        double centerY = targetY + 8.0;
        double a0 = atan2(targetY - centerY, targetX - centerX);
        
        for (int k = 0; k <= 60; ++k) {
            double a = a0 + (double)k / 60.0 * 2.0 * 3.14159265358979323846;
            double px = centerX + 40.0 * cos(a);
            double py = centerY + 32.0 * sin(a);
            if (k == 0) {
                ink.Begin(px, py, inkW, inkColor);
            } else {
                ink.Extend(px, py);
            }
        }
        ink.Extend(targetX, targetY);
        ink.Render(g, 0.0, GetTickCount64());
        
        // Arm
        ArmRenderer arm;
        arm.Press(); // held
        arm.RenderArm(g, style, targetX, targetY, panelW, panelH, true, 0.13);
        
        // Target marker dot (yellow)
        if (style != PointerStyleId::Laser) {
            Gdiplus::SolidBrush marker(Gdiplus::Color(255, 242, 204, 25));
            g.FillEllipse(&marker, (float)(targetX - 3.0), (float)(targetY - 3.0), 6.0f, 6.0f);
        }
        
        g.Restore(state);
    }
    
    CLSID pngClsid;
    if (GetEncoderClsidHelper(L"image/png", &pngClsid) < 0) {
        wprintf(L"RenderTest: failed to find PNG encoder\n");
        return false;
    }
    
    Gdiplus::Status s = fullBitmap->Save(outPath.c_str(), &pngClsid, NULL);
    wprintf(L"RenderTest: save to %s status=%d\n", outPath.c_str(), (int)s);
    return (s == Gdiplus::Ok);
}

} // namespace TinyHand
