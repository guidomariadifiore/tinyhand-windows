#pragma once

#include "Common.h"
#include "HandAsset.h"
#include "Ink.h"

namespace TinyHand {

class ArmRenderer {
public:
    ArmRenderer();
    
    void Press();
    void ResetGrip();
    void StepAnimation(bool held);
    bool IsSettling() const;
    
    void RenderArm(Gdiplus::Graphics& g, PointerStyleId style, double cursorX, double cursorY,
                   double screenW, double screenH, bool showShadow, double handLenFrac);
                   
    void RenderLaser(Gdiplus::Graphics& g, double cursorX, double cursorY,
                     double screenH, double handLenFrac);

    double GetPressAmount() const { return m_pressAmount; }

private:
    double m_pressAmount = 0.0;
    double m_gripX = -1.0;
    double m_gripTarget = -1.0;
    bool m_hasGrip = false;
};

} // namespace TinyHand
