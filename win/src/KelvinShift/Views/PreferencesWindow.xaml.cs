using System.Windows;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using KelvinShift.Services;
using KelvinShift.ViewModels;
using Wpf.Ui.Controls;

namespace KelvinShift.Views;

public partial class PreferencesWindow : FluentWindow
{
    private string? _pressedSlider; // "dayK", "nightK", "bedK", "dayB", "nightB", "bedB"

    public PreferencesWindow()
    {
        InitializeComponent();
    }

    private PreferencesViewModel Vm => (PreferencesViewModel)DataContext;

    // ── Slider preview gestures ───────────────────────────
    //
    // PreviewMouseLeftButtonDown fires before the slider has updated, so we
    // capture the *current* settings value. The Slider's Value binding then
    // updates Settings on each mouse move, which fires our SettingsChanged →
    // ScheduleEngine subscriber. But during preview we want the gamma to
    // follow the slider directly without going through the normal tick path.
    // So we toggle a flag and the SettingsChanged handler bails when set.

    private void DayK_Down(object s, MouseButtonEventArgs e)   { _pressedSlider = "dayK";  Vm.Engine.StartPreview(Vm.Settings.DayKelvin); }
    private void DayK_Up(object s, MouseButtonEventArgs e)     { _pressedSlider = null;    Vm.Engine.StopPreview(); }
    private void NightK_Down(object s, MouseButtonEventArgs e) { _pressedSlider = "nightK"; Vm.Engine.StartPreview(Vm.Settings.NightKelvin); }
    private void NightK_Up(object s, MouseButtonEventArgs e)   { _pressedSlider = null;     Vm.Engine.StopPreview(); }
    private void BedK_Down(object s, MouseButtonEventArgs e)   { _pressedSlider = "bedK";  Vm.Engine.StartPreview(Vm.Settings.BedtimeKelvin); }
    private void BedK_Up(object s, MouseButtonEventArgs e)     { _pressedSlider = null;    Vm.Engine.StopPreview(); }

    private void DayB_Down(object s, MouseButtonEventArgs e)   { _pressedSlider = "dayB";   Vm.Engine.StartBrightnessPreview(Vm.Settings.DayBrightness); }
    private void DayB_Up(object s, MouseButtonEventArgs e)     { _pressedSlider = null;     Vm.Engine.StopPreview(); }
    private void NightB_Down(object s, MouseButtonEventArgs e) { _pressedSlider = "nightB"; Vm.Engine.StartBrightnessPreview(Vm.Settings.NightBrightness); }
    private void NightB_Up(object s, MouseButtonEventArgs e)   { _pressedSlider = null;     Vm.Engine.StopPreview(); }
    private void BedB_Down(object s, MouseButtonEventArgs e)   { _pressedSlider = "bedB";   Vm.Engine.StartBrightnessPreview(Vm.Settings.BedtimeBrightness); }
    private void BedB_Up(object s, MouseButtonEventArgs e)     { _pressedSlider = null;     Vm.Engine.StopPreview(); }

    protected override void OnSourceInitialized(System.EventArgs e)
    {
        base.OnSourceInitialized(e);
        Vm.Settings.PropertyChanged += (_, args) =>
        {
            if (_pressedSlider is null) return;
            switch (_pressedSlider)
            {
                case "dayK"  when args.PropertyName == nameof(Vm.Settings.DayKelvin):   Vm.Engine.UpdatePreview(Vm.Settings.DayKelvin); break;
                case "nightK" when args.PropertyName == nameof(Vm.Settings.NightKelvin): Vm.Engine.UpdatePreview(Vm.Settings.NightKelvin); break;
                case "bedK"  when args.PropertyName == nameof(Vm.Settings.BedtimeKelvin): Vm.Engine.UpdatePreview(Vm.Settings.BedtimeKelvin); break;
                case "dayB"  when args.PropertyName == nameof(Vm.Settings.DayBrightness):   Vm.Engine.UpdateBrightnessPreview(Vm.Settings.DayBrightness); break;
                case "nightB" when args.PropertyName == nameof(Vm.Settings.NightBrightness): Vm.Engine.UpdateBrightnessPreview(Vm.Settings.NightBrightness); break;
                case "bedB"  when args.PropertyName == nameof(Vm.Settings.BedtimeBrightness): Vm.Engine.UpdateBrightnessPreview(Vm.Settings.BedtimeBrightness); break;
            }
        };
    }

    protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
    {
        // Hide instead of close — Quit only via tray menu
        e.Cancel = true;
        Hide();
    }
}
