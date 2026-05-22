#pragma once

// Persists to %APPDATA%\KelvinShift\settings.json with a 250ms debounced
// write so dragging sliders doesn't hammer disk. Raises SettingsChanged on
// every change so the schedule engine can re-tick, and PropertyChanged with
// the property name for finer-grained listeners. Port of SettingsService.cs.

#include "Common.h"
#include "Timer.h"
#include <string>
#include <algorithm>

class SettingsService
{
public:
    SettingsService();

    // Fired with the changed property's name (PascalCase, matches the JSON
    // keys). Fired even while loading.
    Event<const std::string&> PropertyChanged;
    // Fired after any change once loading has finished.
    Event<> SettingsChanged;

    void Load();

    // ── Color temperature ────────────────────────────────────────────────
    int  DayKelvin() const   { return dayKelvin_; }
    void DayKelvin(int v);
    int  NightKelvin() const { return nightKelvin_; }
    void NightKelvin(int v);

    // ── Brightness ───────────────────────────────────────────────────────
    double DayBrightness() const   { return dayBrightness_; }
    void   DayBrightness(double v);
    double NightBrightness() const { return nightBrightness_; }
    void   NightBrightness(double v);

    // ── Schedule ─────────────────────────────────────────────────────────
    const std::string& ScheduleMode() const { return scheduleMode_; }
    void ScheduleMode(const std::string& v);
    int  CustomDayHour() const    { return customDayHour_; }
    void CustomDayHour(int v);
    int  CustomDayMinute() const  { return customDayMinute_; }
    void CustomDayMinute(int v);
    int  CustomNightHour() const  { return customNightHour_; }
    void CustomNightHour(int v);
    int  CustomNightMinute() const{ return customNightMinute_; }
    void CustomNightMinute(int v);

    // ── Location ─────────────────────────────────────────────────────────
    double Latitude() const  { return latitude_; }
    void   Latitude(double v);
    double Longitude() const { return longitude_; }
    void   Longitude(double v);
    const std::string& LocationName() const { return locationName_; }
    void LocationName(const std::string& v);

    // ── Transition ───────────────────────────────────────────────────────
    int  TransitionMinutes() const { return transitionMinutes_; }
    void TransitionMinutes(int v);

    // ── Bedtime ──────────────────────────────────────────────────────────
    bool BedtimeEnabled() const { return bedtimeEnabled_; }
    void BedtimeEnabled(bool v);
    int  BedtimeKelvin() const  { return bedtimeKelvin_; }
    void BedtimeKelvin(int v);
    double BedtimeBrightness() const { return bedtimeBrightness_; }
    void   BedtimeBrightness(double v);
    int  BedtimeHour() const   { return bedtimeHour_; }
    void BedtimeHour(int v);
    int  BedtimeMinute() const { return bedtimeMinute_; }
    void BedtimeMinute(int v);
    int  BedtimeRampMinutes() const { return bedtimeRampMinutes_; }
    void BedtimeRampMinutes(int v);

    // ── Master toggle / misc ─────────────────────────────────────────────
    bool Enabled() const { return enabled_; }
    void Enabled(bool v);
    bool UseSystemColorPipeline() const { return useSystemColorPipeline_; }
    void UseSystemColorPipeline(bool v);
    bool LaunchAtLogin() const { return launchAtLogin_; }
    void LaunchAtLogin(bool v);

    // ── Computed labels (used by the tray menu) ──────────────────────────
    std::string DayTimeLabel() const;
    std::string NightTimeLabel() const;
    std::string BedtimeTimeLabel() const;

private:
    template <class T>
    bool Set(T& field, const T& value, const char* name);

    void Save();
    void ScheduleSave();

    std::wstring path_;
    bool loading_ = false;
    Timer saveDebounce_;

    int    dayKelvin_ = 5000;
    int    nightKelvin_ = 2700;
    double dayBrightness_ = 1.0;
    double nightBrightness_ = 0.8;
    std::string scheduleMode_ = "custom";
    int    customDayHour_ = 7;
    int    customDayMinute_ = 0;
    int    customNightHour_ = 20;
    int    customNightMinute_ = 0;
    double latitude_ = 0.0;
    double longitude_ = 0.0;
    std::string locationName_;
    int    transitionMinutes_ = 20;
    bool   bedtimeEnabled_ = false;
    int    bedtimeKelvin_ = 1900;
    double bedtimeBrightness_ = 0.4;
    int    bedtimeHour_ = 23;
    int    bedtimeMinute_ = 0;
    int    bedtimeRampMinutes_ = 60;
    bool   enabled_ = true;
    bool   useSystemColorPipeline_ = true;
    bool   launchAtLogin_ = false;
};
