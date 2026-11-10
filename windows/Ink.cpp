#include "Ink.h"
#include <cmath>

namespace TinyHand {

void InkManager::Begin(double x, double y, double width, Gdiplus::Color color) {
    InkStroke stroke;
    stroke.points.push_back({ x, y });
    stroke.width = width;
    stroke.color = color;
    stroke.startTime = GetTickCount64();
    stroke.endTime = 0;
    stroke.isEnded = false;
    m_strokes.push_back(std::move(stroke));
}

void InkManager::Extend(double x, double y) {
    if (m_strokes.empty() || m_strokes.back().isEnded) return;
    
    auto& pts = m_strokes.back().points;
    if (!pts.empty()) {
        double dx = x - pts.back().x;
        double dy = y - pts.back().y;
        if (sqrt(dx * dx + dy * dy) < 1.5) return;
    }
    pts.push_back({ x, y });
}

void InkManager::End() {
    if (!m_strokes.empty() && !m_strokes.back().isEnded) {
        m_strokes.back().isEnded = true;
        m_strokes.back().endTime = GetTickCount64();
    }
}

void InkManager::Clear() {
    m_strokes.clear();
}

bool InkManager::IsDrawing() const {
    return !m_strokes.empty() && !m_strokes.back().isEnded;
}

size_t InkManager::StrokeCount() const {
    return m_strokes.size();
}

void InkManager::Render(Gdiplus::Graphics& g, double fadeDelay, ULONGLONG now) {
    const double fadeDuration = 0.6;
    
    // Prune faded strokes
    if (fadeDelay > 0) {
        m_strokes.erase(
            std::remove_if(m_strokes.begin(), m_strokes.end(), [&](const InkStroke& s) {
                if (!s.isEnded) return false;
                double elapsed = (double)(now - s.endTime) / 1000.0;
                return elapsed >= (fadeDelay + fadeDuration);
            }),
            m_strokes.end()
        );
    }
    
    for (const auto& stroke : m_strokes) {
        if (stroke.points.size() < 2) continue;
        
        float alphaFactor = 1.0f;
        if (fadeDelay > 0 && stroke.isEnded) {
            double elapsed = (double)(now - stroke.endTime) / 1000.0;
            if (elapsed > fadeDelay) {
                double progress = (elapsed - fadeDelay) / fadeDuration;
                alphaFactor = (float)(std::max)(0.0, 1.0 - progress);
            }
        }
        
        if (alphaFactor <= 0.001f) continue;
        
        Gdiplus::GraphicsPath path;
        const auto& pts = stroke.points;
        
        Gdiplus::PointF current((float)pts[0].x, (float)pts[0].y);
        
        for (size_t i = 1; i < pts.size(); ++i) {
            Gdiplus::PointF next((float)pts[i].x, (float)pts[i].y);
            Gdiplus::PointF mid((current.X + next.X) / 2.0f, (current.Y + next.Y) / 2.0f);
            
            // Quadratic to cubic conversion
            Gdiplus::PointF cp1(
                current.X + 2.0f / 3.0f * (current.X - current.X), // control pt is current in first step
                current.Y + 2.0f / 3.0f * (current.Y - current.Y)
            );
            // Simpler and smoother in GDI+: AddCurve or AddLine segments with smooth joint
            path.AddLine(current, next);
            current = next;
        }
        
        // Shadow
        BYTE shadowA = (BYTE)(stroke.color.GetA() * 0.18f * alphaFactor);
        if (shadowA > 0) {
            Gdiplus::Pen shadowPen(Gdiplus::Color(shadowA, 0, 0, 0), (float)stroke.width);
            shadowPen.SetStartCap(Gdiplus::LineCapRound);
            shadowPen.SetEndCap(Gdiplus::LineCapRound);
            shadowPen.SetLineJoin(Gdiplus::LineJoinRound);
            
            Gdiplus::Matrix mat;
            mat.Translate(1.0f, 1.5f);
            Gdiplus::GraphicsPath shadowPath;
            shadowPath.AddPath(&path, FALSE);
            shadowPath.Transform(&mat);
            g.DrawPath(&shadowPen, &shadowPath);
        }
        
        // Stroke
        BYTE strokeA = (BYTE)(stroke.color.GetA() * alphaFactor);
        Gdiplus::Color drawColor(strokeA, stroke.color.GetR(), stroke.color.GetG(), stroke.color.GetB());
        Gdiplus::Pen pen(drawColor, (float)stroke.width);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPath(&pen, &path);
    }
}

} // namespace TinyHand
