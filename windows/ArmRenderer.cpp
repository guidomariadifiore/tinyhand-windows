#include "ArmRenderer.h"
#include <cmath>
#include <algorithm>

namespace TinyHand {

ArmRenderer::ArmRenderer() {
}

void ArmRenderer::Press() {
    m_pressAmount = 1.0;
}

void ArmRenderer::ResetGrip() {
    m_hasGrip = false;
    m_pressAmount = 0.0;
}

void ArmRenderer::StepAnimation(bool held) {
    if (!held) {
        m_pressAmount += (0.0 - m_pressAmount) * 0.18;
        if (m_pressAmount < 0.001) m_pressAmount = 0.0;
    }
}

bool ArmRenderer::IsSettling() const {
    if (m_pressAmount > 0.002) return true;
    if (m_hasGrip && std::abs(m_gripTarget - m_gripX) > 0.5) return true;
    return false;
}

void ArmRenderer::RenderLaser(Gdiplus::Graphics& g, double cursorX, double cursorY,
                              double screenH, double handLenFrac) {
    double r = (std::max)(4.0, screenH * 0.0038 * handLenFrac / 0.13) * (1.0 + 0.2 * m_pressAmount);
    double core = r * 0.45;
    
    // Outer shadow/glow
    float glowRadius = (float)(r * (2.2 + 1.0 * m_pressAmount));
    Gdiplus::GraphicsPath glowPath;
    glowPath.AddEllipse((float)(cursorX - glowRadius), (float)(cursorY - glowRadius), glowRadius * 2.0f, glowRadius * 2.0f);
    
    Gdiplus::PathGradientBrush pgb(&glowPath);
    pgb.SetCenterColor(Gdiplus::Color(180, 255, 41, 31));
    Gdiplus::Color surroundColor(0, 255, 41, 31);
    int count = 1;
    pgb.SetSurroundColors(&surroundColor, &count);
    g.FillPath(&pgb, &glowPath);
    
    // Middle solid red dot
    Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255, 255, 41, 31));
    g.FillEllipse(&dotBrush, (float)(cursorX - r), (float)(cursorY - r), (float)(r * 2.0), (float)(r * 2.0));
    
    // Inner bright core
    Gdiplus::SolidBrush coreBrush(Gdiplus::Color(242, 255, 219, 209));
    g.FillEllipse(&coreBrush, (float)(cursorX - core), (float)(cursorY - core), (float)(core * 2.0), (float)(core * 2.0));
}

void ArmRenderer::RenderArm(Gdiplus::Graphics& g, PointerStyleId style, double cursorX, double cursorY,
                            double screenW, double screenH, bool showShadow, double handLenFrac) {
    if (style == PointerStyleId::Laser) {
        RenderLaser(g, cursorX, cursorY, screenH, handLenFrac);
        return;
    }
    
    auto asset = HandAssetManager::Instance().Get(style);
    if (!asset || !asset->image) return;
    
    const double minRodFrac = 0.05;
    const double gripLeanFrac = 0.30;
    const double gripDepthFrac = 0.12;
    const double gripFollow = 0.08;
    const double liftFrac = 0.06;
    
    double targetX = (std::min)((std::max)(cursorX + gripLeanFrac * screenW, screenW * 0.15), screenW * 1.05);
    m_gripTarget = targetX;
    
    if (!m_hasGrip) {
        m_gripX = targetX;
        m_hasGrip = true;
    } else {
        m_gripX = m_gripX + (targetX - m_gripX) * gripFollow;
    }
    
    double gx = m_gripX;
    double gy = screenH + gripDepthFrac * screenH;
    
    double dx = cursorX - gx;
    double dy = cursorY - gy;
    double reach = sqrt(dx * dx + dy * dy);
    double aimAngle = atan2(dy, dx);
    
    double handPts = handLenFrac * screenH * (1.0 - 0.06 * m_pressAmount);
    double s = handPts / (asset->wrist - asset->handTop);
    double widthPts = s * asset->aspect;
    double handTipPts = (asset->wrist - asset->tip) * s;
    double tipOffX = (asset->tipX - asset->rodX) * widthPts;
    
    double along = sqrt((std::max)(0.0, reach * reach - tipOffX * tipOffX));
    double rodLen = (std::max)(minRodFrac * screenH, along - handTipPts);
    double tipAngle = atan2(tipOffX, rodLen + handTipPts);
    double rodAngle = aimAngle - tipAngle;
    
    double lift = liftFrac * screenH * (1.0 - 0.8 * m_pressAmount);
    
    int imgW = asset->image->GetWidth();
    int imgH = asset->image->GetHeight();
    
    auto drawPiece = [&](double startAlong, double lenAlong, double srcY, double srcH, double offX, double offY, Gdiplus::ImageAttributes* attr) {
        double ux = cos(rodAngle);
        double uy = sin(rodAngle);
        double vx = -sin(rodAngle);
        double vy = cos(rodAngle);
        
        double leftEdge = -asset->rodX * widthPts;
        double rightEdge = (1.0 - asset->rodX) * widthPts;
        
        Gdiplus::PointF ptTL(
            (float)(gx + offX + ux * (startAlong + lenAlong) + vx * leftEdge),
            (float)(gy + offY + uy * (startAlong + lenAlong) + vy * leftEdge)
        );
        Gdiplus::PointF ptTR(
            (float)(gx + offX + ux * (startAlong + lenAlong) + vx * rightEdge),
            (float)(gy + offY + uy * (startAlong + lenAlong) + vy * rightEdge)
        );
        Gdiplus::PointF ptBL(
            (float)(gx + offX + ux * startAlong + vx * leftEdge),
            (float)(gy + offY + uy * startAlong + vy * leftEdge)
        );
        
        Gdiplus::PointF destPts[3] = { ptTL, ptTR, ptBL };
        g.DrawImage(asset->image.get(), destPts, 3, 0.0f, (float)srcY, (float)imgW, (float)srcH, Gdiplus::UnitPixel, attr);
    };
    
    // Shadow pass
    if (showShadow) {
        Gdiplus::ImageAttributes shadowAttr;
        float alpha = (float)(0.26 + 0.02 * m_pressAmount);
        Gdiplus::ColorMatrix cm = {
            0, 0, 0, 0, 0,
            0, 0, 0, 0, 0,
            0, 0, 0, 0, 0,
            0, 0, 0, alpha, 0,
            0, 0, 0, 0, 1
        };
        shadowAttr.SetColorMatrix(&cm, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
        
        double sx = lift * 0.6;
        double sy = lift;
        
        drawPiece(0, rodLen, asset->wrist * imgH, (asset->rodEnd - asset->wrist) * imgH, sx, sy, &shadowAttr);
        drawPiece(rodLen, handPts, asset->handTop * imgH, (asset->wrist - asset->handTop) * imgH, sx, sy, &shadowAttr);
    }
    
    // Actual rod and hand
    drawPiece(0, rodLen, asset->wrist * imgH, (asset->rodEnd - asset->wrist) * imgH, 0, 0, NULL);
    drawPiece(rodLen, handPts, asset->handTop * imgH, (asset->wrist - asset->handTop) * imgH, 0, 0, NULL);
}

} // namespace TinyHand
