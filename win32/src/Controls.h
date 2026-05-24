#pragma once

// Windowless custom-drawn control toolkit for the preferences window. A
// single Widget struct (tagged by type) covers every control the WPF build
// got from WPF-UI — sliders, toggle switches, radios, buttons, dropdowns.
// Drawing them ourselves (GDI+, anti-aliased) is both more faithful to the
// Fluent look and less code than dark-theming 30+ child HWNDs.
//
// Widgets live in content coordinates; the window translates by the scroll
// offset before painting and before dispatching mouse events.

#include "Common.h"
#include "GdiPlusInc.h"
#include <string>
#include <vector>
#include <functional>

enum class WT { Label, Slider, Toggle, Radio, Button, Progress, Dropdown, Separator };

// Vector icon ids — drawn by DrawIconGlyph. Used instead of emoji, which the
// GDI+ text path renders as tofu boxes.
enum {
    IconThermo = 0, IconSun, IconClock, IconSwap, IconSliders, IconPin, IconBed,
    IconMoon, IconCalendar
};

// Draws icon `id` filling a `size`-square box at (x, y), in `color`.
void DrawIconGlyph(Gdiplus::Graphics& g, int id, float x, float y, float size,
                   Gdiplus::Color color);

struct Widget
{
    WT type = WT::Label;
    RECT bounds{};
    bool visible = true;
    bool enabled = true;

    // Text / label.
    std::wstring text;
    std::function<std::wstring()> textProvider; // if set, overrides `text`
    int  fontPx = 14;
    bool bold = false;
    int  align = 0;        // 0 = left, 1 = right, 2 = center
    bool secondary = false;
    bool wrap = false;     // multi-line label
    bool warn = false;     // draw label in the warm-warning colour
    int  iconId = -1;      // leading vector icon (see DrawIconGlyph), -1 = none

    // Numeric (slider / progress).
    double minV = 0, maxV = 1, step = 0, value = 0;

    // Boolean (toggle / radio).
    bool on = false;

    // Dropdown.
    std::vector<std::wstring> options;
    int selected = 0;

    // Transient interaction state.
    bool dragging = false;
    bool pressed = false;

    // Callbacks.
    std::function<void()>       onPreviewStart, onPreviewEnd, onClick;
    std::function<void(double)> onChanged;   // slider committed value
    std::function<void(bool)>   onToggled;   // toggle / radio
    std::function<void(int)>    onPick;      // dropdown selection

    // Pulled by the window on a settings change: sync re-reads state from
    // the model; visibleIf recomputes `visible`. Either may be unset.
    std::function<void()>       sync;
    std::function<bool()>       visibleIf;
};

bool WidgetContains(const Widget& w, POINT p);

// Mouse dispatch (content-space coordinates). Dropdowns are handled by the
// window itself (they need a screen-space popup), so WidgetDown ignores them.
// dpi is needed to map a slider's x position back to its value.
void WidgetDown(Widget& w, POINT p, int dpi);
void WidgetDrag(Widget& w, POINT p, int dpi);
void WidgetUp(Widget& w, POINT p, int dpi);

// Renders one widget. `g` is already translated into content space.
void PaintWidget(Gdiplus::Graphics& g, int dpi, const Widget& w,
                 POINT mouse, bool anyButtonDown);

// Shared text helper.
void DrawTextG(Gdiplus::Graphics& g, const std::wstring& text,
              int x, int y, int w, int h, int fontPx, bool bold,
              Gdiplus::Color color, int align, bool vcenter, bool wrap = false);

// Frees the GDI+ font cache (call before GdiplusShutdown).
void ReleaseFontCache();
