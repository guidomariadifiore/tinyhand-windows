#pragma once

#include "Common.h"
#include <map>
#include <mutex>

namespace TinyHand {

struct HandAsset {
    std::shared_ptr<Gdiplus::Bitmap> image;
    std::shared_ptr<Gdiplus::Bitmap> thumbnail;
    std::shared_ptr<Gdiplus::Bitmap> displayImage;
    std::shared_ptr<Gdiplus::Bitmap> displayShadow;
    int displayH = 0;
    int displayW = 0;
    double aspect = 0.666;
    double handTop = 0.07;
    double tip = 0.08;
    double wrist = 0.45;
    double rodEnd = 0.95;
    double tipX = 0.5;
    double rodX = 0.5;
    double handMinX = 0.2;
    double handMaxX = 0.8;
    double rodMinX = 0.45;
    double rodMaxX = 0.55;
    std::mutex mutex;
    
    void EnsureDisplayImages(int targetH = 512);
};

class HandAssetManager {
public:
    static HandAssetManager& Instance();
    
    std::shared_ptr<HandAsset> Get(PointerStyleId id);
    std::shared_ptr<Gdiplus::Bitmap> GetThumbnail(PointerStyleId id);
    void PreloadAll();
    void ClearCache();
    
private:
    HandAssetManager();
    ~HandAssetManager() = default;
    
    std::shared_ptr<HandAsset> LoadAndMeasure(const std::wstring& name);
    std::wstring FindAssetPath(const std::wstring& filename);
    std::shared_ptr<Gdiplus::Bitmap> GenerateThumbnail(const HandAsset& asset);
    
    std::mutex m_mutex;
    std::map<PointerStyleId, std::shared_ptr<HandAsset>> m_cache;
    std::wstring m_exeDir;
};

} // namespace TinyHand
