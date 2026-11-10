#pragma once

#include "Common.h"
#include <vector>
#include <mutex>

namespace TinyHand {

struct InkPoint {
    double x;
    double y;
};

struct InkStroke {
    std::vector<InkPoint> points;
    double width;
    Gdiplus::Color color;
    ULONGLONG startTime;
    ULONGLONG endTime;
    bool isEnded = false;
};

class InkManager {
public:
    InkManager() = default;
    
    void Begin(double x, double y, double width, Gdiplus::Color color);
    void Extend(double x, double y);
    void End();
    void Clear();
    
    bool IsDrawing() const;
    size_t StrokeCount() const;
    
    void Render(Gdiplus::Graphics& g, double fadeDelay, ULONGLONG now);
    
private:
    mutable std::mutex m_mutex;
    std::vector<InkStroke> m_strokes;
};

} // namespace TinyHand
