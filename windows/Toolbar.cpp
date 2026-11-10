#include "Toolbar.h"
#include "HandAsset.h"
#include <windowsx.h>

namespace TinyHand {

static const wchar_t* TOOLBAR_CLASS = L"TinyHand_ToolbarClass";

ToolbarWindow::ToolbarWindow() {
}

ToolbarWindow::~ToolbarWindow() {
    Destroy();
}

static LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ToolbarWindow* self = (ToolbarWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ToolbarWindow::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = ToolbarWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TOOLBAR_CLASS;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassExW(&wc);
    
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TOOLBAR_CLASS,
        L"TinyHand Toolbar",
        WS_POPUP,
        0, 0, m_windowW, m_windowH,
        NULL, NULL, hInstance, NULL
    );
    
    if (!m_hwnd) return false;
    
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    RecalculateLayout();
    return true;
}

void ToolbarWindow::Destroy() {
    if (m_hdcMem) {
        SelectObject(m_hdcMem, m_hOldBitmap);
        DeleteDC(m_hdcMem);
        m_hdcMem = NULL;
    }
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
        m_hBitmap = NULL;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

void ToolbarWindow::Show(bool show) {
    if (!m_hwnd) return;
    if (show) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(m_hwnd, SW_HIDE);
        m_alpha = 0.0f;
    }
}

void ToolbarWindow::SetCurrentStyle(PointerStyleId style) {
    m_currentStyle = style;
    m_contentDirty = true;
    Render();
}

void ToolbarWindow::SetPinnedStyles(const std::vector<PointerStyleId>& pinned) {
    m_pinnedStyles = pinned;
    m_contentDirty = true;
    RecalculateLayout();
    Render();
}

void ToolbarWindow::SetSoundEnabled(bool enabled) {
    m_soundEnabled = enabled;
    m_contentDirty = true;
    Render();
}

void ToolbarWindow::SetInkChoice(InkChoiceId choice) {
    m_inkChoice = choice;
    m_contentDirty = true;
    Render();
}

void ToolbarWindow::RevealTemporarily(double seconds) {
    m_revealUntil = GetTickCount64() + (ULONGLONG)(seconds * 1000.0);
}

RECT ToolbarWindow::GetTotalRect() const {
    RECT r = { m_windowX, m_windowY, m_windowX + m_windowW, m_windowY + m_windowH };
    return r;
}

std::vector<RECT> ToolbarWindow::GetInteractiveRects() const {
    std::vector<RECT> rects;
    if (!IsRevealed()) {
        return rects;
    }
    
    // Bar rect
    RECT bar = {
        (LONG)(m_windowX + m_barRect.X),
        (LONG)(m_windowY + m_barRect.Y),
        (LONG)(m_windowX + m_barRect.GetRight()),
        (LONG)(m_windowY + m_barRect.GetBottom())
    };
    rects.push_back(bar);
    
    // Tray rect if ink palette is open
    if (m_inkOpen && m_trayRect.Width > 0.0f) {
        RECT tray = {
            (LONG)(m_windowX + m_trayRect.X),
            (LONG)(m_windowY + m_trayRect.Y),
            (LONG)(m_windowX + m_trayRect.GetRight()),
            (LONG)(m_windowY + m_trayRect.GetBottom())
        };
        rects.push_back(tray);
    }
    return rects;
}

bool ToolbarWindow::IsOverInteractive(double screenX, double screenY) const {
    if (!IsRevealed()) return false;
    double relX = screenX - m_windowX;
    double relY = screenY - m_windowY;
    if (m_barRect.Contains((float)relX, (float)relY)) return true;
    if (m_inkOpen && m_trayRect.Contains((float)relX, (float)relY)) return true;
    return false;
}

void ToolbarWindow::RecalculateLayout() {
    std::lock_guard<std::recursive_mutex> lock(m_toolbarMutex);
    m_items.clear();
    
    const float btnSize = 36.0f;
    const float spacing = 6.0f;
    const float padX = 10.0f;
    const float barH = 50.0f;
    
    size_t pinnedCount = m_pinnedStyles.size();
    // Pinned buttons + divider (6px) + Ink (36) + Sound (36) + Exit (36) + padding
    float barW = padX * 2.0f + (pinnedCount + 3) * (btnSize + spacing) + 8.0f;
    
    m_windowW = (int)ceil(barW + 40.0f);
    m_windowH = 130; // space for tray above + bar below
    
    m_windowX = (int)(m_screenX + (m_screenW - m_windowW) / 2.0);
    m_windowY = (int)(m_screenY + m_screenH - m_windowH - 16.0);
    
    float barX = (m_windowW - barW) / 2.0f;
    float barY = m_windowH - barH - 8.0f;
    m_barRect = Gdiplus::RectF(barX, barY, barW, barH);
    
    // Position style buttons
    float curX = barX + padX;
    float curY = barY + (barH - btnSize) / 2.0f;
    
    for (size_t i = 0; i < pinnedCount; ++i) {
        ToolbarItem item;
        item.type = ToolbarButtonType::Style;
        item.styleId = m_pinnedStyles[i];
        item.rect = Gdiplus::RectF(curX, curY, btnSize, btnSize);
        m_items.push_back(item);
        curX += btnSize + spacing;
    }
    
    // Divider
    curX += 4.0f;
    
    // Ink button
    {
        ToolbarItem item;
        item.type = ToolbarButtonType::InkToggle;
        item.rect = Gdiplus::RectF(curX, curY, btnSize, btnSize);
        m_items.push_back(item);
        curX += btnSize + spacing;
    }
    
    // Sound button
    {
        ToolbarItem item;
        item.type = ToolbarButtonType::SoundToggle;
        item.rect = Gdiplus::RectF(curX, curY, btnSize, btnSize);
        m_items.push_back(item);
        curX += btnSize + spacing;
    }
    
    // Exit button
    {
        ToolbarItem item;
        item.type = ToolbarButtonType::Exit;
        item.rect = Gdiplus::RectF(curX, curY, btnSize, btnSize);
        m_items.push_back(item);
    }
    
    // Ink Tray Layout (floating above toolbar)
    if (m_inkOpen) {
        const auto& choices = GetAllInkChoices();
        float swatchSize = 28.0f;
        float traySpacing = 4.0f;
        float trayPad = 6.0f;
        float trayW = trayPad * 2.0f + choices.size() * (swatchSize + traySpacing) - traySpacing;
        float trayH = 40.0f;
        float trayX = (m_windowW - trayW) / 2.0f;
        float trayY = barY - trayH - 8.0f;
        
        m_trayRect = Gdiplus::RectF(trayX, trayY, trayW, trayH);
        
        float sx = trayX + trayPad;
        float sy = trayY + (trayH - swatchSize) / 2.0f;
        for (const auto& c : choices) {
            ToolbarItem item;
            item.type = ToolbarButtonType::InkChoice;
            item.inkId = c.id;
            item.rect = Gdiplus::RectF(sx, sy, swatchSize, swatchSize);
            m_items.push_back(item);
            sx += swatchSize + traySpacing;
        }
    } else {
        m_trayRect = Gdiplus::RectF(0, 0, 0, 0);
    }
    
    if (m_hwnd) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, m_windowX, m_windowY, m_windowW, m_windowH,
                     SWP_NOACTIVATE | SWP_NOZORDER);
                     
        HDC hdcScreen = GetDC(NULL);
        if (!m_hdcMem) {
            m_hdcMem = CreateCompatibleDC(hdcScreen);
        }
        if (m_hBitmap) {
            SelectObject(m_hdcMem, m_hOldBitmap);
            DeleteObject(m_hBitmap);
            m_hBitmap = NULL;
        }
        
        BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER) };
        bmi.bmiHeader.biWidth = m_windowW;
        bmi.bmiHeader.biHeight = -m_windowH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        
        m_hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_bits, NULL, 0);
        m_hOldBitmap = SelectObject(m_hdcMem, m_hBitmap);
        ReleaseDC(NULL, hdcScreen);
        
        m_contentDirty = true;
    }
}

ToolbarItem* ToolbarWindow::HitTest(int x, int y) {
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].rect.Contains((float)x, (float)y)) {
            return &m_items[i];
        }
    }
    return nullptr;
}

void ToolbarWindow::Update(double cursorX, double cursorY, double screenX, double screenY,
                           double screenW, double screenH, bool isDrawing, ULONGLONG now) {
    std::lock_guard<std::recursive_mutex> lock(m_toolbarMutex);
    bool screenChanged = (m_screenX != screenX || m_screenY != screenY ||
                          m_screenW != screenW || m_screenH != screenH);
    if (screenChanged) {
        m_screenX = screenX;
        m_screenY = screenY;
        m_screenW = screenW;
        m_screenH = screenH;
        RecalculateLayout();
    }
    
    bool nearBottom = (cursorY >= (screenY + screenH - 90.0) && cursorY <= (screenY + screenH));
    bool temporarilyRevealed = (now < m_revealUntil);
    bool shouldReveal = !isDrawing && (nearBottom || temporarilyRevealed || m_inkOpen);
    
    m_targetAlpha = shouldReveal ? 1.0f : 0.0f;
    
    // Smooth transition
    float diff = m_targetAlpha - m_alpha;
    if (std::abs(diff) > 0.01f) {
        m_alpha += diff * 0.15f;
        Render();
    } else if (m_alpha != m_targetAlpha) {
        m_alpha = m_targetAlpha;
        Render();
    }
}

static void AddRoundedRect(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, float radius) {
    float dia = radius * 2.0f;
    path.AddArc(r.X, r.Y, dia, dia, 180.0f, 90.0f);
    path.AddArc(r.GetRight() - dia, r.Y, dia, dia, 270.0f, 90.0f);
    path.AddArc(r.GetRight() - dia, r.GetBottom() - dia, dia, dia, 0.0f, 90.0f);
    path.AddArc(r.X, r.GetBottom() - dia, dia, dia, 90.0f, 90.0f);
    path.CloseFigure();
}

void ToolbarWindow::RenderContent() {
    if (!m_hdcMem || !m_bits) return;
    
    Gdiplus::Graphics g(m_hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
        
        // 1. Draw Toolbar background
        Gdiplus::GraphicsPath barPath;
        AddRoundedRect(barPath, m_barRect, 16.0f);
        
        // Dark glass fill
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(220, 22, 22, 26));
        g.FillPath(&bgBrush, &barPath);
        
        // Border
        Gdiplus::Pen borderPen(Gdiplus::Color(35, 255, 255, 255), 1.0f);
        g.DrawPath(&borderPen, &barPath);
        
        // 2. Draw Divider
        float divX = 0;
        for (const auto& item : m_items) {
            if (item.type == ToolbarButtonType::InkToggle) {
                divX = item.rect.X - 5.0f;
                break;
            }
        }
        if (divX > 0) {
            Gdiplus::Pen divPen(Gdiplus::Color(35, 255, 255, 255), 1.0f);
            g.DrawLine(&divPen, divX, m_barRect.Y + 10.0f, divX, m_barRect.GetBottom() - 10.0f);
        }
        
        // 3. Draw Buttons
        for (int i = 0; i < (int)m_items.size(); ++i) {
            const auto& item = m_items[i];
            bool hovered = (i == m_hoveredIndex);
            
            if (item.type == ToolbarButtonType::Style) {
                bool selected = (item.styleId == m_currentStyle);
                if (selected || hovered) {
                    Gdiplus::GraphicsPath btnPath;
                    AddRoundedRect(btnPath, item.rect, 8.0f);
                    BYTE alpha = selected ? 65 : 30;
                    Gdiplus::SolidBrush hl(Gdiplus::Color(alpha, 255, 255, 255));
                    g.FillPath(&hl, &btnPath);
                    if (selected) {
                        Gdiplus::Pen selPen(Gdiplus::Color(100, 255, 255, 255), 1.0f);
                        g.DrawPath(&selPen, &btnPath);
                    }
                }
                
                auto thumb = HandAssetManager::Instance().GetThumbnail(item.styleId);
                if (thumb) {
                    Gdiplus::RectF thumbRect = item.rect;
                    thumbRect.Inflate(-2.0f, -2.0f);
                    g.DrawImage(thumb.get(), thumbRect);
                }
            } else if (item.type == ToolbarButtonType::InkToggle) {
                if (hovered || m_inkOpen) {
                    Gdiplus::GraphicsPath btnPath;
                    AddRoundedRect(btnPath, item.rect, 8.0f);
                    Gdiplus::SolidBrush hl(Gdiplus::Color(m_inkOpen ? 60 : 30, 255, 255, 255));
                    g.FillPath(&hl, &btnPath);
                }
                
                // Color circle swatch
                Gdiplus::Color swatchColor = ResolveInkColor(m_inkChoice, m_currentStyle);
                Gdiplus::RectF circleRect = item.rect;
                circleRect.Inflate(-8.0f, -8.0f);
                Gdiplus::SolidBrush swatchBrush(swatchColor);
                g.FillEllipse(&swatchBrush, circleRect);
                Gdiplus::Pen ringPen(Gdiplus::Color(180, 255, 255, 255), 1.5f);
                g.DrawEllipse(&ringPen, circleRect);
            } else if (item.type == ToolbarButtonType::SoundToggle) {
                if (hovered) {
                    Gdiplus::GraphicsPath btnPath;
                    AddRoundedRect(btnPath, item.rect, 8.0f);
                    Gdiplus::SolidBrush hl(Gdiplus::Color(30, 255, 255, 255));
                    g.FillPath(&hl, &btnPath);
                }
                
                // Draw Speaker icon
                Gdiplus::Pen iconPen(Gdiplus::Color(220, 255, 255, 255), 1.8f);
                iconPen.SetStartCap(Gdiplus::LineCapRound);
                iconPen.SetEndCap(Gdiplus::LineCapRound);
                iconPen.SetLineJoin(Gdiplus::LineJoinRound);
                
                float cx = item.rect.X + item.rect.Width / 2.0f;
                float cy = item.rect.Y + item.rect.Height / 2.0f;
                
                // Speaker body
                Gdiplus::GraphicsPath spk;
                spk.AddLine(cx - 6.0f, cy - 3.0f, cx - 3.0f, cy - 3.0f);
                spk.AddLine(cx - 3.0f, cy - 3.0f, cx + 2.0f, cy - 7.0f);
                spk.AddLine(cx + 2.0f, cy - 7.0f, cx + 2.0f, cy + 7.0f);
                spk.AddLine(cx + 2.0f, cy + 7.0f, cx - 3.0f, cy + 3.0f);
                spk.AddLine(cx - 3.0f, cy + 3.0f, cx - 6.0f, cy + 3.0f);
                spk.CloseFigure();
                Gdiplus::SolidBrush spkBrush(Gdiplus::Color(220, 255, 255, 255));
                g.FillPath(&spkBrush, &spk);
                
                if (m_soundEnabled) {
                    // Sound waves
                    g.DrawArc(&iconPen, cx + 2.0f, cy - 5.0f, 6.0f, 10.0f, -45.0f, 90.0f);
                    g.DrawArc(&iconPen, cx + 3.0f, cy - 8.0f, 9.0f, 16.0f, -45.0f, 90.0f);
                } else {
                    // Slash
                    Gdiplus::Pen slashPen(Gdiplus::Color(230, 255, 80, 80), 2.0f);
                    slashPen.SetStartCap(Gdiplus::LineCapRound);
                    slashPen.SetEndCap(Gdiplus::LineCapRound);
                    g.DrawLine(&slashPen, cx - 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f);
                }
            } else if (item.type == ToolbarButtonType::Exit) {
                if (hovered) {
                    Gdiplus::GraphicsPath btnPath;
                    AddRoundedRect(btnPath, item.rect, 8.0f);
                    Gdiplus::SolidBrush hl(Gdiplus::Color(35, 255, 80, 80));
                    g.FillPath(&hl, &btnPath);
                }
                
                // Draw X
                Gdiplus::Pen xPen(Gdiplus::Color(220, 255, 255, 255), 1.8f);
                xPen.SetStartCap(Gdiplus::LineCapRound);
                xPen.SetEndCap(Gdiplus::LineCapRound);
                float cx = item.rect.X + item.rect.Width / 2.0f;
                float cy = item.rect.Y + item.rect.Height / 2.0f;
                float s = 5.0f;
                g.DrawLine(&xPen, cx - s, cy - s, cx + s, cy + s);
                g.DrawLine(&xPen, cx - s, cy + s, cx + s, cy - s);
            } else if (item.type == ToolbarButtonType::InkChoice) {
                // Ink Tray Swatch
                bool selected = (item.inkId == m_inkChoice);
                Gdiplus::Color sc = (item.inkId == InkChoiceId::Match)
                    ? GetStyleInfo(m_currentStyle)->inkColor
                    : ResolveInkColor(item.inkId, m_currentStyle);
                    
                Gdiplus::RectF circleRect = item.rect;
                circleRect.Inflate(hovered ? 0.0f : -2.0f, hovered ? 0.0f : -2.0f);
                
                Gdiplus::SolidBrush sb(sc);
                g.FillEllipse(&sb, circleRect);
                
                if (selected) {
                    Gdiplus::Pen selPen(Gdiplus::Color(240, 255, 255, 255), 2.0f);
                    g.DrawEllipse(&selPen, circleRect);
                } else if (hovered) {
                    Gdiplus::Pen hPen(Gdiplus::Color(140, 255, 255, 255), 1.5f);
                    g.DrawEllipse(&hPen, circleRect);
                }
            }
        }
        
        // 4. Draw Tray background if open
        if (m_inkOpen && m_trayRect.Width > 0) {
            Gdiplus::GraphicsPath trayPath;
            AddRoundedRect(trayPath, m_trayRect, 14.0f);
            Gdiplus::SolidBrush trayBg(Gdiplus::Color(230, 22, 22, 26));
            g.FillPath(&trayBg, &trayPath);
            Gdiplus::Pen trayBorder(Gdiplus::Color(35, 255, 255, 255), 1.0f);
            g.DrawPath(&trayBorder, &trayPath);
            
            // Re-render swatches inside tray
            for (int i = 0; i < (int)m_items.size(); ++i) {
                const auto& item = m_items[i];
                if (item.type != ToolbarButtonType::InkChoice) continue;
                
                bool hovered = (i == m_hoveredIndex);
                bool selected = (item.inkId == m_inkChoice);
                Gdiplus::Color sc = (item.inkId == InkChoiceId::Match)
                    ? GetStyleInfo(m_currentStyle)->inkColor
                    : ResolveInkColor(item.inkId, m_currentStyle);
                    
                Gdiplus::RectF circleRect = item.rect;
                circleRect.Inflate(hovered ? 0.0f : -2.0f, hovered ? 0.0f : -2.0f);
                
                Gdiplus::SolidBrush sb(sc);
                g.FillEllipse(&sb, circleRect);
                
                if (selected) {
                    Gdiplus::Pen selPen(Gdiplus::Color(240, 255, 255, 255), 2.0f);
                    g.DrawEllipse(&selPen, circleRect);
                } else if (hovered) {
                    Gdiplus::Pen hPen(Gdiplus::Color(140, 255, 255, 255), 1.5f);
                    g.DrawEllipse(&hPen, circleRect);
                }
            }
        }
    }

void ToolbarWindow::Render() {
    std::lock_guard<std::recursive_mutex> lock(m_toolbarMutex);
    if (!m_hwnd) return;
    
    if (m_alpha <= 0.005f) {
        POINT ptDst = { m_windowX, m_windowY };
        SIZE sz = { m_windowW, m_windowH };
        POINT ptSrc = { 0, 0 };
        BLENDFUNCTION blend = {};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 0;
        blend.AlphaFormat = AC_SRC_ALPHA;
        if (m_hdcMem) {
            UpdateLayeredWindow(m_hwnd, NULL, &ptDst, &sz, m_hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
        }
        return;
    }
    
    if (!m_hdcMem) {
        RecalculateLayout();
    }
    
    if (m_contentDirty) {
        RenderContent();
        m_contentDirty = false;
    }
    
    POINT ptDst = { m_windowX, m_windowY };
    SIZE sz = { m_windowW, m_windowH };
    POINT ptSrc = { 0, 0 };
    
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = (BYTE)(m_alpha * 255.0f);
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    UpdateLayeredWindow(m_hwnd, NULL, &ptDst, &sz, m_hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
}

LRESULT ToolbarWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
        
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
        
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (m_barRect.Contains((float)pt.x, (float)pt.y) ||
            (m_inkOpen && m_trayRect.Contains((float)pt.x, (float)pt.y))) {
            return HTCLIENT;
        }
        return HTTRANSPARENT;
    }
    
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        
        int newHover = -1;
        for (int i = 0; i < (int)m_items.size(); ++i) {
            if (m_items[i].rect.Contains((float)x, (float)y)) {
                newHover = i;
                break;
            }
        }
        if (newHover != m_hoveredIndex) {
            m_hoveredIndex = newHover;
            m_contentDirty = true;
            Render();
        }
        return 0;
    }
    
    case WM_MOUSELEAVE:
        if (m_hoveredIndex != -1) {
            m_hoveredIndex = -1;
            m_contentDirty = true;
            Render();
        }
        return 0;
        
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        ToolbarItem* item = HitTest(x, y);
        if (item) {
            if (item->type == ToolbarButtonType::Style) {
                if (onSelectStyle) onSelectStyle(item->styleId);
            } else if (item->type == ToolbarButtonType::InkToggle) {
                m_inkOpen = !m_inkOpen;
                RecalculateLayout();
                Render();
            } else if (item->type == ToolbarButtonType::InkChoice) {
                if (onSelectInk) onSelectInk(item->inkId);
                m_inkOpen = false;
                RecalculateLayout();
                Render();
            } else if (item->type == ToolbarButtonType::SoundToggle) {
                if (onToggleSound) onToggleSound();
            } else if (item->type == ToolbarButtonType::Exit) {
                if (onExit) onExit();
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace TinyHand
