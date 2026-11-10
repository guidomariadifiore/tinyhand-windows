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
    m_hasStepTime = false;
    m_hasGripTime = false;
}

void ArmRenderer::StepAnimation(bool held) {
    auto now = std::chrono::steady_clock::now();
    double dt = 0.008;
    if (m_hasStepTime) {
        dt = std::chrono::duration<double>(now - m_lastStepTime).count();
    }
    m_lastStepTime = now;
    m_hasStepTime = true;
    if (dt > 0.1) dt = 0.1;
    if (dt < 0.0005) dt = 0.0005;
    
    if (!held) {
        double decay = 1.0 - std::pow((std::max)(0.0, 1.0 - 0.18), dt / 0.008);
        m_pressAmount += (0.0 - m_pressAmount) * decay;
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
    const double gripFollow = 0.12;
    const double liftFrac = 0.06;
    
    double targetX = (std::min)((std::max)(cursorX + gripLeanFrac * screenW, screenW * 0.15), screenW * 1.05);
    m_gripTarget = targetX;
    
    auto now = std::chrono::steady_clock::now();
    double dt = 0.008;
    if (m_hasGripTime) {
        dt = std::chrono::duration<double>(now - m_lastGripTime).count();
    }
    m_lastGripTime = now;
    m_hasGripTime = true;
    if (dt > 0.05) dt = 0.05;
    if (dt < 0.0005) dt = 0.0005;
    
    if (!m_hasGrip) {
        m_gripX = targetX;
        m_hasGrip = true;
    } else {
        double gripDecay = 1.0 - std::pow((std::max)(0.0, 1.0 - gripFollow), dt / 0.008);
        m_gripX = m_gripX + (targetX - m_gripX) * gripDecay;
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
    
    int targetH = (screenH > 1440) ? 1024 : 512;
    asset->EnsureDisplayImages(targetH);
    
    Gdiplus::Bitmap* armBmp = asset->displayImage ? asset->displayImage.get() : asset->image.get();
    Gdiplus::Bitmap* shadowBmp = asset->displayShadow ? asset->displayShadow.get() : nullptr;
    
    int imgW = armBmp->GetWidth();
    int imgH = armBmp->GetHeight();
    
    auto drawPiece = [&](Gdiplus::Bitmap* bmp, double startAlong, double lenAlong,
                         double srcMinNormX, double srcMaxNormX,
                         double srcNormY, double srcNormH,
                         double offX, double offY, Gdiplus::ImageAttributes* attr) {
        double ux = cos(rodAngle);
        double uy = sin(rodAngle);
        double vx = -sin(rodAngle);
        double vy = cos(rodAngle);
        
        double leftEdge = (srcMinNormX - asset->rodX) * widthPts;
        double rightEdge = (srcMaxNormX - asset->rodX) * widthPts;
        
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
        float srcX = (float)(srcMinNormX * imgW);
        float srcW = (float)((srcMaxNormX - srcMinNormX) * imgW);
        float srcY = (float)(srcNormY * imgH);
        float srcH = (float)(srcNormH * imgH);
        g.DrawImage(bmp, destPts, 3, srcX, srcY, srcW, srcH, Gdiplus::UnitPixel, attr);
    };
    
    // Shadow pass
    if (showShadow) {
        double sx = lift * 0.6;
        double sy = lift;
        
        if (shadowBmp) {
            drawPiece(shadowBmp, 0, rodLen + 1.0, asset->rodMinX, asset->rodMaxX,
                      asset->wrist, asset->rodEnd - asset->wrist, sx, sy, NULL);
            drawPiece(shadowBmp, rodLen, handPts, asset->handMinX, asset->handMaxX,
                      asset->handTop, asset->wrist - asset->handTop, sx, sy, NULL);
        } else {
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
            
            drawPiece(armBmp, 0, rodLen + 1.0, asset->rodMinX, asset->rodMaxX,
                      asset->wrist, asset->rodEnd - asset->wrist, sx, sy, &shadowAttr);
            drawPiece(armBmp, rodLen, handPts, asset->handMinX, asset->handMaxX,
                      asset->handTop, asset->wrist - asset->handTop, sx, sy, &shadowAttr);
        }
    }
    
    // Actual rod and hand
    drawPiece(armBmp, 0, rodLen + 1.0, asset->rodMinX, asset->rodMaxX,
              asset->wrist, asset->rodEnd - asset->wrist, 0, 0, NULL);
    drawPiece(armBmp, rodLen, handPts, asset->handMinX, asset->handMaxX,
              asset->handTop, asset->wrist - asset->handTop, 0, 0, NULL);
}

} // namespace TinyHand
