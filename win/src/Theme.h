#pragma once

// Shared dark-Fluent theming: DWM Mica / dark-mode / rounded-corner helpers,
// a colour palette approximating WPF-UI's Dark theme, and DPI scaling. Used
// by the tray menu and the preferences window so both match the look the
// WPF build got for free.

#include "Common.h"
#include <dwmapi.h>
#include <uxtheme.h>

namespace Theme
{
    // ── DWM window attributes ─────────────────────────────────────────────
    constexpr DWORD AttrDarkMode      = 20; // DWMWA_USE_IMMERSIVE_DARK_MODE
    constexpr DWORD AttrCornerPref    = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
    constexpr DWORD AttrBackdropType  = 38; // DWMWA_SYSTEMBACKDROP_TYPE

    constexpr int CornerRound      = 2;     // DWMWCP_ROUND
    constexpr int CornerRoundSmall = 3;     // DWMWCP_ROUNDSMALL
    constexpr int BackdropMica     = 2;     // DWMSBT_MAINWINDOW   (Mica)
    constexpr int BackdropAcrylic  = 3;     // DWMSBT_TRANSIENTWINDOW (Acrylic)
    constexpr int BackdropMicaAlt  = 4;     // DWMSBT_TABBEDWINDOW (Mica Alt)

    // The chroma key: pixels painted pure black inside a fully-extended DWM
    // frame are rendered transparent, so the Mica/Acrylic material composited
    // by the DWM shows through. Everything we want opaque is painted in a
    // non-black colour.
    constexpr COLORREF KeyTransparent = RGB(0, 0, 0);

    inline void ApplyDarkMode(HWND h)
    {
        BOOL v = TRUE;
        DwmSetWindowAttribute(h, AttrDarkMode, &v, sizeof(v));
    }
    inline void ApplyMica(HWND h, int type = BackdropMica)
    {
        DwmSetWindowAttribute(h, AttrBackdropType, &type, sizeof(type));
    }
    inline void ApplyRoundedCorners(HWND h, int pref = CornerRound)
    {
        DwmSetWindowAttribute(h, AttrCornerPref, &pref, sizeof(pref));
    }

    // Dark mode + a system backdrop. On a window with a standard frame this
    // gives the DWM-drawn title bar a real Mica blur. We deliberately do NOT
    // extend the frame across the client area: doing that turns the whole
    // window into "glass", which both aliases anti-aliased content and makes
    // chroma-keyed regions hit-test as non-client (dead to mouse input).
    inline void EnableBackdrop(HWND h, int backdropType = BackdropMica)
    {
        ApplyDarkMode(h);
        DwmSetWindowAttribute(h, AttrBackdropType, &backdropType, sizeof(backdropType));
    }

    // ── Palette (dark Fluent approximation) ───────────────────────────────
    constexpr COLORREF Background    = RGB(32, 32, 32);
    constexpr COLORREF CardFill      = RGB(43, 43, 43);
    constexpr COLORREF CardBorder    = RGB(61, 61, 61);
    constexpr COLORREF ControlFill   = RGB(55, 55, 55);
    constexpr COLORREF ControlBorder = RGB(75, 75, 75);
    constexpr COLORREF TextPrimary   = RGB(255, 255, 255);
    constexpr COLORREF TextSecondary = RGB(170, 170, 170);
    constexpr COLORREF TextDisabled  = RGB(130, 130, 130);
    constexpr COLORREF Hover         = RGB(58, 58, 58);
    constexpr COLORREF Pressed       = RGB(50, 50, 50);
    constexpr COLORREF SeparatorLine = RGB(64, 64, 64);
    constexpr COLORREF TrackFill     = RGB(80, 80, 80);

    // Windows accent (queried once). Falls back to the Win11 default blue.
    inline COLORREF Accent()
    {
        DWORD color = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)))
            return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        return RGB(76, 194, 255);
    }

    // ── DPI ───────────────────────────────────────────────────────────────
    inline int Dpi(HWND h)
    {
        UINT d = GetDpiForWindow(h);
        return d ? (int)d : 96;
    }
    inline int Scale(int value, int dpi) { return MulDiv(value, dpi, 96); }
}
