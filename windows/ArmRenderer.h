#pragma once

#include "Common.h"
#include "HandAsset.h"
#include "Ink.h"
#include <chrono>

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
    std::chrono::steady_clock::time_point m_lastStepTime;
    std::chrono::steady_clock::time_point m_lastGripTime;
    bool m_hasStepTime = false;
    bool m_hasGripTime = false;
};

} // namespace TinyHand
