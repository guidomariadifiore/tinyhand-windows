#include "HandAsset.h"
#include <algorithm>
#include <cmath>

namespace TinyHand {

HandAssetManager& HandAssetManager::Instance() {
    static HandAssetManager instance;
    return instance;
}

HandAssetManager::HandAssetManager() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring p = path;
    size_t lastSlash = p.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        m_exeDir = p.substr(0, lastSlash);
    }
}

std::wstring HandAssetManager::FindAssetPath(const std::wstring& filename) {
    std::vector<std::wstring> candidates = {
        L"assets\\" + filename,
        m_exeDir + L"\\assets\\" + filename,
        m_exeDir + L"\\..\\assets\\" + filename,
        m_exeDir + L"\\" + filename,
        L"..\\assets\\" + filename
    };
    
    for (const auto& c : candidates) {
        DWORD attr = GetFileAttributesW(c.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return c;
        }
    }
    return L"";
}

std::shared_ptr<HandAsset> HandAssetManager::Get(PointerStyleId id) {
    if (id == PointerStyleId::Laser) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cache.find(id);
    if (it != m_cache.end()) {
        return it->second;
    }
    
    const StyleInfo* si = GetStyleInfo(id);
    if (!si) return nullptr;
    
    std::wstring filename = std::wstring(L"hand_") + si->rawName + L".png";
    auto asset = LoadAndMeasure(filename);
    if (asset) {
        m_cache[id] = asset;
    }
    return asset;
}

std::shared_ptr<Gdiplus::Bitmap> HandAssetManager::GetThumbnail(PointerStyleId id) {
    if (id == PointerStyleId::Laser) {
        // Generate a 64x64 laser dot icon
        auto bmp = std::make_shared<Gdiplus::Bitmap>(64, 64, PixelFormat32bppARGB);
        Gdiplus::Graphics g(bmp.get());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        
        // Outer glow
        Gdiplus::SolidBrush glow(Gdiplus::Color(100, 255, 41, 31));
        g.FillEllipse(&glow, 16.0f, 16.0f, 32.0f, 32.0f);
        // Core red
        Gdiplus::SolidBrush red(Gdiplus::Color(255, 255, 41, 31));
        g.FillEllipse(&red, 22.0f, 22.0f, 20.0f, 20.0f);
        // Bright center
        Gdiplus::SolidBrush center(Gdiplus::Color(245, 255, 219, 209));
        g.FillEllipse(&center, 28.0f, 28.0f, 8.0f, 8.0f);
        return bmp;
    }
    
    auto asset = Get(id);
    if (asset) {
        return asset->thumbnail;
    }
    return nullptr;
}

std::shared_ptr<HandAsset> HandAssetManager::LoadAndMeasure(const std::wstring& filename) {
    std::wstring path = FindAssetPath(filename);
    if (path.empty()) return nullptr;
    
    auto bmp = std::make_shared<Gdiplus::Bitmap>(path.c_str());
    if (bmp->GetLastStatus() != Gdiplus::Ok) {
        return nullptr;
    }
    
    int w = bmp->GetWidth();
    int h = bmp->GetHeight();
    if (w <= 0 || h <= 0) return nullptr;
    
    Gdiplus::BitmapData bmpData;
    Gdiplus::Rect rect(0, 0, w, h);
    if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bmpData) != Gdiplus::Ok) {
        return nullptr;
    }
    
    std::vector<BYTE> alpha(w * h, 0);
    BYTE* scan0 = (BYTE*)bmpData.Scan0;
    int stride = bmpData.Stride;
    
    for (int y = 0; y < h; ++y) {
        BYTE* row = scan0 + y * stride;
        for (int x = 0; x < w; ++x) {
            alpha[y * w + x] = row[x * 4 + 3];
        }
    }
    bmp->UnlockBits(&bmpData);
    
    auto count = [&](int y) -> int {
        int n = 0;
        int row = y * w;
        for (int x = 0; x < w; ++x) {
            if (alpha[row + x] > 128) n++;
        }
        return n;
    };
    
    auto center = [&](int y) -> double {
        int row = y * w;
        int lo = -1, hi = -1;
        for (int x = 0; x < w; ++x) {
            if (alpha[row + x] > 128) {
                if (lo < 0) lo = x;
                hi = x;
            }
        }
        return lo < 0 ? 0.5 : (double)(lo + hi) / 2.0 / (double)w;
    };
    
    std::vector<int> counts(h);
    for (int y = 0; y < h; ++y) counts[y] = count(y);
    
    int top = -1;
    for (int y = 0; y < h; ++y) {
        if (counts[y] > 0) { top = y; break; }
    }
    if (top < 0) return nullptr;
    
    int searchEnd = (std::min)(h, top + (int)(0.45 * (double)h));
    int maxW = 0;
    for (int y = top; y < searchEnd; ++y) {
        if (counts[y] > maxW) maxW = counts[y];
    }
    if (maxW <= 0) return nullptr;
    
    int fist = -1;
    for (int y = top; y < h; ++y) {
        if ((double)counts[y] > 0.6 * (double)maxW) { fist = y; break; }
    }
    if (fist < 0) return nullptr;
    
    int wrist = -1;
    for (int y = fist; y < h; ++y) {
        if ((double)counts[y] < 0.35 * (double)maxW) { wrist = y; break; }
    }
    if (wrist < 0) return nullptr;
    
    int rodW = counts[(std::min)(h - 1, wrist + h / 50)];
    int lastRow = h - 1;
    for (int y = h - 1; y >= 0; --y) {
        if (counts[y] > 0) { lastRow = y; break; }
    }
    
    int from = (std::min)(h - 1, wrist + h / 33);
    int handle = -1;
    for (int y = from; y < h; ++y) {
        if ((double)counts[y] > 1.8 * (double)rodW) { handle = y; break; }
    }
    int rodEnd = (handle >= 0) ? handle : lastRow;
    
    int minX = w, maxX = 0;
    for (int y = top; y < wrist; ++y) {
        int row = y * w;
        for (int x = 0; x < w; ++x) {
            if (alpha[row + x] > 16) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
            }
        }
    }
    
    int rodMin = w, rodMax = 0;
    for (int y = wrist; y <= rodEnd; ++y) {
        int row = y * w;
        for (int x = 0; x < w; ++x) {
            if (alpha[row + x] > 16) {
                if (x < rodMin) rodMin = x;
                if (x > rodMax) rodMax = x;
            }
        }
    }
    if (rodMin >= rodMax) {
        int cx = (int)(center((wrist + rodEnd) / 2) * w);
        int halfW = (std::max)(15, rodW / 2 + 5);
        rodMin = (std::max)(0, cx - halfW);
        rodMax = (std::min)(w - 1, cx + halfW);
    }
    
    double fh = (double)h;
    auto asset = std::make_shared<HandAsset>();
    asset->image = bmp;
    asset->aspect = (double)w / fh;
    asset->handTop = (std::max)(0.0, (double)top - 0.004 * fh) / fh;
    asset->tip = (double)top / fh;
    asset->wrist = (double)wrist / fh;
    asset->rodEnd = (double)rodEnd / fh;
    asset->tipX = center((std::min)(h - 1, top + h / 200));
    asset->rodX = center((wrist + rodEnd) / 2);
    asset->handMinX = (std::max)(0.0, (double)(minX - 4) / (double)w);
    asset->handMaxX = (std::min)(1.0, (double)(maxX + 4) / (double)w);
    asset->rodMinX = (std::max)(0.0, (double)(rodMin - 4) / (double)w);
    asset->rodMaxX = (std::min)(1.0, (double)(rodMax + 4) / (double)w);
    
    asset->EnsureDisplayImages(512);
    asset->thumbnail = GenerateThumbnail(*asset);
    return asset;
}

void HandAsset::EnsureDisplayImages(int targetH) {
    if (targetH <= 0) targetH = 512;
    if (displayH == targetH && displayImage && displayShadow) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (displayH == targetH && displayImage && displayShadow) return;
    if (!image) return;
    
    int origW = image->GetWidth();
    int origH = image->GetHeight();
    if (origW <= 0 || origH <= 0) return;
    
    int targetW = (int)std::round(targetH * aspect);
    if (targetW <= 0) targetW = (int)std::round(targetH * (double)origW / (double)origH);
    if (targetW <= 0) targetW = 340;
    
    auto newDisplayImage = std::make_shared<Gdiplus::Bitmap>(targetW, targetH, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(newDisplayImage.get());
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.DrawImage(image.get(), 0, 0, targetW, targetH);
    }
    
    auto newDisplayShadow = std::make_shared<Gdiplus::Bitmap>(targetW, targetH, PixelFormat32bppPARGB);
    {
        Gdiplus::BitmapData srcData, dstData;
        Gdiplus::Rect r(0, 0, targetW, targetH);
        if (newDisplayImage->LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &srcData) == Gdiplus::Ok) {
            if (newDisplayShadow->LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &dstData) == Gdiplus::Ok) {
                for (int y = 0; y < targetH; ++y) {
                    const BYTE* srcRow = (const BYTE*)srcData.Scan0 + y * srcData.Stride;
                    BYTE* dstRow = (BYTE*)dstData.Scan0 + y * dstData.Stride;
                    for (int x = 0; x < targetW; ++x) {
                        BYTE a = srcRow[x * 4 + 3];
                        BYTE shadowA = (BYTE)((int)a * 27 / 100);
                        dstRow[x * 4 + 0] = 0;
                        dstRow[x * 4 + 1] = 0;
                        dstRow[x * 4 + 2] = 0;
                        dstRow[x * 4 + 3] = shadowA;
                    }
                }
                newDisplayShadow->UnlockBits(&dstData);
            }
            newDisplayImage->UnlockBits(&srcData);
        }
    }
    
    displayImage = newDisplayImage;
    displayShadow = newDisplayShadow;
    displayW = targetW;
    displayH = targetH;
}

std::shared_ptr<Gdiplus::Bitmap> HandAssetManager::GenerateThumbnail(const HandAsset& asset) {
    if (!asset.image) return nullptr;
    
    int w = asset.image->GetWidth();
    int h = asset.image->GetHeight();
    
    double handW = (asset.handMaxX - asset.handMinX) * w;
    double handH = (asset.wrist - asset.handTop) * h;
    double side = (std::max)(handW, handH) * 1.06;
    double cx = (asset.handMinX + asset.handMaxX) / 2.0 * w;
    double cy = (asset.handTop + asset.wrist) / 2.0 * h;
    
    double srcX = (std::max)(0.0, cx - side / 2.0);
    double srcY = (std::max)(0.0, cy - side / 2.0);
    double srcW = (std::min)((double)w - srcX, side);
    double srcH = (std::min)((double)h - srcY, side);
    
    const int px = 64;
    auto thumb = std::make_shared<Gdiplus::Bitmap>(px, px, PixelFormat32bppARGB);
    Gdiplus::Graphics g(thumb.get());
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    
    g.DrawImage(asset.image.get(), Gdiplus::RectF(0, 0, (float)px, (float)px),
                (float)srcX, (float)srcY, (float)srcW, (float)srcH, Gdiplus::UnitPixel);
    return thumb;
}

void HandAssetManager::PreloadAll() {
    for (const auto& s : GetAllStyles()) {
        if (s.id != PointerStyleId::Laser) {
            Get(s.id);
        }
    }
}

void HandAssetManager::ClearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

} // namespace TinyHand
