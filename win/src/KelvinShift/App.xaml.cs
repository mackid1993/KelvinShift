using System;
using System.IO;
using System.Windows;
using System.Windows.Threading;
using KelvinShift.Services;
using KelvinShift.ViewModels;
using KelvinShift.Views;

namespace KelvinShift;

public partial class App : Application
{
    public static SettingsService Settings { get; private set; } = null!;
    public static GammaService Gamma { get; private set; } = null!;
    public static GammaWatchdog Watchdog { get; private set; } = null!;
    public static ScheduleEngine Engine { get; private set; } = null!;
    public static TrayIconService Tray { get; private set; } = null!;

    private static PreferencesWindow? _prefs;
    private bool _startMinimized;

    private static readonly string LogDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "KelvinShift");
    private static readonly string LogPath = Path.Combine(LogDir, "crash.log");

    protected override void OnStartup(StartupEventArgs e)
    {
        // Catch every unhandled exception path so a crash leaves a breadcrumb.
        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
            LogCrash("AppDomain.UnhandledException", args.ExceptionObject as Exception);
        DispatcherUnhandledException += (_, args) =>
        {
            LogCrash("Dispatcher.UnhandledException", args.Exception);
            // Let the default handler still kill the process — at least we logged.
        };
        System.Threading.Tasks.TaskScheduler.UnobservedTaskException += (_, args) =>
        {
            LogCrash("TaskScheduler.UnobservedTaskException", args.Exception);
            args.SetObserved();
        };

        base.OnStartup(e);
        _startMinimized = e.Args.Length > 0 &&
            Array.Exists(e.Args, a => a.Equals("--tray", StringComparison.OrdinalIgnoreCase));

        try
        {
            Settings = new SettingsService();
            Settings.Load();

            Gamma = new GammaService();
            Engine = new ScheduleEngine(Settings, Gamma);
            Watchdog = new GammaWatchdog(Gamma);
            Engine.Start();

            Tray = new TrayIconService(Engine, Settings, ShowPreferences, Quit);
            Tray.Show();

            if (!_startMinimized)
                ShowPreferences();
        }
        catch (Exception ex)
        {
            LogCrash("OnStartup", ex);
            MessageBox.Show(
                $"KelvinShift failed to start:\n\n{ex.Message}\n\nDetails written to:\n{LogPath}",
                "KelvinShift", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
        }
    }

    private static void LogCrash(string source, Exception? ex)
    {
        try
        {
            Directory.CreateDirectory(LogDir);
            File.AppendAllText(LogPath,
                $"--- {DateTime.Now:O} [{source}] ---\n{ex}\n\n");
        }
        catch { /* nothing we can do */ }

        // Critical: don't strand the user with a warm-tinted screen and no app.
        // If the gamma was applied before the crash, reset it back to defaults
        // so they don't have to sign out / use another tool to recover.
        try { Gamma?.Reset(); } catch { }
    }

    private static void ShowPreferences()
    {
        if (_prefs is null)
        {
            _prefs = new PreferencesWindow { DataContext = new PreferencesViewModel(Settings, Engine) };
        }
        _prefs.Show();
        _prefs.WindowState = WindowState.Normal;
        _prefs.Activate();
    }

    private static void Quit()
    {
        Engine?.Stop();
        Watchdog?.Dispose();
        Gamma?.Dispose();
        Tray?.Dispose();
        Current.Shutdown();
    }
}
