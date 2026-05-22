#include "TrayMenu.h"
#include "Theme.h"
#include "Controls.h"

namespace {
const wchar_t* kOwnerClass = L"KelvinShiftMenuOwner";
bool g_ownerClassRegistered = false;
}

TrayMenu::~TrayMenu()
{
    if (font_) DeleteObject(font_);
    if (bg_) DeleteObject(bg_);
    if (owner_) DestroyWindow(owner_);
}

void TrayMenu::EnsureOwner()
{
    if (owner_) return;
    if (!g_ownerClassRegistered)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &TrayMenu::OwnerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kOwnerClass;
        RegisterClassW(&wc);
        g_ownerClassRegistered = true;
    }
    // A normal (not message-only) hidden window: it can be made foreground,
    // which is what lets TrackPopupMenuEx dismiss correctly on click-away.
    owner_ = CreateWindowExW(WS_EX_TOOLWINDOW, kOwnerClass, L"", WS_POPUP,
                             0, 0, 0, 0, nullptr, nullptr,
                             GetModuleHandleW(nullptr), this);
    bg_ = CreateSolidBrush(Theme::Background);
}

void TrayMenu::Track(std::vector<MenuItem> items, int x, int y,
                     bool below, int minWidth)
{
    items_ = std::move(items);
    EnsureOwner();

    // Park the owner at the anchor so per-monitor DPI resolves correctly.
    SetWindowPos(owner_, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    dpi_ = Theme::Dpi(owner_);
    minWidth_ = minWidth;

    if (font_) DeleteObject(font_);
    font_ = CreateFontW(-Theme::Scale(14, dpi_), 0, 0, 0, FW_NORMAL,
                        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");

    // Value column: positioned past the widest label so the Day / Night /
    // Bedtime values line up in a column instead of drifting with the label.
    {
        HDC dc = GetDC(owner_);
        HFONT old = (HFONT)SelectObject(dc, font_);
        int maxLabel = 0;
        for (const auto& it : items_)
        {
            if (it.value.empty()) continue;
            RECT lr{ 0, 0, 0, 0 };
            DrawTextW(dc, it.text.c_str(), -1, &lr, DT_CALCRECT | DT_SINGLELINE);
            if (lr.right > maxLabel) maxLabel = lr.right;
        }
        SelectObject(dc, old);
        ReleaseDC(owner_, dc);
        valueColX_ = Theme::Scale(54, dpi_) + maxLabel + Theme::Scale(16, dpi_);
    }

    HMENU menu = CreatePopupMenu();
    MENUINFO mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = bg_;
    SetMenuInfo(menu, &mi);

    // Every row is owner-drawn (including separators) so the whole menu is
    // dark. Info rows are MF_GRAYED and separators MF_DISABLED, so neither is
    // selectable — TrackPopupMenuEx only ever returns a real action item.
    for (int i = 0; i < (int)items_.size(); ++i)
    {
        const MenuItem& it = items_[i];
        UINT flags = MF_OWNERDRAW;
        // MF_GRAYED (not bare MF_DISABLED) for separators and info rows: an
        // MF_DISABLED owner-draw row still highlights on hover, which looked
        // like a phantom clickable button above "Enabled".
        if (it.separator || !it.enabled) flags |= MF_GRAYED;
        if (it.checkable && it.checked) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, (UINT_PTR)(i + 1), (LPCWSTR)(INT_PTR)i);
    }

    // Tray-menu foreground dance — without it the menu can fail to dismiss.
    SetForegroundWindow(owner_);
    UINT align = below ? (TPM_LEFTALIGN | TPM_TOPALIGN)
                       : (TPM_RIGHTALIGN | TPM_BOTTOMALIGN);
    int cmd = TrackPopupMenuEx(menu,
                               align | TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
                               x, y, owner_, nullptr);
    PostMessageW(owner_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd > 0 && cmd <= (int)items_.size())
    {
        auto cb = items_[cmd - 1].onClick; // copy — callback may quit the app
        if (cb) cb();
    }
}

void TrayMenu::MeasureItem(MEASUREITEMSTRUCT* m)
{
    int i = (int)m->itemData;
    if (i < 0 || i >= (int)items_.size()) return;
    const MenuItem& it = items_[i];

    if (it.separator)
    {
        m->itemHeight = Theme::Scale(9, dpi_);
        m->itemWidth = minWidth_;
        return;
    }

    HDC dc = GetDC(owner_);
    HFONT old = (HFONT)SelectObject(dc, font_);
    // DT_CALCRECT measures with font fallback — matching what DrawTextW
    // actually renders — so emoji (the bed glyph, arrows) aren't
    // under-measured, which was truncating the Schedule row.
    RECT tr{ 0, 0, 0, 0 };
    DrawTextW(dc, it.text.c_str(), -1, &tr, DT_CALCRECT | DT_SINGLELINE);
    int w;
    if (!it.value.empty())
    {
        RECT vr{ 0, 0, 0, 0 };
        DrawTextW(dc, it.value.c_str(), -1, &vr, DT_CALCRECT | DT_SINGLELINE);
        w = valueColX_ + vr.right + Theme::Scale(28, dpi_);
    }
    else
    {
        w = Theme::Scale(54, dpi_) + tr.right + Theme::Scale(28, dpi_);
    }
    SelectObject(dc, old);
    ReleaseDC(owner_, dc);

    m->itemHeight = Theme::Scale(32, dpi_);
    m->itemWidth = (w > minWidth_) ? w : minWidth_;
}

void TrayMenu::DrawItem(const DRAWITEMSTRUCT* d)
{
    int i = (int)d->itemData;
    if (i < 0 || i >= (int)items_.size()) return;
    const MenuItem& it = items_[i];
    HDC dc = d->hDC;
    RECT rc = d->rcItem;

    bool selected = (d->itemState & ODS_SELECTED) != 0;
    bool grayed = (d->itemState & ODS_GRAYED) != 0;
    // Only real, enabled action rows get the hover highlight — never the
    // greyed info rows or separators. (Owner-draw menus still mark greyed
    // items ODS_SELECTED on hover, which looked like phantom buttons.)
    bool highlight = selected && !grayed && !it.separator;

    // Row background.
    HBRUSH fill = CreateSolidBrush(highlight ? Theme::Hover : Theme::Background);
    FillRect(dc, &rc, fill);
    DeleteObject(fill);

    if (it.separator)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, Theme::SeparatorLine);
        HPEN old = (HPEN)SelectObject(dc, pen);
        int cy = (rc.top + rc.bottom) / 2;
        int inset = Theme::Scale(10, dpi_);
        MoveToEx(dc, rc.left + inset, cy, nullptr);
        LineTo(dc, rc.right - inset, cy);
        SelectObject(dc, old);
        DeleteObject(pen);
        return;
    }

    // Three fixed columns so every row lines up: a check column, a glyph
    // column, then the label. Labels always start at labelX regardless of
    // whether the row has a glyph (and which glyph), so Day / Night / Bedtime
    // align as a block.
    int glyphX = Theme::Scale(24, dpi_);
    int labelX = Theme::Scale(54, dpi_);
    HFONT old = (HFONT)SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);

    if (it.checkable && it.checked)
    {
        SetTextColor(dc, Theme::Accent());
        RECT g{ rc.left, rc.top, rc.left + glyphX, rc.bottom };
        DrawTextW(dc, L"\x2713", -1, &g, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    COLORREF textCol = grayed ? Theme::TextSecondary : Theme::TextPrimary;
    if (it.iconId >= 0)
    {
        // Vector icon — uniform size and spacing, unlike the ragged Unicode
        // symbols (the moon glyph rendered tiny, the bed didn't exist).
        Gdiplus::Graphics gfx(dc);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        int isz = Theme::Scale(17, dpi_);
        int iy = rc.top + ((rc.bottom - rc.top) - isz) / 2;
        DrawIconGlyph(gfx, it.iconId, (float)(rc.left + glyphX), (float)iy, (float)isz,
                      Gdiplus::Color(255, GetRValue(textCol), GetGValue(textCol),
                                     GetBValue(textCol)));
    }

    SetTextColor(dc, textCol);
    int textRight = it.value.empty() ? (rc.right - Theme::Scale(12, dpi_))
                                     : (rc.left + valueColX_ - Theme::Scale(6, dpi_));
    RECT tr{ rc.left + labelX, rc.top, textRight, rc.bottom };
    DrawTextW(dc, it.text.c_str(), -1, &tr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!it.value.empty())
    {
        RECT vr{ rc.left + valueColX_, rc.top,
                 rc.right - Theme::Scale(12, dpi_), rc.bottom };
        DrawTextW(dc, it.value.c_str(), -1, &vr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(dc, old);
}

LRESULT CALLBACK TrayMenu::OwnerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = reinterpret_cast<TrayMenu*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self)
    {
        if (msg == WM_MEASUREITEM)
        {
            self->MeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lp));
            return TRUE;
        }
        if (msg == WM_DRAWITEM)
        {
            self->DrawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            return TRUE;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
