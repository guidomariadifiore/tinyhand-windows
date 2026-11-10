#pragma once

#ifndef OEMRESOURCE
#define OEMRESOURCE
#endif

#include <windows.h>
#include <mutex>

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
    mutable std::mutex m_mutex;
};

} // namespace TinyHand
