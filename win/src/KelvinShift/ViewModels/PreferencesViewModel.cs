using System;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using System.Windows.Input;
using KelvinShift.Services;

namespace KelvinShift.ViewModels;

// ViewModel for PreferencesWindow. Direct property pass-through to
// SettingsService (so live binding works) plus commands for the demo
// toggle and "Use Current Location" button. Preview gestures
// (slider-press → ScheduleEngine.StartPreview) are wired from the View.
public sealed class PreferencesViewModel : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    public SettingsService Settings { get; }
    public ScheduleEngine Engine { get; }

    private bool _isDemoRunning;
    public bool IsDemoRunning
    {
        get => _isDemoRunning;
        private set { _isDemoRunning = value; OnChanged(); OnChanged(nameof(DemoButtonText)); }
    }
    public string DemoButtonText => IsDemoRunning ? "Stop" : "Preview Cycle";

    private double _demoProgress;
    public double DemoProgress { get => _demoProgress; private set { _demoProgress = value; OnChanged(); OnChanged(nameof(DemoProgressPercent)); } }
    public int DemoProgressPercent => (int)(DemoProgress * 100);

    private bool _isLocating;
    public bool IsLocating { get => _isLocating; private set { _isLocating = value; OnChanged(); } }

    private string? _locationError;
    public string? LocationError { get => _locationError; private set { _locationError = value; OnChanged(); } }

    public ICommand ToggleDemoCommand { get; }
    public ICommand UseCurrentLocationCommand { get; }

    // ── Extended warm range (HKLM\...\ICM\GdiIcmGammaRange = 256) ────────
    // Opt-in: lifts the Vista-era gamma validation that otherwise clips warm
    // temperatures below ~3500K. Requires UAC to write HKLM and a sign-out
    // for the kernel to pick up the new value.
    private bool _extendedRange;
    public bool ExtendedRangeEnabled
    {
        get => _extendedRange;
        set
        {
            if (_extendedRange == value) return;
            if (GammaRangeService.RequestChange(value))
            {
                _extendedRange = value;
                OnChanged();
                ExtendedRangeStatus = value
                    ? "Enabled. Sign out and back in (or reboot) for full effect."
                    : "Disabled. Sign out and back in to revert.";
            }
            else
            {
                // UAC declined or write failed — revert the toggle visually
                OnChanged();
            }
        }
    }

    private string? _extendedRangeStatus;
    public string? ExtendedRangeStatus
    {
        get => _extendedRangeStatus;
        private set { _extendedRangeStatus = value; OnChanged(); }
    }

    // ── Time picker bridge properties ────────────────────
    // 3 ComboBoxes per time (Hour 1-12 / Minute 0-55 by 5 / AM-PM).
    // Settings stores 0-23 hours; we convert to 12-hour for display.

    public static int[] Hours12 { get; } = Enumerable.Range(1, 12).ToArray();
    public static int[] Minutes { get; } = Enumerable.Range(0, 12).Select(i => i * 5).ToArray();
    public static string[] AmPmOptions { get; } = new[] { "AM", "PM" };

    private static int ToHour12(int h24) => h24 == 0 ? 12 : (h24 > 12 ? h24 - 12 : h24);
    private static int ToHour24(int h12, bool pm)
    {
        if (h12 == 12) return pm ? 12 : 0;
        return pm ? h12 + 12 : h12;
    }

    public int DayHour12
    {
        get => ToHour12(Settings.CustomDayHour);
        set { Settings.CustomDayHour = ToHour24(value, DayIsPM); OnChanged(); }
    }
    public int DayMinute
    {
        get => Settings.CustomDayMinute - (Settings.CustomDayMinute % 5);
        set { Settings.CustomDayMinute = value; OnChanged(); }
    }
    public bool DayIsPM
    {
        get => Settings.CustomDayHour >= 12;
        set { Settings.CustomDayHour = ToHour24(DayHour12, value); OnChanged(nameof(DayAmPm)); OnChanged(); }
    }
    public string DayAmPm
    {
        get => DayIsPM ? "PM" : "AM";
        set { DayIsPM = (value == "PM"); }
    }

    public int NightHour12
    {
        get => ToHour12(Settings.CustomNightHour);
        set { Settings.CustomNightHour = ToHour24(value, NightIsPM); OnChanged(); }
    }
    public int NightMinute
    {
        get => Settings.CustomNightMinute - (Settings.CustomNightMinute % 5);
        set { Settings.CustomNightMinute = value; OnChanged(); }
    }
    public bool NightIsPM
    {
        get => Settings.CustomNightHour >= 12;
        set { Settings.CustomNightHour = ToHour24(NightHour12, value); OnChanged(nameof(NightAmPm)); OnChanged(); }
    }
    public string NightAmPm
    {
        get => NightIsPM ? "PM" : "AM";
        set { NightIsPM = (value == "PM"); }
    }

    public int BedtimeHour12
    {
        get => ToHour12(Settings.BedtimeHour);
        set { Settings.BedtimeHour = ToHour24(value, BedtimeIsPM); OnChanged(); }
    }
    public int BedtimeMinute
    {
        get => Settings.BedtimeMinute - (Settings.BedtimeMinute % 5);
        set { Settings.BedtimeMinute = value; OnChanged(); }
    }
    public bool BedtimeIsPM
    {
        get => Settings.BedtimeHour >= 12;
        set { Settings.BedtimeHour = ToHour24(BedtimeHour12, value); OnChanged(nameof(BedtimeAmPm)); OnChanged(); }
    }
    public string BedtimeAmPm
    {
        get => BedtimeIsPM ? "PM" : "AM";
        set { BedtimeIsPM = (value == "PM"); }
    }

    public PreferencesViewModel(SettingsService settings, ScheduleEngine engine)
    {
        Settings = settings;
        Engine = engine;
        ToggleDemoCommand = new RelayCommand(_ => ToggleDemo());
        UseCurrentLocationCommand = new RelayCommand(async _ => await UseCurrentLocationAsync());

        // Init extended-range toggle from current HKLM state
        _extendedRange = GammaRangeService.IsEnabled();

        Engine.DemoProgressChanged += p =>
        {
            DemoProgress = p;
            IsDemoRunning = Engine.IsDemoRunning;
        };
    }

    private void ToggleDemo()
    {
        if (Engine.IsDemoRunning) Engine.StopDemo();
        else                       Engine.StartDemo();
        IsDemoRunning = Engine.IsDemoRunning;
    }

    private async Task UseCurrentLocationAsync()
    {
        IsLocating = true;
        LocationError = null;
        try
        {
            var pos = await LocationService.GetCurrentAsync();
            if (pos is null)
            {
                LocationError = "Location unavailable. Check Windows location permissions for KelvinShift.";
                return;
            }
            Settings.Latitude = pos.Value.Lat;
            Settings.Longitude = pos.Value.Lon;
            Settings.LocationName = $"{pos.Value.Lat:F2}, {pos.Value.Lon:F2}";
        }
        finally { IsLocating = false; }
    }

    private void OnChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    private sealed class RelayCommand : ICommand
    {
        private readonly Action<object?> _exec;
        public RelayCommand(Action<object?> exec) => _exec = exec;
        public bool CanExecute(object? parameter) => true;
        public void Execute(object? parameter) => _exec(parameter);
        public event EventHandler? CanExecuteChanged { add { } remove { } }
    }
}
