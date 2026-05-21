using System;
using System.Windows.Threading;

namespace KelvinShift.Services;

public enum SchedulePhase
{
    Day, Night, TransitionToNight, TransitionToDay, RampToBedtime, Bedtime
}

public sealed record ScheduleState(
    SchedulePhase Phase,
    int CurrentKelvin,
    double CurrentBrightness,
    int DayKelvin,
    int NightKelvin,
    double DayBrightness,
    double NightBrightness,
    bool BedtimeEnabled,
    int BedtimeKelvin,
    double BedtimeBrightness,
    DateTime? SunriseTime,
    DateTime? SunsetTime,
    DateTime? NextEvent,
    bool Enabled);

// C# port of macos/Sources/KelvinShift/ScheduleEngine.swift. 4-anchor day
// schedule when bedtime is off, 6-anchor (… rampToBedtime, bedtime …) when on.
// Identical Hermite smoothing and wrap-around timeline. The morning
// transitionToDay sources from bedtime values when active, else night.
public sealed class ScheduleEngine
{
    private readonly SettingsService _settings;
    private readonly GammaService _gamma;
    private readonly DispatcherTimer _timer;
    private DispatcherTimer? _demoTimer;

    private int? _previewKelvin;
    private double? _previewBrightness;
    private bool _isDemoRunning;
    private double _demoProgress;

    public event EventHandler<ScheduleState>? StateChanged;
    public event Action<double>? DemoProgressChanged;

    public ScheduleState State { get; private set; }
    public bool IsDemoRunning => _isDemoRunning;

    public ScheduleEngine(SettingsService settings, GammaService gamma)
    {
        _settings = settings;
        _gamma = gamma;
        State = new ScheduleState(SchedulePhase.Day, 6500, 1.0,
            settings.DayKelvin, settings.NightKelvin,
            settings.DayBrightness, settings.NightBrightness,
            settings.BedtimeEnabled, settings.BedtimeKelvin, settings.BedtimeBrightness,
            null, null, null, settings.Enabled);

        _timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(15) };
        _timer.Tick += (_, _) => Tick();

        _settings.SettingsChanged += (_, _) =>
        {
            if (_previewKelvin is null && _previewBrightness is null && !_isDemoRunning)
                Tick();
        };
    }

    public void Start()
    {
        Tick();
        _timer.Start();
    }

    public void Stop()
    {
        _timer.Stop();
        _gamma.Reset();
    }

    // ── Preview (slider drag) ─────────────────────────────

    public void StartPreview(int kelvin)
    {
        _previewKelvin = kelvin;
        _previewBrightness = null;
        _gamma.ApplyKelvin(kelvin);
    }

    public void UpdatePreview(int kelvin)
    {
        if (_previewKelvin is null) return;
        _previewKelvin = kelvin;
        _gamma.ApplyKelvin(kelvin);
    }

    public void StartBrightnessPreview(double brightness)
    {
        _previewBrightness = brightness;
        _previewKelvin = null;
        _gamma.ApplyKelvinWithBrightness(State.CurrentKelvin, brightness);
    }

    public void UpdateBrightnessPreview(double brightness)
    {
        if (_previewBrightness is null) return;
        _previewBrightness = brightness;
        _gamma.ApplyKelvinWithBrightness(State.CurrentKelvin, brightness);
    }

    public void StopPreview()
    {
        _previewKelvin = null;
        _previewBrightness = null;
        RestoreScheduledSettings();
    }

    private void RestoreScheduledSettings()
    {
        var r = ComputeSchedule(DateTime.Now);
        _gamma.ApplyKelvinWithBrightness(r.Kelvin, r.Brightness);
    }

    // ── Demo cycle ────────────────────────────────────────

    public void StartDemo(double durationSeconds = 15.0)
    {
        if (_isDemoRunning) return;
        _isDemoRunning = true;
        _demoProgress = 0.0;
        _previewKelvin = null;
        _previewBrightness = null;

        var step = 1.0 / 60.0;
        var perTick = step / durationSeconds;

        _demoTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(step) };
        _demoTimer.Tick += (_, _) =>
        {
            _demoProgress += perTick;
            if (_demoProgress >= 1.0) { StopDemo(); return; }
            ApplyDemoSettings(_demoProgress);
            DemoProgressChanged?.Invoke(_demoProgress);
        };
        _demoTimer.Start();
        ApplyDemoSettings(0.0);
        DemoProgressChanged?.Invoke(0.0);
    }

    public void StopDemo()
    {
        _demoTimer?.Stop();
        _demoTimer = null;
        _isDemoRunning = false;
        _demoProgress = 0.0;
        DemoProgressChanged?.Invoke(0.0);
        RestoreScheduledSettings();
    }

    private void ApplyDemoSettings(double p)
    {
        int k; double b;
        if (_settings.BedtimeEnabled)
        {
            var third = 1.0 / 3.0;
            if (p < third)
            {
                var t = p * 3.0;
                k = Lerp(_settings.DayKelvin, _settings.NightKelvin, t);
                b = LerpD(_settings.DayBrightness, _settings.NightBrightness, t);
            }
            else if (p < 2 * third)
            {
                var t = (p - third) * 3.0;
                k = Lerp(_settings.NightKelvin, _settings.BedtimeKelvin, t);
                b = LerpD(_settings.NightBrightness, _settings.BedtimeBrightness, t);
            }
            else
            {
                var t = (p - 2 * third) * 3.0;
                k = Lerp(_settings.BedtimeKelvin, _settings.DayKelvin, t);
                b = LerpD(_settings.BedtimeBrightness, _settings.DayBrightness, t);
            }
        }
        else
        {
            if (p < 0.5)
            {
                var t = p * 2.0;
                k = Lerp(_settings.DayKelvin, _settings.NightKelvin, t);
                b = LerpD(_settings.DayBrightness, _settings.NightBrightness, t);
            }
            else
            {
                var t = (p - 0.5) * 2.0;
                k = Lerp(_settings.NightKelvin, _settings.DayKelvin, t);
                b = LerpD(_settings.NightBrightness, _settings.DayBrightness, t);
            }
        }
        _gamma.ApplyKelvinWithBrightness(k, b);
    }

    // ── Tick ──────────────────────────────────────────────

    private void Tick()
    {
        if (_previewKelvin is not null || _previewBrightness is not null || _isDemoRunning) return;

        if (!_settings.Enabled)
        {
            _gamma.Reset();
            Publish(SchedulePhase.Day, 6500, 1.0, null, null, null, false);
            return;
        }

        var now = DateTime.Now;
        var (_, _, sunrise, sunset) = ScheduleTimes(now);
        var r = ComputeSchedule(now);
        var next = TodayAt(r.NextMinute, now);

        _gamma.ApplyKelvinWithBrightness(r.Kelvin, r.Brightness);
        Publish(r.Phase, r.Kelvin, r.Brightness, sunrise, sunset, next, true);
    }

    // ── Shared computation ────────────────────────────────

    public readonly record struct ScheduleResult(int Kelvin, double Brightness, SchedulePhase Phase, int NextMinute);

    public ScheduleResult ComputeSchedule(DateTime now)
    {
        // "Night starts" means dimming BEGINS at that time and reaches the
        // night value `tranMin` minutes later. Same for "Day starts" in the
        // morning. This matches what the labels say — the scheduled time
        // is the start of the transition, not its end.
        var (dayMin, nightMin, _, _) = ScheduleTimes(now);
        var nowMin = MinutesFromMidnight(now);
        var tranMin = _settings.TransitionMinutes;
        var nightTransEnd = Wrap(nightMin + tranMin);
        var dayTransEnd   = Wrap(dayMin   + tranMin);

        var useBedtime = BedtimeIsActive(dayMin, nightMin);
        var bedMin = Wrap(_settings.BedtimeHour * 60 + _settings.BedtimeMinute);

        var morningFromK = useBedtime ? _settings.BedtimeKelvin     : _settings.NightKelvin;
        var morningFromB = useBedtime ? _settings.BedtimeBrightness : _settings.NightBrightness;

        // TransitionToDay: dayMin .. dayMin + tranMin
        if (InRange(nowMin, dayMin, dayTransEnd))
        {
            var p = Progress(nowMin, dayMin, tranMin);
            return new ScheduleResult(
                Lerp(morningFromK, _settings.DayKelvin, p),
                LerpD(morningFromB, _settings.DayBrightness, p),
                SchedulePhase.TransitionToDay, dayTransEnd);
        }
        // Full Day: dayTransEnd .. nightMin
        if (InRange(nowMin, dayTransEnd, nightMin))
            return new ScheduleResult(_settings.DayKelvin, _settings.DayBrightness, SchedulePhase.Day, nightMin);
        // TransitionToNight: nightMin .. nightMin + tranMin
        if (InRange(nowMin, nightMin, nightTransEnd))
        {
            var p = Progress(nowMin, nightMin, tranMin);
            return new ScheduleResult(
                Lerp(_settings.DayKelvin, _settings.NightKelvin, p),
                LerpD(_settings.DayBrightness, _settings.NightBrightness, p),
                SchedulePhase.TransitionToNight, nightTransEnd);
        }
        // Past nightTransEnd, before dayMin.
        if (useBedtime)
        {
            // Bedtime ramp begins at the configured bedtime and reaches the
            // Bed values bedtimeRampMinutes later — same "starts at the
            // scheduled time" semantic as the day↔night transition.
            // Clamped so the ramp can't overrun the night→day window.
            var nightToDay = Wrap(dayMin - nightTransEnd);
            var bedFromNightEnd = Wrap(bedMin - nightTransEnd);
            // If user picked a bedtime BEFORE the night transition completes,
            // ignore the offset and start the ramp at the configured bed time.
            var rampStart = bedFromNightEnd <= 0 ? nightTransEnd : bedMin;
            var rampLen = Math.Min(_settings.BedtimeRampMinutes,
                                   Math.Max(1, nightToDay - bedFromNightEnd));
            var rampEnd = Wrap(rampStart + rampLen);

            if (InRange(nowMin, nightTransEnd, rampStart))
                return new ScheduleResult(_settings.NightKelvin, _settings.NightBrightness, SchedulePhase.Night, rampStart);
            if (InRange(nowMin, rampStart, rampEnd))
            {
                var p = Progress(nowMin, rampStart, rampLen);
                return new ScheduleResult(
                    Lerp(_settings.NightKelvin, _settings.BedtimeKelvin, p),
                    LerpD(_settings.NightBrightness, _settings.BedtimeBrightness, p),
                    SchedulePhase.RampToBedtime, rampEnd);
            }
            return new ScheduleResult(_settings.BedtimeKelvin, _settings.BedtimeBrightness, SchedulePhase.Bedtime, dayMin);
        }
        return new ScheduleResult(_settings.NightKelvin, _settings.NightBrightness, SchedulePhase.Night, dayMin);
    }

    private bool BedtimeIsActive(int dayMin, int nightMin)
    {
        if (!_settings.BedtimeEnabled) return false;
        var bed = Wrap(_settings.BedtimeHour * 60 + _settings.BedtimeMinute);
        var nightToDay = Wrap(dayMin - nightMin);
        var nightToBed = Wrap(bed - nightMin);
        return nightToBed > 0 && nightToBed < nightToDay;
    }

    private (int DayMin, int NightMin, DateTime? Sunrise, DateTime? Sunset) ScheduleTimes(DateTime date)
    {
        if (_settings.ScheduleMode == "solar")
        {
            var s = SolarCalculator.Calculate(date, _settings.Latitude, _settings.Longitude);
            if (s is not null)
                return (MinutesFromMidnight(s.Value.Sunrise), MinutesFromMidnight(s.Value.Sunset), s.Value.Sunrise, s.Value.Sunset);
        }
        return (_settings.CustomDayHour * 60 + _settings.CustomDayMinute,
                _settings.CustomNightHour * 60 + _settings.CustomNightMinute,
                null, null);
    }

    // ── Circular-clock math ───────────────────────────────

    private static int Wrap(int m) => ((m % 1440) + 1440) % 1440;

    private static bool InRange(int t, int from, int to)
        => from <= to ? (t >= from && t < to) : (t >= from || t < to);

    private static double Progress(int t, int from, int length)
    {
        var elapsed = Wrap(t - from);
        return Math.Min(1, (double)elapsed / Math.Max(1, length));
    }

    // Hermite smoothstep: matches macOS exactly. Same formula as
    // macos/Sources/KelvinShift/ScheduleEngine.swift.
    private static int Lerp(int a, int b, double t)
    {
        var s = t * t * (3 - 2 * t);
        return a + (int)Math.Round((b - a) * s);
    }

    private static double LerpD(double a, double b, double t)
    {
        var s = t * t * (3 - 2 * t);
        return a + (b - a) * s;
    }

    private static int MinutesFromMidnight(DateTime d) => d.Hour * 60 + d.Minute;

    private static DateTime TodayAt(int minutes, DateTime relativeTo)
    {
        var start = relativeTo.Date;
        var d = start.AddMinutes(minutes);
        if (d < relativeTo) d = d.AddDays(1);
        return d;
    }

    private void Publish(SchedulePhase phase, int kelvin, double brightness,
        DateTime? sunrise, DateTime? sunset, DateTime? next, bool enabled)
    {
        State = new ScheduleState(phase, kelvin, brightness,
            _settings.DayKelvin, _settings.NightKelvin,
            _settings.DayBrightness, _settings.NightBrightness,
            _settings.BedtimeEnabled, _settings.BedtimeKelvin, _settings.BedtimeBrightness,
            sunrise, sunset, next, enabled);
        StateChanged?.Invoke(this, State);
    }
}
