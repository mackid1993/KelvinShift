#include "SettingsService.h"
#include "Json.h"
#include "LaunchAtLoginService.h"
#include <cstdio>

SettingsService::SettingsService()
{
    std::wstring dir = KnownFolder(FOLDERID_RoamingAppData) + L"\\KelvinShift";
    CreateDirectoryW(dir.c_str(), nullptr);
    path_ = dir + L"\\settings.json";

    // One-shot debounce: each change restarts it; the tick saves once.
    saveDebounce_.SetInterval(250);
    saveDebounce_.SetCallback([this] { saveDebounce_.Stop(); Save(); });
}

// ── Change plumbing ───────────────────────────────────────────────────────

template <class T>
bool SettingsService::Set(T& field, const T& value, const char* name)
{
    if (field == value) return false;
    field = value;
    PropertyChanged.Raise(std::string(name));
    if (!loading_)
    {
        ScheduleSave();
        SettingsChanged.Raise();
    }
    return true;
}

// Explicit instantiations for the types actually used.
template bool SettingsService::Set<int>(int&, const int&, const char*);
template bool SettingsService::Set<double>(double&, const double&, const char*);
template bool SettingsService::Set<bool>(bool&, const bool&, const char*);
template bool SettingsService::Set<std::string>(std::string&, const std::string&, const char*);

// ── Setters (clamping mirrors SettingsService.cs) ─────────────────────────

void SettingsService::DayKelvin(int v)        { Set(dayKelvin_, std::clamp(v, 1000, 6500), "DayKelvin"); }
void SettingsService::NightKelvin(int v)      { Set(nightKelvin_, std::clamp(v, 1000, 6500), "NightKelvin"); }
void SettingsService::DayBrightness(double v) { Set(dayBrightness_, std::clamp(v, 0.1, 1.0), "DayBrightness"); }
void SettingsService::NightBrightness(double v){ Set(nightBrightness_, std::clamp(v, 0.1, 1.0), "NightBrightness"); }
void SettingsService::ScheduleMode(const std::string& v) { Set(scheduleMode_, v, "ScheduleMode"); }
void SettingsService::CustomDayHour(int v)    { Set(customDayHour_, std::clamp(v, 0, 23), "CustomDayHour"); }
void SettingsService::CustomDayMinute(int v)  { Set(customDayMinute_, std::clamp(v, 0, 59), "CustomDayMinute"); }
void SettingsService::CustomNightHour(int v)  { Set(customNightHour_, std::clamp(v, 0, 23), "CustomNightHour"); }
void SettingsService::CustomNightMinute(int v){ Set(customNightMinute_, std::clamp(v, 0, 59), "CustomNightMinute"); }
void SettingsService::Latitude(double v)      { Set(latitude_, v, "Latitude"); }
void SettingsService::Longitude(double v)     { Set(longitude_, v, "Longitude"); }
void SettingsService::LocationName(const std::string& v) { Set(locationName_, v, "LocationName"); }
void SettingsService::TransitionMinutes(int v){ Set(transitionMinutes_, std::max(1, v), "TransitionMinutes"); }
void SettingsService::BedtimeEnabled(bool v)  { Set(bedtimeEnabled_, v, "BedtimeEnabled"); }
void SettingsService::BedtimeKelvin(int v)    { Set(bedtimeKelvin_, std::clamp(v, 1000, 6500), "BedtimeKelvin"); }
void SettingsService::BedtimeBrightness(double v){ Set(bedtimeBrightness_, std::clamp(v, 0.1, 1.0), "BedtimeBrightness"); }
void SettingsService::BedtimeHour(int v)      { Set(bedtimeHour_, std::clamp(v, 0, 23), "BedtimeHour"); }
void SettingsService::BedtimeMinute(int v)    { Set(bedtimeMinute_, std::clamp(v, 0, 59), "BedtimeMinute"); }
void SettingsService::BedtimeRampMinutes(int v){ Set(bedtimeRampMinutes_, std::max(1, v), "BedtimeRampMinutes"); }
void SettingsService::Enabled(bool v)         { Set(enabled_, v, "Enabled"); }
void SettingsService::UseSystemColorPipeline(bool v) { Set(useSystemColorPipeline_, v, "UseSystemColorPipeline"); }

void SettingsService::LaunchAtLogin(bool v)
{
    if (Set(launchAtLogin_, v, "LaunchAtLogin"))
        LaunchAtLoginService::Apply(v);
}

// ── Persistence ───────────────────────────────────────────────────────────

void SettingsService::Load()
{
    HANDLE f = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return; // first run — keep defaults

    std::string text;
    char buf[4096];
    DWORD got;
    while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got > 0)
        text.append(buf, got);
    CloseHandle(f);

    JsonReader r;
    if (!r.Parse(text)) return;

    loading_ = true;
    DayKelvin(r.GetInt("DayKelvin", dayKelvin_));
    NightKelvin(r.GetInt("NightKelvin", nightKelvin_));
    DayBrightness(r.GetDouble("DayBrightness", dayBrightness_));
    NightBrightness(r.GetDouble("NightBrightness", nightBrightness_));
    ScheduleMode(r.GetString("ScheduleMode", "custom"));
    CustomDayHour(r.GetInt("CustomDayHour", customDayHour_));
    CustomDayMinute(r.GetInt("CustomDayMinute", customDayMinute_));
    CustomNightHour(r.GetInt("CustomNightHour", customNightHour_));
    CustomNightMinute(r.GetInt("CustomNightMinute", customNightMinute_));
    Latitude(r.GetDouble("Latitude", 0.0));
    Longitude(r.GetDouble("Longitude", 0.0));
    LocationName(r.GetString("LocationName", ""));
    {
        // 0 (or missing) is treated as "unset" -> default, matching the C# DTO.
        int tm = r.GetInt("TransitionMinutes", 0);
        TransitionMinutes(tm == 0 ? 20 : tm);
    }
    BedtimeEnabled(r.GetBool("BedtimeEnabled", false));
    {
        int bk = r.GetInt("BedtimeKelvin", 0);
        BedtimeKelvin(bk == 0 ? 1900 : bk);
    }
    {
        double bb = r.GetDouble("BedtimeBrightness", 0.0);
        BedtimeBrightness(bb == 0.0 ? 0.4 : bb);
    }
    BedtimeHour(r.GetInt("BedtimeHour", bedtimeHour_));
    BedtimeMinute(r.GetInt("BedtimeMinute", 0));
    {
        int br = r.GetInt("BedtimeRampMinutes", 0);
        BedtimeRampMinutes(br == 0 ? 60 : br);
    }
    UseSystemColorPipeline(r.GetBool("UseSystemColorPipeline", true));
    Enabled(r.GetBool("Enabled", true));
    loading_ = false;

    // Launch-at-login is sourced from the registry, not the file.
    launchAtLogin_ = LaunchAtLoginService::IsEnabled();
}

void SettingsService::Save()
{
    JsonWriter w;
    w.Int("DayKelvin", dayKelvin_);
    w.Int("NightKelvin", nightKelvin_);
    w.Double("DayBrightness", dayBrightness_);
    w.Double("NightBrightness", nightBrightness_);
    w.Str("ScheduleMode", scheduleMode_);
    w.Int("CustomDayHour", customDayHour_);
    w.Int("CustomDayMinute", customDayMinute_);
    w.Int("CustomNightHour", customNightHour_);
    w.Int("CustomNightMinute", customNightMinute_);
    w.Double("Latitude", latitude_);
    w.Double("Longitude", longitude_);
    w.Str("LocationName", locationName_);
    w.Int("TransitionMinutes", transitionMinutes_);
    w.Bool("BedtimeEnabled", bedtimeEnabled_);
    w.Int("BedtimeKelvin", bedtimeKelvin_);
    w.Double("BedtimeBrightness", bedtimeBrightness_);
    w.Int("BedtimeHour", bedtimeHour_);
    w.Int("BedtimeMinute", bedtimeMinute_);
    w.Int("BedtimeRampMinutes", bedtimeRampMinutes_);
    w.Bool("UseSystemColorPipeline", useSystemColorPipeline_);
    w.Bool("Enabled", enabled_);
    std::string json = w.Done();

    // Write to a temp file then rename — never leave a half-written settings
    // file if the process dies mid-save.
    std::wstring tmp = path_ + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(f, json.data(), (DWORD)json.size(), &written, nullptr);
    CloseHandle(f);
    MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void SettingsService::ScheduleSave()
{
    saveDebounce_.Start(); // Start() restarts from zero — the debounce
}

// ── Labels ────────────────────────────────────────────────────────────────

static std::string FormatTime(int h, int m)
{
    int h12 = (h == 0) ? 12 : (h > 12 ? h - 12 : h);
    const char* sfx = (h >= 12) ? "PM" : "AM";
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d %s", h12, m, sfx);
    return buf;
}

std::string SettingsService::DayTimeLabel() const   { return FormatTime(customDayHour_, customDayMinute_); }
std::string SettingsService::NightTimeLabel() const { return FormatTime(customNightHour_, customNightMinute_); }
std::string SettingsService::BedtimeTimeLabel() const { return FormatTime(bedtimeHour_, bedtimeMinute_); }
