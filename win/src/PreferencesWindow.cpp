#include "PreferencesWindow.h"
#include "SettingsService.h"
#include "ScheduleEngine.h"
#include "GammaRangeService.h"
#include "LocationService.h"
#include "Theme.h"
#include "resource.h"

#include <commctrl.h>
#include <windowsx.h>
#include <thread>
#include <cmath>
#include <cstdio>

using namespace Gdiplus;

namespace {

const wchar_t* kClassName = L"KelvinShiftPrefsWnd";
const UINT WM_KS_LOCATION = WM_APP + 5;
const UINT_PTR kScrollTimer = 1;

// Widget ids — index into PreferencesWindow::w_.
enum {
    W_T_DAY_L, W_T_DAY_S, W_T_DAY_U,
    W_T_NIGHT_L, W_T_NIGHT_S, W_T_NIGHT_U,
    W_T_BED_L, W_T_BED_S, W_T_BED_U,
    W_B_DAY_L, W_B_DAY_S, W_B_DAY_V,
    W_B_NIGHT_L, W_B_NIGHT_S, W_B_NIGHT_V,
    W_B_BED_L, W_B_BED_S, W_B_BED_V,
    W_B_FOOT,
    W_S_SOLAR, W_S_CUSTOM,
    W_S_LAT_L, W_S_LON_L, W_S_LOCBTN, W_S_LOCERR, W_S_LOCNAME,
    W_S_DAY_L, W_S_DAY_H, W_S_DAY_C, W_S_DAY_M, W_S_DAY_AP,
    W_S_NIGHT_L, W_S_NIGHT_H, W_S_NIGHT_C, W_S_NIGHT_M, W_S_NIGHT_AP,
    W_S_SEP, W_S_BEDTOG,
    W_S_BED_L, W_S_BED_H, W_S_BED_C, W_S_BED_M, W_S_BED_AP,
    W_S_BEDHELP,
    W_TR_DN_L, W_TR_DN_S, W_TR_DN_V,
    W_TR_NB_L, W_TR_NB_S, W_TR_NB_V,
    W_TR_HELP, W_TR_DEMOBTN, W_TR_PROG, W_TR_DEMOPCT, W_TR_FOOT,
    W_G_ENABLED, W_G_STARTUP, W_G_SEP, W_G_EXTRANGE, W_G_EXTHELP, W_G_EXTSTATUS,
    W_FOOTER_ATTR,
    W_COUNT
};

// Field indices into PreferencesWindow::fields_.
enum { FI_DAYK, FI_NIGHTK, FI_BEDK, FI_LAT, FI_LON, FI_COUNT };
const int kFieldId[FI_COUNT] = { 1001, 1002, 1003, 1004, 1005 };

struct LocResult { bool ok = false; double lat = 0; double lon = 0; std::string name; };

int ToHour12(int h24) { return h24 == 0 ? 12 : (h24 > 12 ? h24 - 12 : h24); }
int ToHour24(int h12, bool pm)
{
    if (h12 == 12) return pm ? 12 : 0;
    return pm ? h12 + 12 : h12;
}

std::wstring PctText(double v) { return std::to_wstring((int)std::lround(v * 100)) + L"%"; }

} // namespace

// ── Construction ──────────────────────────────────────────────────────────

PreferencesWindow::PreferencesWindow(SettingsService& settings, ScheduleEngine& engine)
    : settings_(settings), engine_(engine)
{
    appIcon_ = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON));

    WNDCLASSW wc{};
    wc.lpfnWndProc = &PreferencesWindow::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.hIcon = appIcon_;
    RegisterClassW(&wc);

    // Standard Win11 frame — Windows draws the title bar (with a real Mica
    // blur) and the caption buttons, crisply and correctly. WS_CLIPCHILDREN
    // keeps our content paint from overdrawing the numeric EDIT children,
    // which is what made them flicker.
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    hwnd_ = CreateWindowExW(0, kClassName, L"KelvinShift", style,
                            CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                            nullptr, nullptr, GetModuleHandleW(nullptr), this);

    dpi_ = Theme::Dpi(hwnd_);
    Theme::EnableBackdrop(hwnd_, Theme::BackdropMica); // dark + Mica title bar

    editBrush_ = CreateSolidBrush(Theme::ControlFill);
    RecreateEditFont();

    // Numeric EDIT children — created once, repositioned on scroll / resize.
    for (int i = 0; i < FI_COUNT; ++i)
    {
        NumField f;
        f.id = kFieldId[i];
        f.isInt = (i == FI_DAYK || i == FI_NIGHTK || i == FI_BEDK);
        DWORD es = WS_CHILD | ES_AUTOHSCROLL | ES_LEFT;
        f.hwnd = CreateWindowExW(0, L"EDIT", L"", es, 0, 0, 10, 10,
                                 hwnd_, (HMENU)(INT_PTR)f.id,
                                 GetModuleHandleW(nullptr), nullptr);
        SendMessageW(f.hwnd, WM_SETFONT, (WPARAM)editFont_, TRUE);
        SetWindowSubclass(f.hwnd, &PreferencesWindow::EditProc, f.id, (DWORD_PTR)this);
        fields_.push_back(f);
    }

    BuildUI();
    SyncFromSettings();

    engine_.DemoProgressChanged.Add([this](double p) {
        if (!hwnd_) return;
        w_[W_TR_PROG].value = p;
        if (IsWindowVisible(hwnd_)) SyncFromSettings();
    });
    settings_.SettingsChanged.Add([this] {
        if (hwnd_ && IsWindowVisible(hwnd_)) SyncFromSettings();
    });
}

PreferencesWindow::~PreferencesWindow()
{
    if (editFont_) DeleteObject(editFont_);
    if (editBrush_) DeleteObject(editBrush_);
    if (hwnd_) DestroyWindow(hwnd_);
}

void PreferencesWindow::RecreateEditFont()
{
    if (editFont_) DeleteObject(editFont_);
    editFont_ = CreateFontW(-Theme::Scale(14, dpi_), 0, 0, 0, FW_NORMAL,
                            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    for (auto& f : fields_)
        if (f.hwnd) SendMessageW(f.hwnd, WM_SETFONT, (WPARAM)editFont_, TRUE);
}

// ── UI definition ─────────────────────────────────────────────────────────

void PreferencesWindow::BuildUI()
{
    w_.clear();
    w_.resize(W_COUNT);

    auto label = [&](int id, const wchar_t* t, int px = 14, bool sec = false) {
        Widget& x = w_[id];
        x.type = WT::Label; x.text = t; x.fontPx = px; x.secondary = sec;
    };
    auto slider = [&](int id, double mn, double mx, double st) {
        Widget& x = w_[id];
        x.type = WT::Slider; x.minV = mn; x.maxV = mx; x.step = st;
    };

    // ── Card 0: Color Temperature ─────────────────────────────────────────
    label(W_T_DAY_L, L"Day");   label(W_T_NIGHT_L, L"Night"); label(W_T_BED_L, L"Bed");
    label(W_T_DAY_U, L"K", 14, true); label(W_T_NIGHT_U, L"K", 14, true);
    label(W_T_BED_U, L"K", 14, true);
    w_[W_T_DAY_U].align = w_[W_T_NIGHT_U].align = w_[W_T_BED_U].align = 0;

    struct KRow { int sid; int (SettingsService::*get)() const; void (SettingsService::*set)(int); };
    auto kelvinSlider = [&](int sid, std::function<int()> get, std::function<void(int)> set) {
        slider(sid, 1000, 6500, 50);
        w_[sid].onPreviewStart = [this, get] { engine_.StartPreview(get()); };
        w_[sid].onChanged = [this, set, get](double v) { set((int)v); engine_.UpdatePreview(get()); };
        w_[sid].onPreviewEnd = [this] { engine_.StopPreview(); };
        w_[sid].sync = [this, sid, get] { w_[sid].value = get(); };
    };
    kelvinSlider(W_T_DAY_S, [this] { return settings_.DayKelvin(); },
                            [this](int v) { settings_.DayKelvin(v); });
    kelvinSlider(W_T_NIGHT_S, [this] { return settings_.NightKelvin(); },
                              [this](int v) { settings_.NightKelvin(v); });
    kelvinSlider(W_T_BED_S, [this] { return settings_.BedtimeKelvin(); },
                            [this](int v) { settings_.BedtimeKelvin(v); });

    // ── Card 1: Brightness ────────────────────────────────────────────────
    label(W_B_DAY_L, L"Day"); label(W_B_NIGHT_L, L"Night"); label(W_B_BED_L, L"Bed");
    auto brightSlider = [&](int sid, int vid, std::function<double()> get,
                            std::function<void(double)> set) {
        slider(sid, 0.1, 1.0, 0.05);
        w_[sid].onPreviewStart = [this, get] { engine_.StartBrightnessPreview(get()); };
        w_[sid].onChanged = [this, set, get](double v) {
            set(v); engine_.UpdateBrightnessPreview(get());
        };
        w_[sid].onPreviewEnd = [this] { engine_.StopPreview(); };
        w_[sid].sync = [this, sid, get] { w_[sid].value = get(); };
        w_[vid].type = WT::Label; w_[vid].align = 1;
        w_[vid].textProvider = [get] { return PctText(get()); };
    };
    brightSlider(W_B_DAY_S, W_B_DAY_V, [this] { return settings_.DayBrightness(); },
                                       [this](double v) { settings_.DayBrightness(v); });
    brightSlider(W_B_NIGHT_S, W_B_NIGHT_V, [this] { return settings_.NightBrightness(); },
                                           [this](double v) { settings_.NightBrightness(v); });
    brightSlider(W_B_BED_S, W_B_BED_V, [this] { return settings_.BedtimeBrightness(); },
                                       [this](double v) { settings_.BedtimeBrightness(v); });
    label(W_B_FOOT, L"Dims the screen via gamma — does not affect backlight", 11, true);

    // ── Card 2: Schedule ──────────────────────────────────────────────────
    w_[W_S_SOLAR].type = WT::Radio;  w_[W_S_SOLAR].text = L"Solar (sunrise / sunset)";
    w_[W_S_CUSTOM].type = WT::Radio; w_[W_S_CUSTOM].text = L"Custom times";
    w_[W_S_SOLAR].onToggled  = [this](bool) { settings_.ScheduleMode("solar"); };
    w_[W_S_CUSTOM].onToggled = [this](bool) { settings_.ScheduleMode("custom"); };
    w_[W_S_SOLAR].sync  = [this] { w_[W_S_SOLAR].on  = settings_.ScheduleMode() == "solar"; };
    w_[W_S_CUSTOM].sync = [this] { w_[W_S_CUSTOM].on = settings_.ScheduleMode() == "custom"; };

    label(W_S_LAT_L, L"Latitude", 11, true);
    label(W_S_LON_L, L"Longitude", 11, true);
    w_[W_S_LOCBTN].type = WT::Button; w_[W_S_LOCBTN].text = L"Use Current Location";
    w_[W_S_LOCBTN].onClick = [this] {
        if (locating_) return;
        locating_ = true;
        w_[W_S_LOCBTN].enabled = false;
        locationError_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
        HWND h = hwnd_;
        std::thread([h] {
            auto r = LocationService::GetCurrent();
            auto* res = new LocResult{};
            res->ok = r.has_value();
            if (r)
            {
                res->lat = r->first;
                res->lon = r->second;
                res->name = LocationService::DescribeLocation(r->first, r->second);
            }
            PostMessageW(h, WM_KS_LOCATION, 0, (LPARAM)res);
        }).detach();
    };
    label(W_S_LOCERR, L"", 11);
    w_[W_S_LOCERR].warn = true; w_[W_S_LOCERR].wrap = true;
    w_[W_S_LOCERR].textProvider = [this] { return locationError_; };
    w_[W_S_LOCERR].visibleIf = [this] {
        return settings_.ScheduleMode() == "solar" && !locationError_.empty();
    };

    label(W_S_LOCNAME, L"", 12, true);
    w_[W_S_LOCNAME].iconId = IconPin;
    w_[W_S_LOCNAME].textProvider = [this] {
        return Widen(settings_.LocationName());
    };
    w_[W_S_LOCNAME].visibleIf = [this] {
        return settings_.ScheduleMode() == "solar" && !settings_.LocationName().empty();
    };

    // Solar fields visible only in solar mode.
    auto solarVis = [this] { return settings_.ScheduleMode() == "solar"; };
    for (int id : { W_S_LAT_L, W_S_LON_L, W_S_LOCBTN })
        w_[id].visibleIf = solarVis;
    fields_[FI_LAT].visibleIf = solarVis;
    fields_[FI_LON].visibleIf = solarVis;
    fields_[FI_LAT].read  = [this] { return settings_.Latitude(); };
    fields_[FI_LAT].commit = [this](double v) { settings_.Latitude(v); };
    fields_[FI_LON].read  = [this] { return settings_.Longitude(); };
    fields_[FI_LON].commit = [this](double v) { settings_.Longitude(v); };

    label(W_S_DAY_L, L"Day starts", 11, true);
    label(W_S_NIGHT_L, L"Night starts", 11, true);
    label(W_S_BED_L, L"Bedtime", 11, true);
    label(W_S_DAY_C, L":"); label(W_S_NIGHT_C, L":"); label(W_S_BED_C, L":");
    w_[W_S_DAY_C].align = w_[W_S_NIGHT_C].align = w_[W_S_BED_C].align = 2;

    auto hourOpts = std::vector<std::wstring>();
    for (int i = 1; i <= 12; ++i) hourOpts.push_back(std::to_wstring(i));
    auto minOpts = std::vector<std::wstring>();
    for (int i = 0; i < 60; i += 5)
    {
        wchar_t b[8]; wsprintfW(b, L"%02d", i); minOpts.push_back(b);
    }
    std::vector<std::wstring> apOpts = { L"AM", L"PM" };

    auto timeGroup = [&](int hId, int cId, int mId, int apId,
                         std::function<int()> getHour, std::function<void(int)> setHour,
                         std::function<int()> getMin,  std::function<void(int)> setMin)
    {
        w_[hId].type = WT::Dropdown;  w_[hId].options = hourOpts;
        w_[mId].type = WT::Dropdown;  w_[mId].options = minOpts;
        w_[apId].type = WT::Dropdown; w_[apId].options = apOpts;
        (void)cId;
        w_[hId].onPick = [this, getHour, setHour](int i) {
            bool pm = getHour() >= 12;
            setHour(ToHour24(i + 1, pm));
        };
        w_[mId].onPick = [this, setMin](int i) { setMin(i * 5); };
        w_[apId].onPick = [this, getHour, setHour](int i) {
            setHour(ToHour24(ToHour12(getHour()), i == 1));
        };
        w_[hId].sync  = [this, hId, getHour] { w_[hId].selected = ToHour12(getHour()) - 1; };
        w_[mId].sync  = [this, mId, getMin] {
            int s = (getMin() / 5); if (s < 0) s = 0; if (s > 11) s = 11;
            w_[mId].selected = s;
        };
        w_[apId].sync = [this, apId, getHour] { w_[apId].selected = getHour() >= 12 ? 1 : 0; };
    };
    timeGroup(W_S_DAY_H, W_S_DAY_C, W_S_DAY_M, W_S_DAY_AP,
              [this] { return settings_.CustomDayHour(); },
              [this](int v) { settings_.CustomDayHour(v); },
              [this] { return settings_.CustomDayMinute(); },
              [this](int v) { settings_.CustomDayMinute(v); });
    timeGroup(W_S_NIGHT_H, W_S_NIGHT_C, W_S_NIGHT_M, W_S_NIGHT_AP,
              [this] { return settings_.CustomNightHour(); },
              [this](int v) { settings_.CustomNightHour(v); },
              [this] { return settings_.CustomNightMinute(); },
              [this](int v) { settings_.CustomNightMinute(v); });
    timeGroup(W_S_BED_H, W_S_BED_C, W_S_BED_M, W_S_BED_AP,
              [this] { return settings_.BedtimeHour(); },
              [this](int v) { settings_.BedtimeHour(v); },
              [this] { return settings_.BedtimeMinute(); },
              [this](int v) { settings_.BedtimeMinute(v); });

    auto customVis = [this] { return settings_.ScheduleMode() == "custom"; };
    for (int id : { W_S_DAY_L, W_S_DAY_H, W_S_DAY_C, W_S_DAY_M, W_S_DAY_AP,
                    W_S_NIGHT_L, W_S_NIGHT_H, W_S_NIGHT_C, W_S_NIGHT_M, W_S_NIGHT_AP })
        w_[id].visibleIf = customVis;

    w_[W_S_SEP].type = WT::Separator;
    w_[W_S_BEDTOG].type = WT::Toggle;
    w_[W_S_BEDTOG].text = L"Enable bedtime ramp";
    w_[W_S_BEDTOG].onToggled = [this](bool v) { settings_.BedtimeEnabled(v); };
    w_[W_S_BEDTOG].sync = [this] { w_[W_S_BEDTOG].on = settings_.BedtimeEnabled(); };

    auto bedVis = [this] { return settings_.BedtimeEnabled(); };
    for (int id : { W_S_BED_L, W_S_BED_H, W_S_BED_C, W_S_BED_M, W_S_BED_AP })
        w_[id].visibleIf = bedVis;
    label(W_S_BEDHELP,
          L"After the night setting is reached, the display ramps to the Bed "
          L"temperature/brightness above. Ramp duration is under Transitions.",
          11, true);
    w_[W_S_BEDHELP].wrap = true;
    w_[W_S_BEDHELP].visibleIf = bedVis;

    // ── Card 3: Transitions ───────────────────────────────────────────────
    label(W_TR_DN_L, L"Day↔Night", 11); w_[W_TR_DN_L].secondary = true;
    label(W_TR_NB_L, L"Night→Bed", 11); w_[W_TR_NB_L].secondary = true;
    slider(W_TR_DN_S, 1, 180, 1);
    w_[W_TR_DN_S].onChanged = [this](double v) { settings_.TransitionMinutes((int)v); };
    w_[W_TR_DN_S].sync = [this] { w_[W_TR_DN_S].value = settings_.TransitionMinutes(); };
    slider(W_TR_NB_S, 1, 180, 1);
    w_[W_TR_NB_S].onChanged = [this](double v) { settings_.BedtimeRampMinutes((int)v); };
    w_[W_TR_NB_S].sync = [this] { w_[W_TR_NB_S].value = settings_.BedtimeRampMinutes(); };
    w_[W_TR_DN_V].type = WT::Label; w_[W_TR_DN_V].align = 1;
    w_[W_TR_DN_V].textProvider = [this] {
        return std::to_wstring(settings_.TransitionMinutes()) + L" min";
    };
    w_[W_TR_NB_V].type = WT::Label; w_[W_TR_NB_V].align = 1;
    w_[W_TR_NB_V].textProvider = [this] {
        return std::to_wstring(settings_.BedtimeRampMinutes()) + L" min";
    };
    for (int id : { W_TR_NB_L, W_TR_NB_S, W_TR_NB_V })
        w_[id].visibleIf = bedVis;

    label(W_TR_HELP,
          L"Smooth transition between phases. Bedtime ramp is clamped to the "
          L"night→bedtime window if too long.", 11, true);
    w_[W_TR_HELP].wrap = true;
    w_[W_TR_DEMOBTN].type = WT::Button;
    w_[W_TR_DEMOBTN].textProvider = [this] {
        return engine_.IsDemoRunning() ? std::wstring(L"Stop")
                                       : std::wstring(L"Preview Cycle");
    };
    w_[W_TR_DEMOBTN].onClick = [this] {
        if (engine_.IsDemoRunning()) engine_.StopDemo(); else engine_.StartDemo();
    };
    w_[W_TR_PROG].type = WT::Progress;
    w_[W_TR_PROG].visibleIf = [this] { return engine_.IsDemoRunning(); };
    w_[W_TR_DEMOPCT].type = WT::Label;
    w_[W_TR_DEMOPCT].textProvider = [this] {
        return std::to_wstring((int)std::lround(w_[W_TR_PROG].value * 100)) + L"%";
    };
    w_[W_TR_DEMOPCT].visibleIf = [this] { return engine_.IsDemoRunning(); };
    label(W_TR_FOOT, L"Runs through a full day / night cycle.", 11, true);

    // ── Card 4: General ───────────────────────────────────────────────────
    w_[W_G_ENABLED].type = WT::Toggle; w_[W_G_ENABLED].text = L"Enabled";
    w_[W_G_ENABLED].onToggled = [this](bool v) { settings_.Enabled(v); };
    w_[W_G_ENABLED].sync = [this] { w_[W_G_ENABLED].on = settings_.Enabled(); };

    w_[W_G_STARTUP].type = WT::Toggle; w_[W_G_STARTUP].text = L"Start with Windows";
    w_[W_G_STARTUP].onToggled = [this](bool v) { settings_.LaunchAtLogin(v); };
    w_[W_G_STARTUP].sync = [this] { w_[W_G_STARTUP].on = settings_.LaunchAtLogin(); };

    w_[W_G_SEP].type = WT::Separator;
    w_[W_G_EXTRANGE].type = WT::Toggle;
    w_[W_G_EXTRANGE].text = L"Allow warm temperatures below 3500K";
    w_[W_G_EXTRANGE].onToggled = [this](bool v) {
        if (GammaRangeService::RequestChange(v))
            extRangeStatus_ = v
                ? L"Enabled. Sign out and back in (or reboot) for full effect."
                : L"Disabled. Sign out and back in to revert.";
        w_[W_G_EXTRANGE].on = GammaRangeService::IsEnabled();
        SyncFromSettings();
    };
    w_[W_G_EXTRANGE].sync = [this] { w_[W_G_EXTRANGE].on = GammaRangeService::IsEnabled(); };
    label(W_G_EXTHELP,
          L"Optional. Below ~3500K Windows clips the color temperature (a "
          L"readability safeguard), so the warmest Night / Bedtime settings "
          L"can't fully apply. This writes a registry value (UAC prompt) that "
          L"lifts the cap — turning it back off, or uninstalling, removes the "
          L"value. Sign out or reboot for a change to take effect.", 11, true);
    w_[W_G_EXTHELP].wrap = true;
    label(W_G_EXTSTATUS, L"", 11, true);
    w_[W_G_EXTSTATUS].wrap = true;
    w_[W_G_EXTSTATUS].textProvider = [this] { return extRangeStatus_; };
    w_[W_G_EXTSTATUS].visibleIf = [this] { return !extRangeStatus_.empty(); };

    // Quiet "Developed by David Brustein" line in the bottom-right of the
    // scrolling content — mirrors the macOS Preferences footer.
    label(W_FOOTER_ATTR, L"Developed by David Brustein", 11, true);
    w_[W_FOOTER_ATTR].align = 1;

    // Kelvin field read/commit.
    fields_[FI_DAYK].read = [this] { return (double)settings_.DayKelvin(); };
    fields_[FI_DAYK].commit = [this](double v) { settings_.DayKelvin((int)v); };
    fields_[FI_NIGHTK].read = [this] { return (double)settings_.NightKelvin(); };
    fields_[FI_NIGHTK].commit = [this](double v) { settings_.NightKelvin((int)v); };
    fields_[FI_BEDK].read = [this] { return (double)settings_.BedtimeKelvin(); };
    fields_[FI_BEDK].commit = [this](double v) { settings_.BedtimeKelvin((int)v); };
    fields_[FI_BEDK].visibleIf = bedVis;

    // Bed rows in the temp/brightness cards follow the bedtime toggle.
    for (int id : { W_T_BED_L, W_T_BED_S, W_T_BED_U,
                    W_B_BED_L, W_B_BED_S, W_B_BED_V })
        w_[id].visibleIf = bedVis;

    // Card metadata + paint order.
    cards_ = {
        { {}, L"Color Temperature", IconThermo },
        { {}, L"Brightness",        IconSun },
        { {}, L"Schedule",          IconCalendar },
        { {}, L"Transitions",       IconSwap },
        { {}, L"General",           IconSliders },
    };
    cardIds_ = {
        { W_T_DAY_L, W_T_DAY_S, W_T_DAY_U, W_T_NIGHT_L, W_T_NIGHT_S, W_T_NIGHT_U,
          W_T_BED_L, W_T_BED_S, W_T_BED_U },
        { W_B_DAY_L, W_B_DAY_S, W_B_DAY_V, W_B_NIGHT_L, W_B_NIGHT_S, W_B_NIGHT_V,
          W_B_BED_L, W_B_BED_S, W_B_BED_V, W_B_FOOT },
        { W_S_SOLAR, W_S_CUSTOM, W_S_LAT_L, W_S_LON_L, W_S_LOCBTN, W_S_LOCERR,
          W_S_LOCNAME,
          W_S_DAY_L, W_S_DAY_H, W_S_DAY_C, W_S_DAY_M, W_S_DAY_AP,
          W_S_NIGHT_L, W_S_NIGHT_H, W_S_NIGHT_C, W_S_NIGHT_M, W_S_NIGHT_AP,
          W_S_SEP, W_S_BEDTOG, W_S_BED_L, W_S_BED_H, W_S_BED_C, W_S_BED_M,
          W_S_BED_AP, W_S_BEDHELP },
        { W_TR_DN_L, W_TR_DN_S, W_TR_DN_V, W_TR_NB_L, W_TR_NB_S, W_TR_NB_V,
          W_TR_HELP, W_TR_DEMOBTN, W_TR_PROG, W_TR_DEMOPCT, W_TR_FOOT },
        { W_G_ENABLED, W_G_STARTUP, W_G_SEP, W_G_EXTRANGE, W_G_EXTHELP, W_G_EXTSTATUS },
    };
    built_ = true;
}

// ── Layout ────────────────────────────────────────────────────────────────

void PreferencesWindow::Layout()
{
    if (!built_) return;
    auto S = [&](int v) { return Theme::Scale(v, dpi_); };

    RECT cr; GetClientRect(hwnd_, &cr);
    int clientW = cr.right;
    int cardL = S(20), cardR = clientW - S(20);
    int innerL = cardL + S(16), innerR = cardR - S(16);
    int y = S(10);
    const int rowH = S(40);

    auto cardTitleSpace = S(46);

    auto sliderRowWithField = [&](int L, int Sl, int U, int fld, int& gy) {
        if (!w_[Sl].visible) return;
        w_[L].bounds = { innerL, gy, innerL + S(52), gy + rowH };
        w_[U].bounds = { innerR - S(18), gy, innerR, gy + rowH };
        int fR = innerR - S(24), fW = S(72), fL = fR - fW;
        fields_[fld].rect = { fL, gy + S(6), fR, gy + rowH - S(6) };
        w_[Sl].bounds = { innerL + S(60), gy, fL - S(12), gy + rowH };
        gy += rowH + S(2);
    };
    auto sliderRowWithLabel = [&](int L, int Sl, int V, int& gy, int labelW) {
        if (!w_[Sl].visible) return;
        w_[L].bounds = { innerL, gy, innerL + labelW, gy + rowH };
        w_[V].bounds = { innerR - S(70), gy, innerR, gy + rowH };
        w_[Sl].bounds = { innerL + labelW + S(8), gy, innerR - S(82), gy + rowH };
        gy += rowH + S(2);
    };

    // Card 0 — Color Temperature.
    {
        int top = y, gy = top + cardTitleSpace;
        sliderRowWithField(W_T_DAY_L, W_T_DAY_S, W_T_DAY_U, FI_DAYK, gy);
        sliderRowWithField(W_T_NIGHT_L, W_T_NIGHT_S, W_T_NIGHT_U, FI_NIGHTK, gy);
        sliderRowWithField(W_T_BED_L, W_T_BED_S, W_T_BED_U, FI_BEDK, gy);
        cards_[0].r = { cardL, top, cardR, gy + S(10) };
        y = cards_[0].r.bottom + S(12);
    }
    // Card 1 — Brightness.
    {
        int top = y, gy = top + cardTitleSpace;
        sliderRowWithLabel(W_B_DAY_L, W_B_DAY_S, W_B_DAY_V, gy, S(52));
        sliderRowWithLabel(W_B_NIGHT_L, W_B_NIGHT_S, W_B_NIGHT_V, gy, S(52));
        sliderRowWithLabel(W_B_BED_L, W_B_BED_S, W_B_BED_V, gy, S(52));
        w_[W_B_FOOT].bounds = { innerL, gy + S(2), innerR, gy + S(2) + S(20) };
        cards_[1].r = { cardL, top, cardR, gy + S(2) + S(20) + S(10) };
        y = cards_[1].r.bottom + S(12);
    }
    // Card 2 — Schedule.
    {
        int top = y, gy = top + cardTitleSpace;
        w_[W_S_SOLAR].bounds  = { innerL, gy, innerL + S(220), gy + S(26) };
        w_[W_S_CUSTOM].bounds = { innerL + S(232), gy, innerR, gy + S(26) };
        gy += S(38);

        if (settings_.ScheduleMode() == "solar")
        {
            int fw = S(150);                  // sensible lat/lon field width
            int col2 = innerL + S(168);       // second column x
            w_[W_S_LAT_L].bounds = { innerL, gy, innerL + fw, gy + S(16) };
            w_[W_S_LON_L].bounds = { col2, gy, col2 + fw, gy + S(16) };
            gy += S(18);
            fields_[FI_LAT].rect = { innerL, gy, innerL + fw, gy + S(30) };
            fields_[FI_LON].rect = { col2, gy, col2 + fw, gy + S(30) };
            gy += S(40);
            w_[W_S_LOCBTN].bounds = { innerL, gy, innerL + S(190), gy + S(34) };
            gy += S(42);
            if (w_[W_S_LOCERR].visible)
            {
                w_[W_S_LOCERR].bounds = { innerL, gy, innerR, gy + S(34) };
                gy += S(38);
            }
            if (w_[W_S_LOCNAME].visible)
            {
                w_[W_S_LOCNAME].bounds = { innerL, gy, innerR, gy + S(20) };
                gy += S(24);
            }
        }
        else
        {
            auto timeRow = [&](int lbl, int h, int c, int m, int ap) {
                w_[lbl].bounds = { innerL, gy, innerR, gy + S(16) };
                int ry = gy + S(18), rh = S(32);
                int x = innerL;
                w_[h].bounds  = { x, ry, x + S(56), ry + rh }; x += S(56);
                w_[c].bounds  = { x, ry, x + S(14), ry + rh }; x += S(14);
                w_[m].bounds  = { x, ry, x + S(56), ry + rh }; x += S(56) + S(8);
                w_[ap].bounds = { x, ry, x + S(64), ry + rh };
                gy = ry + rh + S(8);
            };
            timeRow(W_S_DAY_L, W_S_DAY_H, W_S_DAY_C, W_S_DAY_M, W_S_DAY_AP);
            timeRow(W_S_NIGHT_L, W_S_NIGHT_H, W_S_NIGHT_C, W_S_NIGHT_M, W_S_NIGHT_AP);
        }

        w_[W_S_SEP].bounds = { innerL, gy, innerR, gy + S(18) };
        gy += S(18);
        w_[W_S_BEDTOG].bounds = { innerL, gy, innerR, gy + S(30) };
        gy += S(34);
        if (settings_.BedtimeEnabled())
        {
            w_[W_S_BED_L].bounds = { innerL, gy, innerR, gy + S(16) };
            int ry = gy + S(18), rh = S(32), x = innerL;
            w_[W_S_BED_H].bounds  = { x, ry, x + S(56), ry + rh }; x += S(56);
            w_[W_S_BED_C].bounds  = { x, ry, x + S(14), ry + rh }; x += S(14);
            w_[W_S_BED_M].bounds  = { x, ry, x + S(56), ry + rh }; x += S(56) + S(8);
            w_[W_S_BED_AP].bounds = { x, ry, x + S(64), ry + rh };
            gy = ry + rh + S(6);
            w_[W_S_BEDHELP].bounds = { innerL, gy, innerR, gy + S(46) };
            gy += S(48);
        }
        cards_[2].r = { cardL, top, cardR, gy + S(8) };
        y = cards_[2].r.bottom + S(12);
    }
    // Card 3 — Transitions.
    {
        int top = y, gy = top + cardTitleSpace;
        sliderRowWithLabel(W_TR_DN_L, W_TR_DN_S, W_TR_DN_V, gy, S(96));
        sliderRowWithLabel(W_TR_NB_L, W_TR_NB_S, W_TR_NB_V, gy, S(96));
        w_[W_TR_HELP].bounds = { innerL, gy + S(2), innerR, gy + S(2) + S(34) };
        gy += S(2) + S(34) + S(8);
        w_[W_TR_DEMOBTN].bounds = { innerL, gy, innerL + S(140), gy + S(34) };
        w_[W_TR_PROG].bounds = { innerL + S(152), gy, innerR - S(48), gy + S(34) };
        w_[W_TR_DEMOPCT].bounds = { innerR - S(42), gy, innerR, gy + S(34) };
        gy += S(40);
        w_[W_TR_FOOT].bounds = { innerL, gy, innerR, gy + S(18) };
        gy += S(20);
        cards_[3].r = { cardL, top, cardR, gy + S(8) };
        y = cards_[3].r.bottom + S(12);
    }
    // Card 4 — General.
    {
        int top = y, gy = top + cardTitleSpace;
        w_[W_G_ENABLED].bounds = { innerL, gy, innerR, gy + S(30) }; gy += S(34);
        w_[W_G_STARTUP].bounds = { innerL, gy, innerR, gy + S(30) }; gy += S(34);
        w_[W_G_SEP].bounds = { innerL, gy, innerR, gy + S(18) };     gy += S(18);
        w_[W_G_EXTRANGE].bounds = { innerL, gy, innerR, gy + S(30) }; gy += S(32);
        w_[W_G_EXTHELP].bounds = { innerL, gy, innerR, gy + S(78) };  gy += S(80);
        if (w_[W_G_EXTSTATUS].visible)
        {
            w_[W_G_EXTSTATUS].bounds = { innerL, gy, innerR, gy + S(34) };
            gy += S(36);
        }
        cards_[4].r = { cardL, top, cardR, gy + S(8) };
        y = cards_[4].r.bottom + S(12);
    }

    // Attribution footer — right-aligned under the last card, outside any
    // card panel. Painted in its own pass after the cards loop in Paint().
    {
        int fh = S(18);
        w_[W_FOOTER_ATTR].bounds = { cardL, y, cardR, y + fh };
        y += fh + S(4);
    }

    contentH_ = y + S(8);
    int maxs = MaxScroll();
    if (!scrollAnim_)
    {
        if (scrollY_ > maxs) scrollY_ = maxs;
        if (scrollY_ < 0) scrollY_ = 0;
        scrollYf_ = scrollY_;
    }
    PositionFields();
}

// ── Sync / fields ─────────────────────────────────────────────────────────

void PreferencesWindow::SyncFromSettings()
{
    if (!built_) return;
    for (auto& w : w_)
    {
        if (w.visibleIf) w.visible = w.visibleIf();
        if (w.sync) w.sync();
    }
    for (auto& f : fields_)
    {
        if (f.visibleIf) f.visible = f.visibleIf();
        if (f.hwnd && GetFocus() != f.hwnd && f.read)
        {
            wchar_t buf[32];
            if (f.isInt) wsprintfW(buf, L"%d", (int)f.read());
            else         swprintf(buf, 32, L"%.5g", f.read());
            wchar_t cur[48];
            GetWindowTextW(f.hwnd, cur, 48);
            // Skip the SetWindowText when unchanged — a redundant one repaints
            // the EDIT and was part of the "boxes flash" symptom.
            if (lstrcmpW(cur, buf) != 0) SetWindowTextW(f.hwnd, buf);
        }
    }
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreferencesWindow::PositionFields()
{
    // The EDIT control is shown only for the field currently being edited;
    // every other value is drawn by Paint(). Numbers therefore track the
    // sliders live, and there are no child-window scroll trails.
    bool scrolling = scrollAnim_ || scrollDrag_;
    RECT cr; GetClientRect(hwnd_, &cr);
    int padX = Theme::Scale(8, dpi_);
    int editH = Theme::Scale(22, dpi_);
    for (int i = 0; i < (int)fields_.size(); ++i)
    {
        NumField& f = fields_[i];
        if (!f.hwnd) continue;
        int fieldH = f.rect.bottom - f.rect.top;
        int eh = editH < fieldH ? editH : fieldH;
        int boxTop = f.rect.top - scrollY_;
        bool show = (i == editingField_) && !scrolling && f.visible
                 && boxTop >= 0 && boxTop + fieldH <= cr.bottom;
        if (!show)
        {
            if (IsWindowVisible(f.hwnd)) ShowWindow(f.hwnd, SW_HIDE);
            f.placed = RECT{};
            continue;
        }
        // EDIT control sized to one text line and centred in the field box,
        // so the number sits vertically centred instead of top-aligned.
        int ey = boxTop + (fieldH - eh) / 2;
        RECT want{ f.rect.left + padX, ey, f.rect.right - padX, ey + eh };
        // Only MoveWindow when the position actually changed — a redundant
        // move every settings tick is what made the boxes flicker.
        if (!EqualRect(&want, &f.placed))
        {
            MoveWindow(f.hwnd, want.left, want.top,
                       want.right - want.left, want.bottom - want.top, TRUE);
            f.placed = want;
        }
        if (!IsWindowVisible(f.hwnd)) ShowWindow(f.hwnd, SW_SHOW);
    }
}

void PreferencesWindow::CommitField(int fieldId)
{
    for (auto& f : fields_)
    {
        if (f.id != fieldId || !f.hwnd || !f.commit) continue;
        wchar_t buf[48];
        GetWindowTextW(f.hwnd, buf, 48);
        if (buf[0] == L'\0') { SyncFromSettings(); return; }
        double v = wcstod(buf, nullptr);
        f.commit(v);
        return;
    }
}

// ── Paint ─────────────────────────────────────────────────────────────────

int PreferencesWindow::TitleBarH() const { return 0; } // Windows draws the frame
int PreferencesWindow::ViewportH() const
{
    RECT cr; GetClientRect(hwnd_, &cr);
    return cr.bottom - TitleBarH();
}
int PreferencesWindow::MaxScroll() const
{
    int m = contentH_ - ViewportH();
    return m > 0 ? m : 0;
}

void PreferencesWindow::Paint()
{
    auto S = [&](int v) { return Theme::Scale(v, dpi_); };
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT cr; GetClientRect(hwnd_, &cr);
    int W = cr.right, H = cr.bottom;
    if (W <= 0 || H <= 0) { EndPaint(hwnd_, &ps); return; }

    // Double-buffered, fully opaque. WS_CLIPCHILDREN keeps the numeric EDIT
    // children out of this DC's clip region, so they are never overdrawn.
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(Theme::Background);
    FillRect(mem, &cr, bg);
    DeleteObject(bg);

    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        GraphicsState st = g.Save();
        g.SetClip(Rect(0, 0, W, H));
        g.TranslateTransform(0.0f, (REAL)(-scrollY_));

        for (size_t ci = 0; ci < cards_.size(); ++ci)
        {
            const RECT& r = cards_[ci].r;
            if (r.bottom - scrollY_ < 0 || r.top - scrollY_ > H) continue;

            GraphicsPath panel;
            float rad = (float)S(8);
            panel.AddArc((REAL)r.left, (REAL)r.top, rad * 2, rad * 2, 180, 90);
            panel.AddArc(r.right - rad * 2, (REAL)r.top, rad * 2, rad * 2, 270, 90);
            panel.AddArc(r.right - rad * 2, r.bottom - rad * 2, rad * 2, rad * 2, 0, 90);
            panel.AddArc((REAL)r.left, r.bottom - rad * 2, rad * 2, rad * 2, 90, 90);
            panel.CloseFigure();
            SolidBrush fill(Color(255, 43, 43, 43));
            g.FillPath(&fill, &panel);
            Pen border(Color(255, 61, 61, 61), 1.0f);
            g.DrawPath(&border, &panel);

            {
                int icoSz = S(19);
                int titleX = r.left + S(16);
                if (cards_[ci].iconId >= 0)
                {
                    COLORREF ac = Theme::Accent();
                    DrawIconGlyph(g, cards_[ci].iconId, (REAL)(r.left + S(16)),
                                  (REAL)(r.top + S(14) + (S(22) - icoSz) / 2),
                                  (REAL)icoSz,
                                  Color(255, GetRValue(ac), GetGValue(ac), GetBValue(ac)));
                    titleX = r.left + S(16) + icoSz + S(9);
                }
                DrawTextG(g, cards_[ci].title, titleX, r.top + S(14),
                          r.right - titleX - S(16), S(22), S(14), true,
                          Color(255, 255, 255, 255), 0, true);
            }

            // Field backgrounds (the EDIT child sits just inside this).
            for (auto& f : fields_)
            {
                if (!f.visible) continue;
                if (f.rect.top < r.top || f.rect.bottom > r.bottom) continue;
                GraphicsPath fp;
                float fr = (float)S(4);
                RECT q = f.rect;
                fp.AddArc((REAL)q.left, (REAL)q.top, fr * 2, fr * 2, 180, 90);
                fp.AddArc(q.right - fr * 2, (REAL)q.top, fr * 2, fr * 2, 270, 90);
                fp.AddArc(q.right - fr * 2, q.bottom - fr * 2, fr * 2, fr * 2, 0, 90);
                fp.AddArc((REAL)q.left, q.bottom - fr * 2, fr * 2, fr * 2, 90, 90);
                fp.CloseFigure();
                SolidBrush ff(Color(255, 55, 55, 55));
                g.FillPath(&ff, &fp);
                Pen fb(Color(255, 75, 75, 75), 1.0f);
                g.DrawPath(&fb, &fp);

                // Draw the value directly — this scrolls cleanly with the
                // card; the live EDIT control (shown only when not scrolling)
                // sits on top of it identically.
                if (f.read)
                {
                    wchar_t vb[32];
                    if (f.isInt) wsprintfW(vb, L"%d", (int)f.read());
                    else         swprintf(vb, 32, L"%.5g", f.read());
                    DrawTextG(g, vb, q.left + S(8), q.top,
                              (q.right - q.left) - S(16), q.bottom - q.top,
                              S(14), false, Color(255, 255, 255, 255), 0, true);
                }
            }

            for (int id : cardIds_[ci])
                PaintWidget(g, dpi_, w_[id], mouse_, captured_ >= 0);
        }

        // Attribution footer (lives outside any card).
        {
            const RECT& fr = w_[W_FOOTER_ATTR].bounds;
            if (fr.bottom - scrollY_ >= 0 && fr.top - scrollY_ <= H)
                PaintWidget(g, dpi_, w_[W_FOOTER_ATTR], mouse_, captured_ >= 0);
        }
        g.Restore(st);

        // Scrollbar thumb.
        if (MaxScroll() > 0)
        {
            int vh = H;
            float frac = (float)vh / (float)contentH_;
            int thumbH = (int)(vh * frac);
            if (thumbH < S(28)) thumbH = S(28);
            int travel = vh - thumbH;
            int thumbY = (int)((float)scrollY_ / MaxScroll() * travel);
            SolidBrush tb(Color(170, 150, 150, 150));
            g.FillRectangle(&tb, (REAL)(W - S(9)), (REAL)(thumbY + S(2)),
                            (REAL)S(5), (REAL)(thumbH - S(4)));
        }
    }

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

// ── Mouse ─────────────────────────────────────────────────────────────────

POINT PreferencesWindow::ToContent(POINT clientPt) const
{
    return { clientPt.x, clientPt.y - TitleBarH() + scrollY_ };
}

int PreferencesWindow::HitWidget(POINT c) const
{
    for (int i = 0; i < (int)w_.size(); ++i)
        if (w_[i].visible && w_[i].type != WT::Label && w_[i].type != WT::Separator
            && WidgetContains(w_[i], c))
            return i;
    return -1;
}

int PreferencesWindow::CaptionHitTest(POINT p) const
{
    RECT cr; GetClientRect(hwnd_, &cr);
    int bw = Theme::Scale(46, dpi_), bh = Theme::Scale(34, dpi_);
    if (p.y < 0 || p.y >= bh) return -1;
    if (p.x >= cr.right - bw) return 1;          // close
    if (p.x >= cr.right - bw * 2) return 0;      // minimise
    return -1;
}

void PreferencesWindow::OnMouseDown(POINT clientPt)
{
    if (clientPt.y < TitleBarH()) return;

    // Any click ends an in-progress field edit (commits it via EN_KILLFOCUS).
    if (editingField_ >= 0) SetFocus(hwnd_);

    // Scrollbar grab.
    RECT cr; GetClientRect(hwnd_, &cr);
    if (MaxScroll() > 0 && clientPt.x >= cr.right - Theme::Scale(16, dpi_))
    {
        scrollDrag_ = true;
        scrollDragStartY_ = clientPt.y;
        scrollDragOrigin_ = scrollY_;
        scrollVel_ = 0.0;                 // a grab cancels any inertia
        if (scrollAnim_) { KillTimer(hwnd_, kScrollTimer); scrollAnim_ = false; }
        SetCapture(hwnd_);
        return;
    }

    POINT c = ToContent(clientPt);

    // A click inside a numeric field box opens it for editing.
    for (int i = 0; i < (int)fields_.size(); ++i)
    {
        const NumField& f = fields_[i];
        if (f.visible && f.hwnd
            && c.x >= f.rect.left && c.x < f.rect.right
            && c.y >= f.rect.top && c.y < f.rect.bottom)
        {
            BeginFieldEdit(i);
            return;
        }
    }

    int idx = HitWidget(c);
    if (idx < 0) return;
    if (w_[idx].type == WT::Dropdown)
    {
        OpenDropdown(idx);
        return;
    }
    captured_ = idx;
    WidgetDown(w_[idx], c, dpi_);
    SetCapture(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreferencesWindow::BeginFieldEdit(int fieldIndex)
{
    editingField_ = fieldIndex;
    PositionFields();                       // reveals this EDIT, hides the rest
    if (fieldIndex >= 0 && fields_[fieldIndex].hwnd)
        SetFocus(fields_[fieldIndex].hwnd); // WM_SETFOCUS selects the text
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreferencesWindow::OnMouseMove(POINT clientPt)
{
    mouse_ = ToContent(clientPt);
    if (scrollDrag_)
    {
        int vh = ViewportH();
        float frac = (float)vh / (float)contentH_;
        int thumbH = (int)(vh * frac);
        if (thumbH < Theme::Scale(28, dpi_)) thumbH = Theme::Scale(28, dpi_);
        int travel = vh - thumbH;
        if (travel > 0)
        {
            int dy = clientPt.y - scrollDragStartY_;
            scrollY_ = scrollDragOrigin_ + (int)((float)dy / travel * MaxScroll());
            if (scrollY_ < 0) scrollY_ = 0;
            if (scrollY_ > MaxScroll()) scrollY_ = MaxScroll();
            scrollYf_ = scrollY_;
            scrollVel_ = 0.0;
            PositionFields();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (captured_ >= 0)
    {
        WidgetDrag(w_[captured_], mouse_, dpi_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // Idle hover: only repaint when the hovered widget actually changes —
    // repainting on every WM_MOUSEMOVE is wasteful.
    int hw = HitWidget(mouse_);
    if (hw != hoverWidget_)
    {
        hoverWidget_ = hw;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PreferencesWindow::OnMouseUp(POINT clientPt)
{
    if (scrollDrag_)
    {
        scrollDrag_ = false;
        ReleaseCapture();
        PositionFields();                    // bring the EDIT boxes back
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (captured_ >= 0)
    {
        WidgetUp(w_[captured_], ToContent(clientPt), dpi_);
        captured_ = -1;
        ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PreferencesWindow::OnWheel(int delta)
{
    if (editingField_ >= 0) SetFocus(hwnd_);  // scrolling commits any open edit

    // Each notch adds momentum; rapid notches accumulate into a faster fling.
    double impulse = (double)Theme::Scale(6, dpi_);
    scrollVel_ += -(double)delta / WHEEL_DELTA * impulse;
    double cap = (double)Theme::Scale(95, dpi_);
    if (scrollVel_ > cap) scrollVel_ = cap;
    if (scrollVel_ < -cap) scrollVel_ = -cap;
    StartScrollAnim();
}

void PreferencesWindow::StartScrollAnim()
{
    if (!scrollAnim_)
    {
        scrollAnim_ = true;
        SetTimer(hwnd_, kScrollTimer, 12, nullptr);
    }
}

void PreferencesWindow::StepScroll()
{
    // Inertia: the position coasts on its velocity and the velocity decays by
    // a friction factor each frame. Past the top/bottom edge a spring pulls
    // it back (with heavier damping) — the rubber-band overscroll bounce.
    double maxs = (double)MaxScroll();
    bool over = scrollYf_ < 0.0 || scrollYf_ > maxs;

    if (scrollYf_ < 0.0)
        scrollVel_ += (0.0 - scrollYf_) * 0.18;
    else if (scrollYf_ > maxs)
        scrollVel_ += (maxs - scrollYf_) * 0.18;

    scrollVel_ *= over ? 0.58 : 0.88;   // heavy damping in overscroll, gentle coast in-bounds
    scrollYf_ += scrollVel_;

    if (fabs(scrollVel_) < 0.4 && scrollYf_ >= -0.5 && scrollYf_ <= maxs + 0.5)
    {
        scrollYf_ = scrollYf_ < 0 ? 0 : (scrollYf_ > maxs ? maxs : scrollYf_);
        scrollVel_ = 0.0;
        KillTimer(hwnd_, kScrollTimer);
        scrollAnim_ = false;
    }
    scrollY_ = (int)llround(scrollYf_);
    PositionFields();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreferencesWindow::OpenDropdown(int id)
{
    Widget& w = w_[id];
    std::vector<MenuItem> items;
    for (int i = 0; i < (int)w.options.size(); ++i)
    {
        MenuItem m;
        m.text = w.options[i];
        m.checkable = true;
        m.checked = (i == w.selected);
        m.onClick = [this, id, i] {
            w_[id].selected = i;
            if (w_[id].onPick) w_[id].onPick(i);
        };
        items.push_back(std::move(m));
    }
    POINT p{ w.bounds.left, w.bounds.bottom - scrollY_ + TitleBarH() };
    ClientToScreen(hwnd_, &p);
    dropdownPopup_.Track(std::move(items), p.x, p.y, true,
                         w.bounds.right - w.bounds.left);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ── Show ──────────────────────────────────────────────────────────────────

void PreferencesWindow::Show()
{
    if (!placed_)
    {
        dpi_ = Theme::Dpi(hwnd_);
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int ww = Theme::Scale(600, dpi_);
        int wh = Theme::Scale(880, dpi_);
        int maxH = (wa.bottom - wa.top) - Theme::Scale(60, dpi_);
        if (wh > maxH) wh = maxH;
        int wx = wa.left + ((wa.right - wa.left) - ww) / 2;
        int wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;
        SetWindowPos(hwnd_, nullptr, wx, wy, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
        placed_ = true;
        Layout();
    }
    SyncFromSettings();
    ShowWindow(hwnd_, SW_SHOW);
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
}

// ── Window procedures ─────────────────────────────────────────────────────

LRESULT CALLBACK PreferencesWindow::EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                             UINT_PTR id, DWORD_PTR)
{
    switch (msg)
    {
        case WM_KEYDOWN:
            if (wp == VK_RETURN) { SetFocus(GetParent(h)); return 0; }
            break;
        case WM_CHAR:
            if (wp == VK_RETURN) return 0; // swallow -> no error beep
            break;
        case WM_SETFOCUS:
        {
            LRESULT r = DefSubclassProc(h, msg, wp, lp);
            SendMessageW(h, EM_SETSEL, 0, -1);
            return r;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, &PreferencesWindow::EditProc, id);
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK PreferencesWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = reinterpret_cast<PreferencesWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg)
    {
        case WM_DPICHANGED:
        {
            self->dpi_ = HIWORD(wp);
            self->RecreateEditFont();
            RECT* sug = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, sug->left, sug->top,
                         sug->right - sug->left, sug->bottom - sug->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            self->Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_SIZE:
            if (self->built_) { self->Layout(); InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;

        case WM_TIMER:
            if (wp == kScrollTimer) self->StepScroll();
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = Theme::Scale(540, self->dpi_);
            mmi->ptMinTrackSize.y = Theme::Scale(420, self->dpi_);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            self->Paint();
            return 0;

        case WM_MOUSEMOVE:
        {
            self->OnMouseMove(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            self->mouse_ = { -1, -1 };
            if (self->hoverWidget_ != -1)
            {
                self->hoverWidget_ = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN:
            self->OnMouseDown(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            return 0;

        case WM_LBUTTONUP:
            self->OnMouseUp(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            return 0;

        case WM_MOUSEWHEEL:
            self->OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
            return 0;

        case WM_ACTIVATE:
            // Alt-tabbing away mid-drag would otherwise leave the engine's
            // preview guard latched — release it.
            if (LOWORD(wp) == WA_INACTIVE && self->captured_ >= 0)
            {
                WidgetUp(self->w_[self->captured_], self->mouse_, self->dpi_);
                self->captured_ = -1;
            }
            return 0;

        case WM_CTLCOLOREDIT:
        {
            HDC dc = (HDC)wp;
            SetTextColor(dc, Theme::TextPrimary);
            SetBkColor(dc, Theme::ControlFill);
            return (LRESULT)self->editBrush_;
        }

        case WM_COMMAND:
            if (HIWORD(wp) == EN_KILLFOCUS)
            {
                self->CommitField(LOWORD(wp));
                self->editingField_ = -1;   // box closed -> Paint shows the value
                self->PositionFields();     // hide the EDIT control
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_KS_LOCATION:
        {
            std::unique_ptr<LocResult> res(reinterpret_cast<LocResult*>(lp));
            self->locating_ = false;
            self->w_[W_S_LOCBTN].enabled = true;
            if (res && res->ok)
            {
                self->locationError_.clear();
                self->settings_.Latitude(res->lat);
                self->settings_.Longitude(res->lon);
                if (!res->name.empty())
                {
                    self->settings_.LocationName(res->name);
                }
                else
                {
                    char ll[64];
                    snprintf(ll, sizeof(ll), "%.4f, %.4f", res->lat, res->lon);
                    self->settings_.LocationName(ll);
                }
            }
            else
            {
                self->locationError_ =
                    L"Location unavailable. Check Windows location "
                    L"permissions for KelvinShift.";
            }
            self->SyncFromSettings();
            return 0;
        }

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE); // quit only via the tray menu
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
