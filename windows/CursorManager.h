#pragma once

#ifndef OEMRESOURCE
#define OEMRESOURCE
#endif

#include <windows.h>

namespace TinyHand {

class CursorManager {
public:
    static CursorManager& Instance();
    
    void Hide();
    void Show();
    void Restore();
    bool IsHidden() const { return m_isHidden; }
    
private:
    CursorManager();
    ~CursorManager();
    
    HCURSOR CreateBlankCursor();
    
    bool m_isHidden = false;
};

} // namespace TinyHand
