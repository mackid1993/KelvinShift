#include "TrayIconService.h"
#include "ScheduleEngine.h"
#include "SettingsService.h"
#include "GdiPlusInc.h"
#include "Controls.h"
#include "resource.h"
#include <shellapi.h>
#include <windowsx.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <cwchar>

using namespace Gdiplus;

namespace {

const wchar_t* kClassName = L"KelvinShiftTrayWnd";
const UINT WM_TRAYCALLBACK = WM_APP + 20;
const UINT kIconId = 1;

constexpr double kTwoPi = 6.28318530717958647692;

// ── Vector glyphs ─────────────────────────────────────────────────────────
// All glyphs target a 32x32 canvas centred on (16,16); the caller scales the
// Graphics context up to the real icon size. Verbatim port of the C# draws.

GraphicsPath* MakeRoundedRect(float x, float y, float w, float h, float r)
{
    auto* path = new GraphicsPath();
    r = (std::min)(r, (std::min)(w, h) / 2.0f);
    path->AddArc(x,             y,             r * 2, r * 2, 180, 90);
    path->AddArc(x + w - r * 2, y,             r * 2, r * 2, 270, 90);
    path->AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2,   0, 90);
    path->AddArc(x,             y + h - r * 2, r * 2, r * 2,  90, 90);
    path->CloseFigure();
    return path;
}

void DrawSun(Graphics& g, int size, const Color& color)
{
    float cx = size / 2.0f, cy = size / 2.0f;
    SolidBrush fill(color);
    Pen pen(color, 3.1f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);

    // Bigger, bolder disc + rays so the glyph reads solidly at tray size.
    float r = 6.5f;
    g.FillEllipse(&fill, cx - r, cy - r, r * 2, r * 2);

    const int rays = 8;
    float inner = 9.3f, outer = 14.4f;
    for (int i = 0; i < rays; ++i)
    {
        double a = i * kTwoPi / rays;
        g.DrawLine(&pen,
                   cx + (float)cos(a) * inner, cy + (float)sin(a) * inner,
                   cx + (float)cos(a) * outer, cy + (float)sin(a) * outer);
    }
}

void DrawMoon(Graphics& g, int size, const Color& color)
{
    float cx = size / 2.0f, cy = size / 2.0f;
    SolidBrush fill(color);

    // Crescent = outer disc with an offset disc bitten out of the lit edge.
    GraphicsPath outer;
    float outerR = 11.0f;
    outer.AddEllipse(cx - outerR, cy - outerR, outerR * 2, outerR * 2);
    Region region(&outer);

    GraphicsPath bite;
    float biteR = 10.0f, biteX = 5.0f, biteY = -2.5f;
    bite.AddEllipse(cx - biteR + biteX, cy - biteR + biteY, biteR * 2, biteR * 2);
    region.Exclude(&bite);

    g.FillRegion(&fill, &region);
}

void DrawBed(Graphics& g, int size, const Color& color)
{
    SolidBrush fill(color);

    // Mattress base (rounded rect).
    {
        std::unique_ptr<GraphicsPath> m(MakeRoundedRect(5.0f, 17.0f, size - 3.0f - 5.0f, 24.0f - 17.0f, 2.5f));
        g.FillPath(&fill, m.get());
    }
    // Pillow.
    {
        std::unique_ptr<GraphicsPath> p(MakeRoundedRect(8.5f, 13.5f, 16.5f - 8.5f, 17.0f - 13.5f, 1.5f));
        g.FillPath(&fill, p.get());
    }
    // Headboard post.
    g.FillRectangle(&fill, 4.0f, 8.0f, 3.0f, 16.0f);
    // Base feet.
    g.FillRectangle(&fill, 4.0f, 23.5f, 3.0f, 3.5f);
    g.FillRectangle(&fill, size - 7.0f, 23.5f, 3.0f, 3.5f);
}

void DrawSunMoon(Graphics& g, int size, const Color& color)
{
    GraphicsState st = g.Save();
    g.TranslateTransform(-size * 0.20f, size * 0.20f);
    g.ScaleTransform(0.75f, 0.75f);
    DrawSun(g, size, color);
    g.Restore(st);

    st = g.Save();
    g.TranslateTransform(size * 0.25f, -size * 0.25f);
    g.ScaleTransform(0.65f, 0.65f);
    DrawMoon(g, size, color);
    g.Restore(st);
}

void DrawSleepingMoon(Graphics& g, int size, const Color& moonColor, const Color& zColor)
{
    GraphicsState st = g.Save();
    g.TranslateTransform(-2.0f, 2.0f);
    g.ScaleTransform(0.85f, 0.85f);
    DrawMoon(g, size, moonColor);
    g.Restore(st);

    Pen pen(zColor, 2.6f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    float x = size * 0.55f, y = size * 0.10f, s = size * 0.30f;
    g.DrawLine(&pen, x,     y,     x + s, y);
    g.DrawLine(&pen, x + s, y,     x,     y + s);
    g.DrawLine(&pen, x,     y + s, x + s, y + s);
}

void DrawOff(Graphics& g, int size)
{
    // Universal power glyph — a muted warm orange that reads on light or
    // dark taskbars without theme detection.
    float cx = size / 2.0f, cy = size / 2.0f + 1.0f;
    Color color(0xFF, 0xE0, 0x90, 0x60);
    Pen pen(color, 2.6f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);

    float r = 9.0f;
    g.DrawArc(&pen, cx - r, cy - r, r * 2, r * 2, 300.0f, 300.0f);
    g.DrawLine(&pen, cx, cy - r - 2.0f, cx, cy);
}

// ── Label helpers (port of PhaseLabel / ScheduleLabel) ────────────────────

// Phase icon + text are kept separate so the menu can column-align them
// with the Day / Night / Bedtime rows below.
int PhaseIcon(SchedulePhase p)
{
    switch (p)
    {
        case SchedulePhase::Day:               return IconSun;
        case SchedulePhase::Night:             return IconMoon;
        case SchedulePhase::TransitionToNight: return IconMoon;
        case SchedulePhase::TransitionToDay:   return IconSun;
        case SchedulePhase::RampToBedtime:     return IconBed;
        case SchedulePhase::Bedtime:           return IconBed;
    }
    return -1;
}

const wchar_t* PhaseText(SchedulePhase p)
{
    switch (p)
    {
        case SchedulePhase::Day:               return L"Daytime";
        case SchedulePhase::Night:             return L"Nighttime";
        case SchedulePhase::TransitionToNight: return L"Transitioning to Night";
        case SchedulePhase::TransitionToDay:   return L"Transitioning to Day";
        case SchedulePhase::RampToBedtime:     return L"Ramping to Bedtime";
        case SchedulePhase::Bedtime:           return L"Bedtime";
    }
    return L"";
}

std::wstring FormatClock(int minutes)
{
    int h = (minutes / 60) % 24;
    int m = minutes % 60;
    int h12 = (h == 0) ? 12 : (h > 12 ? h - 12 : h);
    const wchar_t* sfx = (h >= 12) ? L"PM" : L"AM";
    wchar_t buf[16];
    wsprintfW(buf, L"%d:%02d %s", h12, m, sfx);
    return buf;
}

std::wstring ScheduleLabel(SettingsService& s, const ScheduleState& st)
{
    if (s.ScheduleMode() == "solar")
    {
        std::wstring rise = st.sunriseMin ? FormatClock(*st.sunriseMin) : L"–";
        std::wstring set  = st.sunsetMin  ? FormatClock(*st.sunsetMin)  : L"–";
        std::wstring bed  = s.BedtimeEnabled()
            ? L"   Bed " + Widen(s.BedtimeTimeLabel()) : L"";
        return L"Schedule: Solar  ↑" + rise + L"  ↓" + set + bed;
    }
    std::wstring bedSuf = s.BedtimeEnabled()
        ? L"  ·  Bed " + Widen(s.BedtimeTimeLabel()) : L"";
    return L"Schedule: " + Widen(s.DayTimeLabel()) + L" – "
         + Widen(s.NightTimeLabel()) + bedSuf;
}

int Pct(double brightness) { return (int)llround(brightness * 100.0); }

} // namespace

// ── Construction ──────────────────────────────────────────────────────────

TrayIconService::TrayIconService(ScheduleEngine& engine, SettingsService& settings,
                                 std::function<void()> openPrefs, std::function<void()> quit)
    : engine_(engine), settings_(settings),
      openPrefs_(std::move(openPrefs)), quit_(std::move(quit))
{
    fallbackIcon_ = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON));
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc{};
    wc.lpfnWndProc = &TrayIconService::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), this);

    // Re-render the glyph whenever the schedule state or settings change.
    engine_.StateChanged.Add([this](const ScheduleState&) { Refresh(); });
    settings_.SettingsChanged.Add([this] { Refresh(); });
}

TrayIconService::~TrayIconService()
{
    if (iconAdded_)
    {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = kIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    if (currentIcon_) DestroyIcon(currentIcon_);
    if (hwnd_) DestroyWindow(hwnd_);
}

void TrayIconService::Show()
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAYCALLBACK;
    nid.hIcon = fallbackIcon_;
    wcscpy_s(nid.szTip, L"KelvinShift");
    Shell_NotifyIconW(NIM_ADD, &nid);

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    iconAdded_ = true;

    Refresh();
}

// ── Refresh ───────────────────────────────────────────────────────────────

void TrayIconService::Refresh()
{
    const ScheduleState& s = engine_.State();

    HICON newIcon = RenderStateIcon(s);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kIconId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = newIcon ? newIcon : fallbackIcon_;

    std::wstring tip;
    if (s.enabled)
    {
        wchar_t buf[96];
        wsprintfW(buf, L"KelvinShift — %dK · %d%%",
                  s.currentKelvin, Pct(s.currentBrightness));
        tip = buf;
    }
    else
    {
        tip = L"KelvinShift — Off";
    }
    wcscpy_s(nid.szTip, tip.c_str());

    if (iconAdded_)
        Shell_NotifyIconW(NIM_MODIFY, &nid);

    if (currentIcon_) DestroyIcon(currentIcon_);
    currentIcon_ = newIcon; // may be null -> fallback stays shown
}

HICON TrayIconService::RenderStateIcon(const ScheduleState& s)
{
    // Render at 64x64 so Windows downscales with proper anti-aliasing for any
    // taskbar DPI; the glyph helpers target a virtual 32px canvas.
    const int size = 64;
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.ScaleTransform(size / 32.0f, size / 32.0f);
    const int v = 32;

    if (!s.enabled)
    {
        DrawOff(g, v);
    }
    else
    {
        // Warm-stable palette: colours already in the R/G plane so warm
        // gamma doesn't hue-shift them.
        Color sun(0xFF, 0xFF, 0xC8, 0x4A);          // warm gold
        Color moon(0xFF, 0xF0, 0xE0, 0xC0);         // warm cream
        Color bed(0xFF, 0xE8, 0x90, 0x58);          // warm amber
        Color transToNight(0xFF, 0xFF, 0xA8, 0x60); // warm orange
        Color transToDay(0xFF, 0xFF, 0xD8, 0x80);   // soft yellow

        switch (s.phase)
        {
            case SchedulePhase::Day:               DrawSun(g, v, sun); break;
            case SchedulePhase::Night:             DrawMoon(g, v, moon); break;
            case SchedulePhase::Bedtime:           DrawBed(g, v, bed); break;
            case SchedulePhase::TransitionToNight: DrawSunMoon(g, v, transToNight); break;
            case SchedulePhase::TransitionToDay:   DrawSunMoon(g, v, transToDay); break;
            case SchedulePhase::RampToBedtime:     DrawSleepingMoon(g, v, moon, bed); break;
        }
    }

    HICON hicon = nullptr;
    bmp.GetHICON(&hicon);
    return hicon;
}

// ── Menu ──────────────────────────────────────────────────────────────────

std::vector<MenuItem> TrayIconService::BuildMenuItems()
{
    const ScheduleState& s = engine_.State();
    std::vector<MenuItem> items;

    // One row builder: a glyph column, the label, and an optional value drawn
    // in an aligned right column. Every label starts at the same x, so the
    // Day / Night / Bedtime rows line up as a block.
    auto row = [](int icon, std::wstring label, std::wstring value)
    {
        MenuItem m;
        m.iconId = icon;
        m.text  = std::move(label);
        m.value = std::move(value);
        m.enabled = false;
        return m;
    };
    auto sep = []
    {
        MenuItem m;
        m.separator = true;
        return m;
    };

    wchar_t v[64];
    wsprintfW(v, L"%d K  ·  %d%%", s.currentKelvin, Pct(s.currentBrightness));
    items.push_back(row(-1, L"Current:", v));
    items.push_back(row(PhaseIcon(s.phase), PhaseText(s.phase), L""));
    items.push_back(sep());

    wsprintfW(v, L"%d K  ·  %d%%", s.dayKelvin, Pct(s.dayBrightness));
    items.push_back(row(IconSun, L"Day:", v));
    wsprintfW(v, L"%d K  ·  %d%%", s.nightKelvin, Pct(s.nightBrightness));
    items.push_back(row(IconMoon, L"Night:", v));
    if (s.bedtimeEnabled)
    {
        wsprintfW(v, L"%d K  ·  %d%%", s.bedtimeKelvin, Pct(s.bedtimeBrightness));
        items.push_back(row(IconBed, L"Bedtime:", v));
    }
    items.push_back(row(IconCalendar, ScheduleLabel(settings_, s), L""));
    items.push_back(sep());

    MenuItem enabled;
    enabled.text = L"Enabled";
    enabled.checkable = true;
    enabled.checked = settings_.Enabled();
    enabled.onClick = [this] { settings_.Enabled(!settings_.Enabled()); };
    items.push_back(std::move(enabled));
    items.push_back(sep());

    MenuItem prefs;
    prefs.text = L"Preferences…";
    prefs.onClick = [this] { if (openPrefs_) openPrefs_(); };
    items.push_back(std::move(prefs));

    MenuItem quit;
    quit.text = L"Quit KelvinShift";
    quit.onClick = [this] { if (quit_) quit_(); };
    items.push_back(std::move(quit));

    return items;
}

// ── Tray callback ─────────────────────────────────────────────────────────

void TrayIconService::OnTrayCallback(WPARAM wParam, LPARAM lParam)
{
    UINT evt = LOWORD(lParam);
    int x = GET_X_LPARAM(wParam);
    int y = GET_Y_LPARAM(wParam);

    if (evt == WM_LBUTTONUP || evt == NIN_SELECT || evt == NIN_KEYSELECT)
    {
        if (openPrefs_) openPrefs_();
    }
    else if (evt == WM_CONTEXTMENU || evt == WM_RBUTTONUP)
    {
        menu_.Track(BuildMenuItems(), x, y);
    }
}

LRESULT CALLBACK TrayIconService::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto* self = reinterpret_cast<TrayIconService*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    if (msg == WM_TRAYCALLBACK)
    {
        self->OnTrayCallback(wParam, lParam);
        return 0;
    }
    if (msg == self->taskbarCreatedMsg_ && self->taskbarCreatedMsg_ != 0)
    {
        // Explorer restarted — re-add the icon.
        self->iconAdded_ = false;
        self->Show();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Debug: dump every phase glyph to PNG ──────────────────────────────────

void TrayIconService::DumpIcons(const std::wstring& dir)
{
    CLSID png{};
    UINT num = 0, sz = 0;
    GetImageEncodersSize(&num, &sz);
    if (sz == 0) return;
    std::vector<BYTE> buf(sz);
    auto* info = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(num, sz, info);
    bool found = false;
    for (UINT i = 0; i < num && !found; ++i)
        if (wcscmp(info[i].MimeType, L"image/png") == 0) { png = info[i].Clsid; found = true; }
    if (!found) return;

    struct P { const wchar_t* name; SchedulePhase phase; bool enabled; };
    const P phases[] = {
        { L"day",        SchedulePhase::Day,               true  },
        { L"night",      SchedulePhase::Night,             true  },
        { L"bed",        SchedulePhase::Bedtime,           true  },
        { L"transNight", SchedulePhase::TransitionToNight, true  },
        { L"transDay",   SchedulePhase::TransitionToDay,   true  },
        { L"ramp",       SchedulePhase::RampToBedtime,     true  },
        { L"off",        SchedulePhase::Day,               false },
    };

    for (const P& p : phases)
    {
        const int S = 256;
        Bitmap bmp(S, S, PixelFormat32bppARGB);
        Graphics g(&bmp);
        g.Clear(Color(255, 48, 48, 48));
        Pen cross(Color(130, 255, 70, 70), 1.0f);   // centre crosshair
        g.DrawLine(&cross, S / 2.0f, 0.0f, S / 2.0f, (REAL)S);
        g.DrawLine(&cross, 0.0f, S / 2.0f, (REAL)S, S / 2.0f);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.ScaleTransform(S / 32.0f, S / 32.0f);

        Color sun(0xFF, 0xFF, 0xC8, 0x4A), moon(0xFF, 0xF0, 0xE0, 0xC0);
        Color bed(0xFF, 0xE8, 0x90, 0x58);
        Color tNight(0xFF, 0xFF, 0xA8, 0x60), tDay(0xFF, 0xFF, 0xD8, 0x80);
        if (!p.enabled)
            DrawOff(g, 32);
        else switch (p.phase)
        {
            case SchedulePhase::Day:               DrawSun(g, 32, sun); break;
            case SchedulePhase::Night:             DrawMoon(g, 32, moon); break;
            case SchedulePhase::Bedtime:           DrawBed(g, 32, bed); break;
            case SchedulePhase::TransitionToNight: DrawSunMoon(g, 32, tNight); break;
            case SchedulePhase::TransitionToDay:   DrawSunMoon(g, 32, tDay); break;
            case SchedulePhase::RampToBedtime:     DrawSleepingMoon(g, 32, moon, bed); break;
        }
        std::wstring path = dir + L"\\icon_" + p.name + L".png";
        bmp.Save(path.c_str(), &png);
    }
}
