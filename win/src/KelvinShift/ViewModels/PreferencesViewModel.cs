using System;
using System.ComponentModel;
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

    public PreferencesViewModel(SettingsService settings, ScheduleEngine engine)
    {
        Settings = settings;
        Engine = engine;
        ToggleDemoCommand = new RelayCommand(_ => ToggleDemo());
        UseCurrentLocationCommand = new RelayCommand(async _ => await UseCurrentLocationAsync());

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
