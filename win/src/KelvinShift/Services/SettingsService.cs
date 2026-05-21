using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Windows.Threading;

namespace KelvinShift.Services;

// Mirrors macos/Sources/KelvinShift/Settings.swift. Persists to
// %APPDATA%\KelvinShift\settings.json with a 250ms debounced write so
// dragging sliders doesn't hammer disk. Raises SettingsChanged on every
// property change so the schedule engine can re-tick.
public sealed class SettingsService : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;
    public event EventHandler? SettingsChanged;

    private readonly string _path;
    private bool _loading;
    private DispatcherTimer? _saveDebounce;

    public SettingsService()
    {
        var dir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "KelvinShift");
        Directory.CreateDirectory(dir);
        _path = Path.Combine(dir, "settings.json");
    }

    // ── Color Temperature ──────────────────────────────────
    private int _dayKelvin = 5000;
    public int DayKelvin { get => _dayKelvin; set => Set(ref _dayKelvin, Math.Clamp(value, 2000, 6500)); }

    private int _nightKelvin = 2700;
    public int NightKelvin { get => _nightKelvin; set => Set(ref _nightKelvin, Math.Clamp(value, 1800, 5500)); }

    // ── Brightness ────────────────────────────────────────
    private double _dayBrightness = 1.0;
    public double DayBrightness { get => _dayBrightness; set => Set(ref _dayBrightness, Math.Clamp(value, 0.1, 1.0)); }

    private double _nightBrightness = 0.8;
    public double NightBrightness { get => _nightBrightness; set => Set(ref _nightBrightness, Math.Clamp(value, 0.1, 1.0)); }

    // ── Schedule ───────────────────────────────────────────
    private string _scheduleMode = "custom";
    public string ScheduleMode { get => _scheduleMode; set => Set(ref _scheduleMode, value); }

    private int _customDayHour = 7;
    public int CustomDayHour { get => _customDayHour; set => Set(ref _customDayHour, Math.Clamp(value, 0, 23)); }

    private int _customDayMinute = 0;
    public int CustomDayMinute { get => _customDayMinute; set => Set(ref _customDayMinute, Math.Clamp(value, 0, 59)); }

    private int _customNightHour = 20;
    public int CustomNightHour { get => _customNightHour; set => Set(ref _customNightHour, Math.Clamp(value, 0, 23)); }

    private int _customNightMinute = 0;
    public int CustomNightMinute { get => _customNightMinute; set => Set(ref _customNightMinute, Math.Clamp(value, 0, 59)); }

    // ── Location ──────────────────────────────────────────
    private double _latitude;
    public double Latitude { get => _latitude; set => Set(ref _latitude, value); }

    private double _longitude;
    public double Longitude { get => _longitude; set => Set(ref _longitude, value); }

    private string _locationName = "";
    public string LocationName { get => _locationName; set => Set(ref _locationName, value); }

    // ── Transition ─────────────────────────────────────────
    private int _transitionMinutes = 20;
    public int TransitionMinutes { get => _transitionMinutes; set => Set(ref _transitionMinutes, Math.Max(1, value)); }

    // ── Bedtime ───────────────────────────────────────────
    private bool _bedtimeEnabled;
    public bool BedtimeEnabled { get => _bedtimeEnabled; set => Set(ref _bedtimeEnabled, value); }

    private int _bedtimeKelvin = 1900;
    public int BedtimeKelvin { get => _bedtimeKelvin; set => Set(ref _bedtimeKelvin, Math.Clamp(value, 1000, 4000)); }

    private double _bedtimeBrightness = 0.4;
    public double BedtimeBrightness { get => _bedtimeBrightness; set => Set(ref _bedtimeBrightness, Math.Clamp(value, 0.1, 1.0)); }

    private int _bedtimeHour = 23;
    public int BedtimeHour { get => _bedtimeHour; set => Set(ref _bedtimeHour, Math.Clamp(value, 0, 23)); }

    private int _bedtimeMinute;
    public int BedtimeMinute { get => _bedtimeMinute; set => Set(ref _bedtimeMinute, Math.Clamp(value, 0, 59)); }

    private int _bedtimeRampMinutes = 60;
    public int BedtimeRampMinutes { get => _bedtimeRampMinutes; set => Set(ref _bedtimeRampMinutes, Math.Max(1, value)); }

    // ── Master toggle ─────────────────────────────────────
    private bool _enabled = true;
    public bool Enabled { get => _enabled; set => Set(ref _enabled, value); }

    // ── System color pipeline (MHC) ───────────────────────
    // Opt-out toggle for the GPU hardware color transform path. When on
    // (default) and the GPU supports it, color goes through Windows' MHC
    // pipeline — no Action Center flash, no screenshot tint. Off falls
    // back to the gamma ramp path (some flash on shell flyouts possible).
    private bool _useSystemColorPipeline = true;
    public bool UseSystemColorPipeline { get => _useSystemColorPipeline; set => Set(ref _useSystemColorPipeline, value); }

    // ── Launch at Login ───────────────────────────────────
    private bool _launchAtLogin;
    public bool LaunchAtLogin
    {
        get => _launchAtLogin;
        set
        {
            if (Set(ref _launchAtLogin, value))
                LaunchAtLoginService.Apply(value);
        }
    }

    // ── Persistence ───────────────────────────────────────

    public void Load()
    {
        if (!File.Exists(_path)) return;
        try
        {
            _loading = true;
            var dto = JsonSerializer.Deserialize<Dto>(File.ReadAllText(_path));
            if (dto is null) return;
            DayKelvin         = dto.DayKelvin;
            NightKelvin       = dto.NightKelvin;
            DayBrightness     = dto.DayBrightness;
            NightBrightness   = dto.NightBrightness;
            ScheduleMode      = dto.ScheduleMode ?? "custom";
            CustomDayHour     = dto.CustomDayHour;
            CustomDayMinute   = dto.CustomDayMinute;
            CustomNightHour   = dto.CustomNightHour;
            CustomNightMinute = dto.CustomNightMinute;
            Latitude          = dto.Latitude;
            Longitude         = dto.Longitude;
            LocationName      = dto.LocationName ?? "";
            TransitionMinutes = dto.TransitionMinutes == 0 ? 20 : dto.TransitionMinutes;
            BedtimeEnabled    = dto.BedtimeEnabled;
            BedtimeKelvin     = dto.BedtimeKelvin == 0 ? 1900 : dto.BedtimeKelvin;
            BedtimeBrightness = dto.BedtimeBrightness == 0 ? 0.4 : dto.BedtimeBrightness;
            BedtimeHour       = dto.BedtimeHour;
            BedtimeMinute     = dto.BedtimeMinute;
            BedtimeRampMinutes = dto.BedtimeRampMinutes == 0 ? 60 : dto.BedtimeRampMinutes;
            UseSystemColorPipeline = dto.UseSystemColorPipeline;
            Enabled           = dto.Enabled;
            LaunchAtLogin     = LaunchAtLoginService.IsEnabled();
        }
        catch { /* leave defaults */ }
        finally { _loading = false; }
    }

    private void Save()
    {
        var dto = new Dto
        {
            DayKelvin = DayKelvin, NightKelvin = NightKelvin,
            DayBrightness = DayBrightness, NightBrightness = NightBrightness,
            ScheduleMode = ScheduleMode,
            CustomDayHour = CustomDayHour, CustomDayMinute = CustomDayMinute,
            CustomNightHour = CustomNightHour, CustomNightMinute = CustomNightMinute,
            Latitude = Latitude, Longitude = Longitude, LocationName = LocationName,
            TransitionMinutes = TransitionMinutes,
            BedtimeEnabled = BedtimeEnabled,
            BedtimeKelvin = BedtimeKelvin, BedtimeBrightness = BedtimeBrightness,
            BedtimeHour = BedtimeHour, BedtimeMinute = BedtimeMinute,
            BedtimeRampMinutes = BedtimeRampMinutes,
            UseSystemColorPipeline = UseSystemColorPipeline,
            Enabled = Enabled,
        };
        File.WriteAllText(_path, JsonSerializer.Serialize(dto, new JsonSerializerOptions { WriteIndented = true }));
    }

    private void ScheduleSave()
    {
        _saveDebounce ??= new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        _saveDebounce.Stop();
        _saveDebounce.Tick -= OnSaveTick;
        _saveDebounce.Tick += OnSaveTick;
        _saveDebounce.Start();
    }
    private void OnSaveTick(object? s, EventArgs e) { _saveDebounce!.Stop(); Save(); }

    private bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        if (!_loading)
        {
            ScheduleSave();
            SettingsChanged?.Invoke(this, EventArgs.Empty);
        }
        return true;
    }

    public string DayTimeLabel     => FormatTime(CustomDayHour, CustomDayMinute);
    public string NightTimeLabel   => FormatTime(CustomNightHour, CustomNightMinute);
    public string BedtimeTimeLabel => FormatTime(BedtimeHour, BedtimeMinute);

    private static string FormatTime(int h, int m)
    {
        var h12 = h == 0 ? 12 : (h > 12 ? h - 12 : h);
        var sfx = h >= 12 ? "PM" : "AM";
        return $"{h12}:{m:D2} {sfx}";
    }

    private sealed class Dto
    {
        public int DayKelvin { get; set; }
        public int NightKelvin { get; set; }
        public double DayBrightness { get; set; }
        public double NightBrightness { get; set; }
        public string? ScheduleMode { get; set; }
        public int CustomDayHour { get; set; }
        public int CustomDayMinute { get; set; }
        public int CustomNightHour { get; set; }
        public int CustomNightMinute { get; set; }
        public double Latitude { get; set; }
        public double Longitude { get; set; }
        public string? LocationName { get; set; }
        public int TransitionMinutes { get; set; }
        public bool BedtimeEnabled { get; set; }
        public int BedtimeKelvin { get; set; }
        public double BedtimeBrightness { get; set; }
        public int BedtimeHour { get; set; }
        public int BedtimeMinute { get; set; }
        public int BedtimeRampMinutes { get; set; }
        public bool UseSystemColorPipeline { get; set; } = true;
        public bool Enabled { get; set; }
    }
}
