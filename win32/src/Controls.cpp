#include "Controls.h"
#include "Theme.h"
#include <map>
#include <cmath>

using namespace Gdiplus;

// ── GDI+ helpers ──────────────────────────────────────────────────────────
namespace {

std::map<std::pair<int, bool>, Font*> g_fonts;

Font* GetFont(int px, bool bold)
{
    auto key = std::make_pair(px, bold);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    Font* f = new Font(L"Segoe UI", (REAL)px,
                       bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    g_fonts[key] = f;
    return f;
}

Color GC(COLORREF c, BYTE a = 255)
{
    return Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void RoundPath(GraphicsPath& p, float x, float y, float w, float h, float r)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r <= 0) { p.AddRectangle(RectF(x, y, w, h)); return; }
    p.AddArc(x,             y,             r * 2, r * 2, 180, 90);
    p.AddArc(x + w - r * 2, y,             r * 2, r * 2, 270, 90);
    p.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2,   0, 90);
    p.AddArc(x,             y + h - r * 2, r * 2, r * 2,  90, 90);
    p.CloseFigure();
}

void FillRound(Graphics& g, const Color& c, float x, float y, float w, float h, float r)
{
    GraphicsPath p;
    RoundPath(p, x, y, w, h, r);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

void StrokeRound(Graphics& g, const Color& c, float x, float y, float w, float h, float r)
{
    GraphicsPath p;
    RoundPath(p, x, y, w, h, r);
    Pen pen(c, 1.0f);
    g.DrawPath(&pen, &p);
}

// Horizontal slider track geometry.
void SliderTrack(const Widget& w, int dpi, float& left, float& width, float& cy)
{
    float r = (float)Theme::Scale(9, dpi);
    left  = w.bounds.left + r;
    width = (w.bounds.right - w.bounds.left) - 2 * r;
    if (width < 1) width = 1;
    cy = (w.bounds.top + w.bounds.bottom) / 2.0f;
}

double SnapValue(const Widget& w, double v)
{
    if (v < w.minV) v = w.minV;
    if (v > w.maxV) v = w.maxV;
    if (w.step > 0)
        v = w.minV + std::round((v - w.minV) / w.step) * w.step;
    if (v < w.minV) v = w.minV;
    if (v > w.maxV) v = w.maxV;
    return v;
}

void SliderSetFromX(Widget& w, int dpi, int x)
{
    float left, width, cy;
    SliderTrack(w, dpi, left, width, cy);
    double t = (x - left) / width;
    double v = SnapValue(w, w.minV + t * (w.maxV - w.minV));
    if (v != w.value)
    {
        w.value = v;
        if (w.onChanged) w.onChanged(v);
    }
}

} // namespace

void ReleaseFontCache()
{
    for (auto& kv : g_fonts) delete kv.second;
    g_fonts.clear();
}

// ── Hit testing / mouse ───────────────────────────────────────────────────

bool WidgetContains(const Widget& w, POINT p)
{
    return w.visible && p.x >= w.bounds.left && p.x < w.bounds.right
        && p.y >= w.bounds.top && p.y < w.bounds.bottom;
}

void WidgetDown(Widget& w, POINT p, int dpi)
{
    if (!w.visible || !w.enabled) return;
    switch (w.type)
    {
        case WT::Slider:
            w.dragging = true;
            if (w.onPreviewStart) w.onPreviewStart();
            SliderSetFromX(w, dpi, p.x);
            break;
        case WT::Button:
            w.pressed = true;
            break;
        default:
            break;
    }
}

void WidgetDrag(Widget& w, POINT p, int dpi)
{
    if (w.type == WT::Slider && w.dragging)
        SliderSetFromX(w, dpi, p.x);
}

void WidgetUp(Widget& w, POINT p, int /*dpi*/)
{
    if (!w.visible || !w.enabled) return;
    switch (w.type)
    {
        case WT::Slider:
            if (w.dragging)
            {
                w.dragging = false;
                if (w.onPreviewEnd) w.onPreviewEnd();
            }
            break;
        case WT::Button:
            if (w.pressed)
            {
                w.pressed = false;
                if (WidgetContains(w, p) && w.onClick) w.onClick();
            }
            break;
        case WT::Toggle:
            if (WidgetContains(w, p))
            {
                w.on = !w.on;
                if (w.onToggled) w.onToggled(w.on);
            }
            break;
        case WT::Radio:
            if (WidgetContains(w, p) && !w.on)
            {
                w.on = true;
                if (w.onToggled) w.onToggled(true);
            }
            break;
        default:
            break;
    }
}

// ── Text ──────────────────────────────────────────────────────────────────

void DrawTextG(Graphics& g, const std::wstring& text, int x, int y, int w, int h,
              int fontPx, bool bold, Color color, int align, bool vcenter, bool wrap)
{
    Font* f = GetFont(fontPx, bold);
    StringFormat sf;
    sf.SetAlignment(align == 0 ? StringAlignmentNear
                  : align == 1 ? StringAlignmentFar : StringAlignmentCenter);
    sf.SetLineAlignment(vcenter ? StringAlignmentCenter : StringAlignmentNear);
    if (!wrap)
    {
        sf.SetFormatFlags(StringFormatFlagsNoWrap);
        sf.SetTrimming(StringTrimmingEllipsisCharacter);
    }
    SolidBrush b(color);
    RectF rc((REAL)x, (REAL)y, (REAL)w, (REAL)h);
    g.DrawString(text.c_str(), -1, f, rc, &sf, &b);
}

// ── Vector icons (emoji render as tofu in the GDI+ text path) ─────────────

void DrawIconGlyph(Graphics& g, int id, float x, float y, float s, Color color)
{
    constexpr double kPi = 3.14159265358979;
    SolidBrush br(color);
    Pen pen(color, s * 0.10f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    float cx = x + s / 2.0f, cy = y + s / 2.0f;

    switch (id)
    {
        case IconThermo:
        {
            float bw = s * 0.24f, bulbR = s * 0.19f, bulbCy = y + s * 0.76f;
            FillRound(g, color, cx - bw / 2, y + s * 0.10f, bw, s * 0.62f, bw / 2);
            g.FillEllipse(&br, cx - bulbR, bulbCy - bulbR, bulbR * 2, bulbR * 2);
            break;
        }
        case IconSun:
        {
            float r = s * 0.19f;
            g.FillEllipse(&br, cx - r, cy - r, r * 2, r * 2);
            for (int i = 0; i < 8; ++i)
            {
                double a = i * 2.0 * kPi / 8.0;
                g.DrawLine(&pen,
                    cx + (float)cos(a) * s * 0.30f, cy + (float)sin(a) * s * 0.30f,
                    cx + (float)cos(a) * s * 0.45f, cy + (float)sin(a) * s * 0.45f);
            }
            break;
        }
        case IconClock:
        {
            float r = s * 0.42f;
            g.DrawEllipse(&pen, cx - r, cy - r, r * 2, r * 2);
            g.DrawLine(&pen, cx, cy, cx, cy - r * 0.52f);
            g.DrawLine(&pen, cx, cy, cx + r * 0.40f, cy + r * 0.12f);
            break;
        }
        case IconSwap:
        {
            float o = s * 0.20f, hd = s * 0.13f;
            float lx = x + s * 0.16f, rx = x + s * 0.84f;
            g.DrawLine(&pen, lx, cy - o, rx, cy - o);
            g.DrawLine(&pen, rx, cy - o, rx - hd, cy - o - hd);
            g.DrawLine(&pen, rx, cy - o, rx - hd, cy - o + hd);
            g.DrawLine(&pen, rx, cy + o, lx, cy + o);
            g.DrawLine(&pen, lx, cy + o, lx + hd, cy + o - hd);
            g.DrawLine(&pen, lx, cy + o, lx + hd, cy + o + hd);
            break;
        }
        case IconSliders:
        {
            float kr = s * 0.115f, lx = x + s * 0.13f, rx = x + s * 0.87f;
            g.DrawLine(&pen, lx, cy - s * 0.21f, rx, cy - s * 0.21f);
            g.FillEllipse(&br, x + s * 0.62f - kr, cy - s * 0.21f - kr, kr * 2, kr * 2);
            g.DrawLine(&pen, lx, cy + s * 0.21f, rx, cy + s * 0.21f);
            g.FillEllipse(&br, x + s * 0.34f - kr, cy + s * 0.21f - kr, kr * 2, kr * 2);
            break;
        }
        case IconPin:
        {
            float r = s * 0.27f, headCy = y + s * 0.36f;
            GraphicsPath p;
            p.AddArc(cx - r, headCy - r, r * 2, r * 2, 140.0f, 260.0f);
            p.AddLine(cx + (float)cos(40 * kPi / 180) * r,
                      headCy + (float)sin(40 * kPi / 180) * r,
                      cx, y + s * 0.95f);
            p.CloseFigure();
            g.FillPath(&br, &p);
            break;
        }
        case IconBed:
        {
            FillRound(g, color, x + s * 0.14f, y + s * 0.50f, s * 0.78f, s * 0.22f, s * 0.05f);
            g.FillRectangle(&br, x + s * 0.10f, y + s * 0.30f, s * 0.07f, s * 0.42f);
            FillRound(g, color, x + s * 0.22f, y + s * 0.40f, s * 0.24f, s * 0.12f, s * 0.04f);
            break;
        }
        case IconCalendar:
        {
            float bx = x + s * 0.16f, by = y + s * 0.26f;
            float bw = s * 0.68f, bh = s * 0.58f, rad = s * 0.09f;
            GraphicsPath body;
            body.AddArc(bx, by, rad * 2, rad * 2, 180, 90);
            body.AddArc(bx + bw - rad * 2, by, rad * 2, rad * 2, 270, 90);
            body.AddArc(bx + bw - rad * 2, by + bh - rad * 2, rad * 2, rad * 2, 0, 90);
            body.AddArc(bx, by + bh - rad * 2, rad * 2, rad * 2, 90, 90);
            body.CloseFigure();
            g.DrawPath(&pen, &body);
            g.DrawLine(&pen, bx, by + s * 0.17f, bx + bw, by + s * 0.17f);
            g.DrawLine(&pen, x + s * 0.35f, y + s * 0.13f, x + s * 0.35f, y + s * 0.31f);
            g.DrawLine(&pen, x + s * 0.65f, y + s * 0.13f, x + s * 0.65f, y + s * 0.31f);
            break;
        }
        case IconMoon:
        {
            // Crescent: a disc with an offset disc bitten out of the lit edge.
            float r = s * 0.36f;
            GraphicsPath outer;
            outer.AddEllipse(cx - r, cy - r, r * 2, r * 2);
            Region reg(&outer);
            GraphicsPath bite;
            float b2 = s * 0.33f;
            bite.AddEllipse(cx - b2 + s * 0.17f, cy - b2 - s * 0.06f, b2 * 2, b2 * 2);
            reg.Exclude(&bite);
            g.FillRegion(&br, &reg);
            break;
        }
    }
}

// ── Painting ──────────────────────────────────────────────────────────────

void PaintWidget(Graphics& g, int dpi, const Widget& w, POINT mouse, bool anyButtonDown)
{
    if (!w.visible) return;

    const RECT& b = w.bounds;
    const int bw = b.right - b.left;
    const int bh = b.bottom - b.top;
    const bool hover = WidgetContains(w, mouse);
    const std::wstring text = w.textProvider ? w.textProvider() : w.text;

    switch (w.type)
    {
        case WT::Label:
        {
            Color c = !w.enabled ? GC(Theme::TextDisabled)
                    : w.warn ? GC(RGB(0xE0, 0x90, 0x60))
                    : w.secondary ? GC(Theme::TextSecondary)
                                  : GC(Theme::TextPrimary);
            int tx = b.left;
            if (w.iconId >= 0)
            {
                float isz = (float)Theme::Scale(w.fontPx + 4, dpi);
                DrawIconGlyph(g, w.iconId, (float)b.left,
                              b.top + (bh - isz) / 2.0f, isz, GC(Theme::Accent()));
                tx = b.left + (int)isz + Theme::Scale(7, dpi);
            }
            DrawTextG(g, text, tx, b.top, b.right - tx, bh,
                      Theme::Scale(w.fontPx, dpi), w.bold, c, w.align, !w.wrap, w.wrap);
            break;
        }

        case WT::Separator:
        {
            Pen pen(GC(Theme::SeparatorLine), 1.0f);
            float y = b.top + bh / 2.0f;
            g.DrawLine(&pen, (REAL)b.left, y, (REAL)b.right, y);
            break;
        }

        case WT::Slider:
        {
            float left, width, cy;
            SliderTrack(w, dpi, left, width, cy);
            float trackH = (float)Theme::Scale(4, dpi);
            double f = (w.maxV > w.minV) ? (w.value - w.minV) / (w.maxV - w.minV) : 0;
            if (f < 0) f = 0; if (f > 1) f = 1;
            float thumbX = left + (float)f * width;

            FillRound(g, GC(Theme::TrackFill), left, cy - trackH / 2,
                      width, trackH, trackH / 2);
            FillRound(g, GC(Theme::Accent()), left, cy - trackH / 2,
                      thumbX - left, trackH, trackH / 2);

            float R = (float)Theme::Scale((hover || w.dragging) ? 10 : 9, dpi);
            SolidBrush white(GC(Theme::TextPrimary));
            g.FillEllipse(&white, thumbX - R, cy - R, R * 2, R * 2);
            Pen ring(GC(Theme::ControlBorder), 1.0f);
            g.DrawEllipse(&ring, thumbX - R, cy - R, R * 2, R * 2);
            float ir = (float)Theme::Scale((hover || w.dragging) ? 6 : 5, dpi);
            SolidBrush dot(GC(Theme::Accent()));
            g.FillEllipse(&dot, thumbX - ir, cy - ir, ir * 2, ir * 2);
            break;
        }

        case WT::Toggle:
        {
            float pillW = (float)Theme::Scale(40, dpi);
            float pillH = (float)Theme::Scale(20, dpi);
            float px = (float)b.left;
            float py = b.top + (bh - pillH) / 2.0f;
            float knobR = pillH / 2 - (float)Theme::Scale(3, dpi);

            if (w.on)
            {
                FillRound(g, GC(Theme::Accent()), px, py, pillW, pillH, pillH / 2);
                SolidBrush knob(GC(RGB(255, 255, 255)));
                float kx = px + pillW - pillH / 2;
                g.FillEllipse(&knob, kx - knobR, py + pillH / 2 - knobR, knobR * 2, knobR * 2);
            }
            else
            {
                StrokeRound(g, GC(Theme::TextSecondary), px, py, pillW, pillH, pillH / 2);
                SolidBrush knob(GC(Theme::TextSecondary));
                float kx = px + pillH / 2;
                g.FillEllipse(&knob, kx - knobR, py + pillH / 2 - knobR, knobR * 2, knobR * 2);
            }

            if (!text.empty())
                DrawTextG(g, text, (int)(px + pillW) + Theme::Scale(10, dpi), b.top,
                         bw - (int)pillW - Theme::Scale(10, dpi), bh,
                         Theme::Scale(14, dpi), false, GC(Theme::TextPrimary), 0, true);
            break;
        }

        case WT::Radio:
        {
            float d = (float)Theme::Scale(18, dpi);
            float cx = b.left + d / 2;
            float cy = b.top + bh / 2.0f;
            if (w.on)
            {
                SolidBrush fill(GC(Theme::Accent()));
                g.FillEllipse(&fill, cx - d / 2, cy - d / 2, d, d);
                float ir = d * 0.27f;
                SolidBrush inner(GC(Theme::Background));
                g.FillEllipse(&inner, cx - ir, cy - ir, ir * 2, ir * 2);
            }
            else
            {
                Pen ring(GC(Theme::TextSecondary), 1.4f);
                g.DrawEllipse(&ring, cx - d / 2, cy - d / 2, d, d);
            }
            DrawTextG(g, text, (int)(b.left + d) + Theme::Scale(8, dpi), b.top,
                     bw - (int)d - Theme::Scale(8, dpi), bh,
                     Theme::Scale(14, dpi), false, GC(Theme::TextPrimary), 0, true);
            break;
        }

        case WT::Button:
        {
            Color fill = w.pressed ? GC(Theme::Pressed)
                       : (hover && w.enabled) ? GC(Theme::Hover)
                                              : GC(Theme::ControlFill);
            FillRound(g, fill, (float)b.left, (float)b.top, (float)bw, (float)bh,
                      (float)Theme::Scale(5, dpi));
            StrokeRound(g, GC(Theme::ControlBorder), (float)b.left, (float)b.top,
                        (float)bw, (float)bh, (float)Theme::Scale(5, dpi));
            Color tc = w.enabled ? GC(Theme::TextPrimary) : GC(Theme::TextDisabled);
            DrawTextG(g, text, b.left, b.top, bw, bh,
                     Theme::Scale(14, dpi), false, tc, 2, true);
            break;
        }

        case WT::Progress:
        {
            float h = (float)Theme::Scale(6, dpi);
            float y = b.top + (bh - h) / 2.0f;
            FillRound(g, GC(Theme::TrackFill), (float)b.left, y, (float)bw, h, h / 2);
            double f = w.value; if (f < 0) f = 0; if (f > 1) f = 1;
            if (f > 0)
                FillRound(g, GC(Theme::Accent()), (float)b.left, y,
                          (float)(bw * f), h, h / 2);
            break;
        }

        case WT::Dropdown:
        {
            Color fill = (hover && w.enabled) ? GC(Theme::Hover) : GC(Theme::ControlFill);
            FillRound(g, fill, (float)b.left, (float)b.top, (float)bw, (float)bh,
                      (float)Theme::Scale(4, dpi));
            StrokeRound(g, GC(Theme::ControlBorder), (float)b.left, (float)b.top,
                        (float)bw, (float)bh, (float)Theme::Scale(4, dpi));
            std::wstring sel = (w.selected >= 0 && w.selected < (int)w.options.size())
                ? w.options[w.selected] : L"";
            DrawTextG(g, sel, b.left + Theme::Scale(10, dpi), b.top,
                     bw - Theme::Scale(34, dpi), bh, Theme::Scale(14, dpi), false,
                     GC(Theme::TextPrimary), 0, true);
            // Chevron.
            DrawTextG(g, L"\x25BE", b.right - Theme::Scale(24, dpi), b.top,
                     Theme::Scale(20, dpi), bh, Theme::Scale(10, dpi), false,
                     GC(Theme::TextSecondary), 2, true);
            break;
        }
    }
    (void)anyButtonDown;
}
