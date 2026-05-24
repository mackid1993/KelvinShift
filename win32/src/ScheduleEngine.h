#pragma once

// Port of macos/Sources/KelvinShift/ScheduleEngine.swift (and the C# port).
// 4-anchor day schedule when bedtime is off, 6-anchor when on. Identical
// linear interpolation and wrap-around timeline. The morning transitionToDay
// sources from bedtime values when bedtime is active, else from night.

#include "Common.h"
#include "Timer.h"
#include <windows.h>
#include <optional>

class SettingsService;
class GammaService;

enum class SchedulePhase
{
    Day, Night, TransitionToNight, TransitionToDay, RampToBedtime, Bedtime
};

struct ScheduleState
{
    SchedulePhase phase = SchedulePhase::Day;
    int    currentKelvin = 6500;
    double currentBrightness = 1.0;
    int    dayKelvin = 5000;
    int    nightKelvin = 2700;
    double dayBrightness = 1.0;
    double nightBrightness = 0.8;
    bool   bedtimeEnabled = false;
    int    bedtimeKelvin = 1900;
    double bedtimeBrightness = 0.4;
    // Minutes from local midnight; empty unless solar mode resolved them.
    std::optional<int> sunriseMin;
    std::optional<int> sunsetMin;
    std::optional<int> nextEventMin;
    bool   enabled = true;
};

class ScheduleEngine
{
public:
    ScheduleEngine(SettingsService& settings, GammaService& gamma);

    Event<const ScheduleState&> StateChanged;
    Event<double> DemoProgressChanged;

    const ScheduleState& State() const { return state_; }
    bool IsDemoRunning() const { return isDemoRunning_; }

    void Start();
    void Stop();

    // Preview (slider drag) — gamma follows the slider directly, bypassing
    // the scheduled tick until StopPreview restores it.
    void StartPreview(int kelvin);
    void UpdatePreview(int kelvin);
    void StartBrightnessPreview(double brightness);
    void UpdateBrightnessPreview(double brightness);
    void StopPreview();

    // Demo cycle — runs a full day/night(/bedtime) loop.
    void StartDemo(double durationSeconds = 15.0);
    void StopDemo();

    struct ScheduleResult
    {
        int kelvin;
        double brightness;
        SchedulePhase phase;
        int nextMinute;
    };
    ScheduleResult ComputeSchedule(const SYSTEMTIME& now) const;

private:
    struct Times
    {
        int dayMin;
        int nightMin;
        std::optional<int> sunriseMin;
        std::optional<int> sunsetMin;
    };

    void Tick();
    void RestoreScheduledSettings();
    void ApplyDemoSettings(double progress);
    Times ScheduleTimes(const SYSTEMTIME& date) const;
    bool BedtimeIsActive(int dayMin, int nightMin) const;
    void Publish(SchedulePhase phase, int kelvin, double brightness,
                 std::optional<int> sunrise, std::optional<int> sunset,
                 std::optional<int> next, bool enabled);

    SettingsService& settings_;
    GammaService& gamma_;
    Timer timer_;
    Timer demoTimer_;

    ScheduleState state_;
    std::optional<int> previewKelvin_;
    std::optional<double> previewBrightness_;
    bool isDemoRunning_ = false;
    double demoProgress_ = 0.0;
    double demoPerTick_ = 0.0;
};
