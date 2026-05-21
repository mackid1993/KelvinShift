using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
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

    // Single-instance enforcement. Second launch signals the existing
    // instance to surface its Preferences window, then exits.
    private const string MutexName     = "KelvinShift-SingleInstance-v1";
    private const string ActivateEvent = "KelvinShift-Activate-v1";
    private Mutex? _mutex;
    private EventWaitHandle? _activateHandle;

    private static readonly string LogDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "KelvinShift");
    private static readonly string LogPath = Path.Combine(LogDir, "crash.log");

    protected override void OnStartup(StartupEventArgs e)
    {
        // Elevated registry helper: if launched with the CLI flag (via
        // GammaRangeService.RequestChange's UAC re-launch), perform the
        // HKLM write and exit immediately. Never reaches the UI path.
        if (e.Args.Length > 0 && Array.Exists(e.Args, a =>
                a.Equals(GammaRangeService.CliFlag, StringComparison.OrdinalIgnoreCase)))
        {
            Environment.Exit(GammaRangeService.ApplyFromCli(e.Args));
            return;
        }

        // Uninstall hook: Inno Setup calls us with this flag BEFORE removing
        // files. Reset the gamma ramp to identity so the system reverts to
        // its prior calibration. No UI; exits immediately.
        if (e.Args.Length > 0 && Array.Exists(e.Args, a =>
                a.Equals("--uninstall-cleanup", StringComparison.OrdinalIgnoreCase)))
        {
            try { var svc = new GammaService(); svc.Reset(); svc.Dispose(); } catch { }
            Environment.Exit(0);
            return;
        }


        // Single-instance gate: try to take the named mutex. If we can't, an
        // existing instance is running — signal it to show prefs and exit.
        _mutex = new Mutex(initiallyOwned: true, MutexName, out var createdNew);
        if (!createdNew)
        {
            try
            {
                if (EventWaitHandle.TryOpenExisting(ActivateEvent, out var ev))
                {
                    ev.Set();
                    ev.Dispose();
                }
            }
            catch { }
            Shutdown(0);
            return;
        }
        _activateHandle = new EventWaitHandle(false, EventResetMode.AutoReset, ActivateEvent);
        Task.Run(ListenForActivations);

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

        // Always launch tray-only — click the tray icon to open Preferences.
        base.OnStartup(e);

        try
        {
            Settings = new SettingsService();
            Settings.Load();

            Gamma = new GammaService();
            Engine = new ScheduleEngine(Settings, Gamma);
            Watchdog = new GammaWatchdog(Gamma);
            Engine.Start();

            // When the user toggles "Use system color pipeline" off, tear
            // down any active MHC association immediately rather than waiting
            // for the next scheduled gamma apply.
            Settings.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(SettingsService.UseSystemColorPipeline))
                    Gamma.OnSystemColorPipelineToggled();
            };

            Tray = new TrayIconService(Engine, Settings, ShowPreferences, Quit);
            Tray.Show();
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

    private void ListenForActivations()
    {
        while (true)
        {
            try { _activateHandle?.WaitOne(); } catch { return; }
            Dispatcher.BeginInvoke(new Action(ShowPreferences));
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
        try { Engine?.Stop(); }    catch { }
        try { Watchdog?.Dispose(); } catch { }
        try { Gamma?.Dispose(); }   catch { }
        try { Tray?.Dispose(); }    catch { }
        Current?.Shutdown();
        // Hard guarantee in case a background hook or wait handle is keeping
        // the process alive past Shutdown(). Without this the user sees the
        // tray icon disappear but KelvinShift.exe lingers in Task Manager.
        Environment.Exit(0);
    }
}
