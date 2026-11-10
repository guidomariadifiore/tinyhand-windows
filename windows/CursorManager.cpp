#include "CursorManager.h"
#include <vector>

namespace TinyHand {

static void OnCursorProcessExit() {
    CursorManager::Instance().Restore();
}

static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    CursorManager::Instance().Restore();
    return FALSE;
}

CursorManager& CursorManager::Instance() {
    static CursorManager instance;
    static bool s_handlersInstalled = false;
    if (!s_handlersInstalled) {
        s_handlersInstalled = true;
        atexit(OnCursorProcessExit);
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    }
    return instance;
}

CursorManager::CursorManager() {
}

CursorManager::~CursorManager() {
    Restore();
}

HCURSOR CursorManager::CreateBlankCursor() {
    int w = GetSystemMetrics(SM_CXCURSOR);
    int h = GetSystemMetrics(SM_CYCURSOR);
    
    std::vector<BYTE> andMask(w * h / 8, 0xFF);
    std::vector<BYTE> xorMask(w * h / 8, 0x00);
    
    return CreateCursor(NULL, 0, 0, w, h, andMask.data(), xorMask.data());
}

void CursorManager::Hide() {
    if (m_isHidden) return;
    
    const DWORD cursorIds[] = {
        OCR_NORMAL,
        OCR_IBEAM,
        OCR_WAIT,
        OCR_CROSS,
        OCR_UP,
        OCR_SIZE,
        OCR_ICON,
        OCR_SIZENWSE,
        OCR_SIZENESW,
        OCR_SIZEWE,
        OCR_SIZENS,
        OCR_SIZEALL,
        OCR_NO,
        OCR_HAND,
        OCR_APPSTARTING
    };
    
    for (DWORD id : cursorIds) {
        HCURSOR hBlank = CreateBlankCursor();
        if (hBlank) {
            SetSystemCursor(hBlank, id);
        }
    }
    
    m_isHidden = true;
}

void CursorManager::Show() {
    if (!m_isHidden) return;
    Restore();
}

void CursorManager::Restore() {
    if (m_isHidden) {
        SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, 0);
        m_isHidden = false;
    }
}

} // namespace TinyHand
