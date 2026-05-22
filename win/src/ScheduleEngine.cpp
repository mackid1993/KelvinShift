#include "ScheduleEngine.h"
#include "SettingsService.h"
#include "GammaService.h"
#include "SolarCalculator.h"
#include <algorithm>
#include <cmath>

// ── Circular-clock helpers ────────────────────────────────────────────────
namespace {

int Wrap(int m) { return ((m % 1440) + 1440) % 1440; }

bool InRange(int t, int from, int to)
{
    return from <= to ? (t >= from && t < to)
                      : (t >= from || t < to);
}

double Progress(int t, int from, int length)
{
    int elapsed = Wrap(t - from);
    return std::min(1.0, (double)elapsed / std::max(1, length));
}

// Linear interpolation — constant rate of change so a fixed slice of time
// always moves the same colour delta regardless of where in the ramp it is.
int LerpI(int a, int b, double t) { return a + (int)llround((b - a) * t); }
double LerpD(double a, double b, double t) { return a + (b - a) * t; }

int MinutesFromMidnight(const SYSTEMTIME& d) { return d.wHour * 60 + d.wMinute; }

// Normalize raw solar minutes (possibly <0 or >=1440) into a time-of-day,
// matching DateTime.AddMinutes(...).Hour*60+Minute in the C# port.
int NormalizeMinute(double x) { return Wrap((int)std::floor(x)); }

} // namespace

ScheduleEngine::ScheduleEngine(SettingsService& settings, GammaService& gamma)
    : settings_(settings), gamma_(gamma)
{
    state_.dayKelvin = settings_.DayKelvin();
    state_.nightKelvin = settings_.NightKelvin();
    state_.dayBrightness = settings_.DayBrightness();
    state_.nightBrightness = settings_.NightBrightness();
    state_.bedtimeEnabled = settings_.BedtimeEnabled();
    state_.bedtimeKelvin = settings_.BedtimeKelvin();
    state_.bedtimeBrightness = settings_.BedtimeBrightness();
    state_.enabled = settings_.Enabled();

    timer_.SetInterval(15000); // 15-second schedule tick
    timer_.SetCallback([this] { Tick(); });

    settings_.SettingsChanged.Add([this]
    {
        if (!previewKelvin_ && !previewBrightness_ && !isDemoRunning_)
            Tick();
    });
}

void ScheduleEngine::Start()
{
    Tick();
    timer_.Start();
}

void ScheduleEngine::Stop()
{
    timer_.Stop();
    gamma_.Reset();
}

// ── Preview ───────────────────────────────────────────────────────────────

void ScheduleEngine::StartPreview(int kelvin)
{
    previewKelvin_ = kelvin;
    previewBrightness_.reset();
    gamma_.ApplyKelvin(kelvin);
}

void ScheduleEngine::UpdatePreview(int kelvin)
{
    if (!previewKelvin_) return;
    previewKelvin_ = kelvin;
    gamma_.ApplyKelvin(kelvin);
}

void ScheduleEngine::StartBrightnessPreview(double brightness)
{
    previewBrightness_ = brightness;
    previewKelvin_.reset();
    gamma_.ApplyKelvinWithBrightness(state_.currentKelvin, brightness);
}

void ScheduleEngine::UpdateBrightnessPreview(double brightness)
{
    if (!previewBrightness_) return;
    previewBrightness_ = brightness;
    gamma_.ApplyKelvinWithBrightness(state_.currentKelvin, brightness);
}

void ScheduleEngine::StopPreview()
{
    previewKelvin_.reset();
    previewBrightness_.reset();
    RestoreScheduledSettings();
}

void ScheduleEngine::RestoreScheduledSettings()
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    auto r = ComputeSchedule(now);
    gamma_.ApplyKelvinWithBrightness(r.kelvin, r.brightness);
}

// ── Demo cycle ────────────────────────────────────────────────────────────

void ScheduleEngine::StartDemo(double durationSeconds)
{
    if (isDemoRunning_) return;
    isDemoRunning_ = true;
    demoProgress_ = 0.0;
    previewKelvin_.reset();
    previewBrightness_.reset();

    const double step = 1.0 / 60.0;
    demoPerTick_ = step / durationSeconds;

    demoTimer_.SetInterval((unsigned)std::lround(step * 1000.0));
    demoTimer_.SetCallback([this]
    {
        demoProgress_ += demoPerTick_;
        if (demoProgress_ >= 1.0) { StopDemo(); return; }
        ApplyDemoSettings(demoProgress_);
        DemoProgressChanged.Raise(demoProgress_);
    });
    demoTimer_.Start();
    ApplyDemoSettings(0.0);
    DemoProgressChanged.Raise(0.0);
}

void ScheduleEngine::StopDemo()
{
    demoTimer_.Stop();
    isDemoRunning_ = false;
    demoProgress_ = 0.0;
    DemoProgressChanged.Raise(0.0);
    RestoreScheduledSettings();
}

void ScheduleEngine::ApplyDemoSettings(double p)
{
    // "Preview Cycle" always runs the full day -> night -> bedtime -> day
    // loop, so the bedtime phase is always shown — the point of the preview
    // is to see it, even before bedtime is switched on in the schedule.
    int k;
    double b;
    const double third = 1.0 / 3.0;
    if (p < third)
    {
        double t = p * 3.0;
        k = LerpI(settings_.DayKelvin(), settings_.NightKelvin(), t);
        b = LerpD(settings_.DayBrightness(), settings_.NightBrightness(), t);
    }
    else if (p < 2 * third)
    {
        double t = (p - third) * 3.0;
        k = LerpI(settings_.NightKelvin(), settings_.BedtimeKelvin(), t);
        b = LerpD(settings_.NightBrightness(), settings_.BedtimeBrightness(), t);
    }
    else
    {
        double t = (p - 2 * third) * 3.0;
        k = LerpI(settings_.BedtimeKelvin(), settings_.DayKelvin(), t);
        b = LerpD(settings_.BedtimeBrightness(), settings_.DayBrightness(), t);
    }
    gamma_.ApplyKelvinWithBrightness(k, b);
}

// ── Tick ──────────────────────────────────────────────────────────────────

void ScheduleEngine::Tick()
{
    if (previewKelvin_ || previewBrightness_ || isDemoRunning_) return;

    if (!settings_.Enabled())
    {
        gamma_.Reset();
        Publish(SchedulePhase::Day, 6500, 1.0,
                std::nullopt, std::nullopt, std::nullopt, false);
        return;
    }

    SYSTEMTIME now;
    GetLocalTime(&now);
    Times t = ScheduleTimes(now);
    auto r = ComputeSchedule(now);

    gamma_.ApplyKelvinWithBrightness(r.kelvin, r.brightness);
    Publish(r.phase, r.kelvin, r.brightness, t.sunriseMin, t.sunsetMin,
            r.nextMinute, true);
}

// ── Shared computation ────────────────────────────────────────────────────

ScheduleEngine::ScheduleResult ScheduleEngine::ComputeSchedule(const SYSTEMTIME& now) const
{
    // "Day starts" / "Night starts" / "Bedtime" are the times the screen
    // REACHES that state. Each transition runs for tranMin minutes BEFORE
    // the scheduled time and is capped to the gap to the adjacent anchor.
    Times t = ScheduleTimes(now);
    int dayMin = t.dayMin, nightMin = t.nightMin;
    int nowMin = MinutesFromMidnight(now);
    int tranMin = settings_.TransitionMinutes();

    bool useBedtime = BedtimeIsActive(dayMin, nightMin);
    int bedMin = Wrap(settings_.BedtimeHour() * 60 + settings_.BedtimeMinute());
    int morningFromK = useBedtime ? settings_.BedtimeKelvin() : settings_.NightKelvin();
    double morningFromB = useBedtime ? settings_.BedtimeBrightness() : settings_.NightBrightness();

    int dayToNight = Wrap(nightMin - dayMin);
    int nightToBed = useBedtime ? Wrap(bedMin - nightMin) : 0;
    int bedOrNightToDay = useBedtime ? Wrap(dayMin - bedMin)
                                     : Wrap(dayMin - nightMin);

    int nightTransLen = std::min(tranMin, dayToNight);
    int dayTransLen   = std::min(tranMin, bedOrNightToDay);
    int rampLen = useBedtime ? std::min(settings_.BedtimeRampMinutes(), nightToBed) : 0;

    int nightTransStart = Wrap(nightMin - nightTransLen);
    int dayTransStart   = Wrap(dayMin - dayTransLen);

    if (InRange(nowMin, dayMin, nightTransStart))
        return { settings_.DayKelvin(), settings_.DayBrightness(),
                 SchedulePhase::Day, nightTransStart };

    if (InRange(nowMin, nightTransStart, nightMin))
    {
        double p = Progress(nowMin, nightTransStart, nightTransLen);
        return { LerpI(settings_.DayKelvin(), settings_.NightKelvin(), p),
                 LerpD(settings_.DayBrightness(), settings_.NightBrightness(), p),
                 SchedulePhase::TransitionToNight, nightMin };
    }

    if (InRange(nowMin, dayTransStart, dayMin))
    {
        double p = Progress(nowMin, dayTransStart, dayTransLen);
        return { LerpI(morningFromK, settings_.DayKelvin(), p),
                 LerpD(morningFromB, settings_.DayBrightness(), p),
                 SchedulePhase::TransitionToDay, dayMin };
    }

    // Past nightMin, before dayTransStart.
    if (useBedtime)
    {
        int rampStart = Wrap(bedMin - rampLen);
        if (InRange(nowMin, nightMin, rampStart))
            return { settings_.NightKelvin(), settings_.NightBrightness(),
                     SchedulePhase::Night, rampStart };
        if (InRange(nowMin, rampStart, bedMin))
        {
            double p = Progress(nowMin, rampStart, rampLen);
            return { LerpI(settings_.NightKelvin(), settings_.BedtimeKelvin(), p),
                     LerpD(settings_.NightBrightness(), settings_.BedtimeBrightness(), p),
                     SchedulePhase::RampToBedtime, bedMin };
        }
        return { settings_.BedtimeKelvin(), settings_.BedtimeBrightness(),
                 SchedulePhase::Bedtime, dayTransStart };
    }
    return { settings_.NightKelvin(), settings_.NightBrightness(),
             SchedulePhase::Night, dayTransStart };
}

bool ScheduleEngine::BedtimeIsActive(int dayMin, int nightMin) const
{
    if (!settings_.BedtimeEnabled()) return false;
    int bed = Wrap(settings_.BedtimeHour() * 60 + settings_.BedtimeMinute());
    int nightToDay = Wrap(dayMin - nightMin);
    int nightToBed = Wrap(bed - nightMin);
    return nightToBed > 0 && nightToBed < nightToDay;
}

ScheduleEngine::Times ScheduleEngine::ScheduleTimes(const SYSTEMTIME& date) const
{
    if (settings_.ScheduleMode() == "solar")
    {
        auto s = SolarCalculator::Calculate(date, settings_.Latitude(), settings_.Longitude());
        if (s)
        {
            int rise = NormalizeMinute(s->sunriseMin);
            int set  = NormalizeMinute(s->sunsetMin);
            return { rise, set, rise, set };
        }
    }
    return { settings_.CustomDayHour() * 60 + settings_.CustomDayMinute(),
             settings_.CustomNightHour() * 60 + settings_.CustomNightMinute(),
             std::nullopt, std::nullopt };
}

void ScheduleEngine::Publish(SchedulePhase phase, int kelvin, double brightness,
                             std::optional<int> sunrise, std::optional<int> sunset,
                             std::optional<int> next, bool enabled)
{
    state_.phase = phase;
    state_.currentKelvin = kelvin;
    state_.currentBrightness = brightness;
    state_.dayKelvin = settings_.DayKelvin();
    state_.nightKelvin = settings_.NightKelvin();
    state_.dayBrightness = settings_.DayBrightness();
    state_.nightBrightness = settings_.NightBrightness();
    state_.bedtimeEnabled = settings_.BedtimeEnabled();
    state_.bedtimeKelvin = settings_.BedtimeKelvin();
    state_.bedtimeBrightness = settings_.BedtimeBrightness();
    state_.sunriseMin = sunrise;
    state_.sunsetMin = sunset;
    state_.nextEventMin = next;
    state_.enabled = enabled;
    StateChanged.Raise(state_);
}
