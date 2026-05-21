using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using H.NotifyIcon;

namespace KelvinShift.Services;

// Tray icon mirrors macos/Sources/KelvinShift/StatusBarController.swift:
// a state-aware glyph (sun/transition/moon/bed/off), regenerated on every
// state change, with tooltip showing current Kelvin + brightness. Click
// opens Preferences; right-click shows context menu.
//
// Uses GDI+ (System.Drawing) for icon rendering — the canonical Windows
// tray path that produces reliable HICONs. Falls back to the static
// AppIcon if dynamic rendering fails so the tray never goes blank.
public sealed class TrayIconService : IDisposable
{
    private readonly TaskbarIcon _icon;
    private readonly ScheduleEngine _engine;
    private readonly SettingsService _settings;
    private readonly Action _openPrefs;
    private readonly Action _quit;
    private readonly Icon? _fallbackIcon;
    private Icon? _currentIcon;

    private MenuItem _miCurrent = null!, _miPhase = null!, _miDay = null!, _miNight = null!;
    private MenuItem _miBedtime = null!, _miSchedule = null!, _miEnabled = null!;

    public TrayIconService(ScheduleEngine engine, SettingsService settings, Action openPrefs, Action quit)
    {
        _engine = engine;
        _settings = settings;
        _openPrefs = openPrefs;
        _quit = quit;

        _fallbackIcon = TryLoadAppIcon();

        _icon = new TaskbarIcon
        {
            ToolTipText = "KelvinShift",
            ContextMenu = BuildMenu(),
            LeftClickCommand = new RelayCommand(_ => _openPrefs()),
            NoLeftClickDelay = true,
        };

        engine.StateChanged += (_, _) => Refresh();
        settings.SettingsChanged += (_, _) => Refresh();
    }

    public void Show()
    {
        // Force the icon to be created so it shows up even before the first
        // state change. Set a static fallback first so it never appears blank.
        if (_fallbackIcon is not null)
            _icon.Icon = _fallbackIcon;

        _icon.ForceCreate();
        Refresh();
    }

    public void Dispose()
    {
        _icon.Dispose();
        DisposeCurrentIcon();
        _fallbackIcon?.Dispose();
    }

    // ── Menu ──────────────────────────────────────────────

    private ContextMenu BuildMenu()
    {
        var m = new ContextMenu();
        _miCurrent  = DisabledItem(); m.Items.Add(_miCurrent);
        _miPhase    = DisabledItem(); m.Items.Add(_miPhase);
        m.Items.Add(new Separator());
        _miDay      = DisabledItem(); m.Items.Add(_miDay);
        _miNight    = DisabledItem(); m.Items.Add(_miNight);
        _miBedtime  = DisabledItem(); m.Items.Add(_miBedtime);
        _miSchedule = DisabledItem(); m.Items.Add(_miSchedule);
        m.Items.Add(new Separator());

        _miEnabled = new MenuItem { Header = "Enabled", IsCheckable = true };
        _miEnabled.Click += (_, _) => _settings.Enabled = !_settings.Enabled;
        m.Items.Add(_miEnabled);
        m.Items.Add(new Separator());

        var pref = new MenuItem { Header = "Preferences…" };
        pref.Click += (_, _) => _openPrefs();
        m.Items.Add(pref);

        var quit = new MenuItem { Header = "Quit KelvinShift" };
        quit.Click += (_, _) => _quit();
        m.Items.Add(quit);

        return m;
    }

    private static MenuItem DisabledItem() => new() { IsEnabled = false };

    // ── Refresh ───────────────────────────────────────────

    private void Refresh()
    {
        var s = _engine.State;

        var newIcon = TryRenderStateIcon(s) ?? _fallbackIcon;
        if (newIcon is not null && newIcon != _currentIcon)
        {
            _icon.Icon = newIcon;
            DisposeCurrentIcon();
            _currentIcon = newIcon == _fallbackIcon ? null : newIcon;
        }

        _icon.ToolTipText = s.Enabled
            ? $"KelvinShift — {s.CurrentKelvin}K · {(int)Math.Round(s.CurrentBrightness * 100)}%"
            : "KelvinShift — Off";

        var curBrtPct   = (int)Math.Round(s.CurrentBrightness * 100);
        var dayBrtPct   = (int)Math.Round(s.DayBrightness * 100);
        var nightBrtPct = (int)Math.Round(s.NightBrightness * 100);
        var bedBrtPct   = (int)Math.Round(s.BedtimeBrightness * 100);

        _miCurrent.Header  = $"Current: {s.CurrentKelvin} K  ·  {curBrtPct}%";
        _miPhase.Header    = PhaseLabel(s.Phase);
        _miDay.Header      = $"☀  Day:     {s.DayKelvin} K  ·  {dayBrtPct}%";
        _miNight.Header    = $"☾  Night:   {s.NightKelvin} K  ·  {nightBrtPct}%";
        _miBedtime.Header  = $"🛏  Bedtime: {s.BedtimeKelvin} K  ·  {bedBrtPct}%";
        _miBedtime.Visibility = s.BedtimeEnabled ? Visibility.Visible : Visibility.Collapsed;
        _miSchedule.Header = ScheduleLabel(s);
        _miEnabled.IsChecked = s.Enabled;
    }

    private static string PhaseLabel(SchedulePhase p) => p switch
    {
        SchedulePhase.Day               => "☀  Daytime",
        SchedulePhase.Night             => "☾  Nighttime",
        SchedulePhase.TransitionToNight => "☀→☾  Transitioning to Night",
        SchedulePhase.TransitionToDay   => "☾→☀  Transitioning to Day",
        SchedulePhase.RampToBedtime     => "☾→🛏  Ramping to Bedtime",
        SchedulePhase.Bedtime           => "🛏  Bedtime",
        _ => p.ToString()
    };

    private string ScheduleLabel(ScheduleState s)
    {
        if (_settings.ScheduleMode == "solar")
        {
            var r = s.SunriseTime?.ToString("h:mm tt") ?? "–";
            var t = s.SunsetTime?.ToString("h:mm tt")  ?? "–";
            var bed = _settings.BedtimeEnabled ? $"  🛏{_settings.BedtimeTimeLabel}" : "";
            return $"Schedule: Solar  ↑{r}  ↓{t}{bed}";
        }
        var bedSuf = _settings.BedtimeEnabled ? $" · 🛏{_settings.BedtimeTimeLabel}" : "";
        return $"Schedule: {_settings.DayTimeLabel} – {_settings.NightTimeLabel}{bedSuf}";
    }

    // ── Icon rendering (GDI+) ─────────────────────────────

    private void DisposeCurrentIcon()
    {
        if (_currentIcon is null) return;
        try { DestroyIcon(_currentIcon.Handle); } catch { }
        _currentIcon.Dispose();
        _currentIcon = null;
    }

    private static Icon? TryRenderStateIcon(ScheduleState s)
    {
        try
        {
            // 32x32 is the standard tray icon size on modern Windows; the OS
            // scales for higher-DPI variants. Hand-drawn vector shapes via GDI+
            // (filled paths, no font glyphs) for a clean modern look that
            // stays crisp at any size — not Wingdings/Symbol-font glyphs.
            const int size = 32;
            using var bmp = new Bitmap(size, size, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.CompositingQuality = CompositingQuality.HighQuality;

                if (!s.Enabled)
                {
                    DrawOff(g, size);
                }
                else
                {
                    switch (s.Phase)
                    {
                        case SchedulePhase.Day:
                            DrawSun(g, size, Color.FromArgb(0xFF, 0xC8, 0x4A));
                            break;
                        case SchedulePhase.TransitionToNight:
                            DrawSun(g, size, Color.FromArgb(0xFF, 0x9A, 0x4A));
                            break;
                        case SchedulePhase.Night:
                            DrawMoon(g, size, Color.FromArgb(0xA8, 0xC8, 0xFF));
                            break;
                        case SchedulePhase.TransitionToDay:
                            DrawMoon(g, size, Color.FromArgb(0xFF, 0xC0, 0x80));
                            break;
                        case SchedulePhase.RampToBedtime:
                            DrawBed(g, size, Color.FromArgb(0xC8, 0x80, 0xFF));
                            break;
                        case SchedulePhase.Bedtime:
                            DrawBed(g, size, Color.FromArgb(0xA0, 0x60, 0xE0));
                            break;
                    }
                }
            }

            var hicon = bmp.GetHicon();
            // Icon.FromHandle does not take ownership; we destroy hicon in
            // DisposeCurrentIcon. (Disposing the Icon alone leaks the handle.)
            return Icon.FromHandle(hicon);
        }
        catch
        {
            return null;
        }
    }

    // ── Vector glyphs ─────────────────────────────────────
    //
    // All glyphs target a 32×32 canvas with the visual centered on (16,16).
    // Stroke widths and proportions chosen to remain readable when Windows
    // scales the icon down to 16px on low-DPI taskbars.

    private static void DrawSun(Graphics g, int size, Color color)
    {
        var cx = size / 2f;
        var cy = size / 2f;
        using var fill = new SolidBrush(color);
        using var pen = new Pen(color, 2.4f) { StartCap = LineCap.Round, EndCap = LineCap.Round };

        // Center disc
        var r = 5.5f;
        g.FillEllipse(fill, cx - r, cy - r, r * 2, r * 2);

        // 8 evenly spaced rays
        const int rays = 8;
        var inner = 8.5f;
        var outer = 13.5f;
        for (var i = 0; i < rays; i++)
        {
            var a = i * Math.PI * 2 / rays;
            var x1 = cx + (float)Math.Cos(a) * inner;
            var y1 = cy + (float)Math.Sin(a) * inner;
            var x2 = cx + (float)Math.Cos(a) * outer;
            var y2 = cy + (float)Math.Sin(a) * outer;
            g.DrawLine(pen, x1, y1, x2, y2);
        }
    }

    private static void DrawMoon(Graphics g, int size, Color color)
    {
        var cx = size / 2f;
        var cy = size / 2f;
        using var fill = new SolidBrush(color);

        // Crescent = outer filled circle clipped by an offset circle that
        // bites out the lit edge. Drawn into a temporary path for cleanliness.
        using var path = new GraphicsPath();
        var outerR = 11f;
        path.AddEllipse(cx - outerR, cy - outerR, outerR * 2, outerR * 2);

        // Punch out the offset disc to form the crescent
        using var biteRegion = new Region(path);
        using var bitePath = new GraphicsPath();
        var biteR = 10f;
        var biteOffsetX = 5.0f;
        var biteOffsetY = -2.5f;
        bitePath.AddEllipse(cx - biteR + biteOffsetX, cy - biteR + biteOffsetY, biteR * 2, biteR * 2);
        biteRegion.Exclude(bitePath);

        g.FillRegion(fill, biteRegion);
    }

    private static void DrawBed(Graphics g, int size, Color color)
    {
        var cx = size / 2f;
        using var fill = new SolidBrush(color);
        using var pen = new Pen(color, 2.4f) { StartCap = LineCap.Round, EndCap = LineCap.Round };

        // Headboard-and-mattress silhouette: a tall left post, a low mattress,
        // and a small pillow on top.
        //
        //   ┃▔▔▔▔▔▔▔▔▔▔▔▔
        //   ┃ ▁▁▁▁▁▁▁▁▁▁▁▁▁
        //   ┃▕░░░░░░░░░░░░░▏
        //   ┗━━━━━━━━━━━━━━

        // Mattress base (rounded rect)
        var mattressLeft   = 5f;
        var mattressRight  = size - 3f;
        var mattressTop    = 17f;
        var mattressBottom = 24f;
        using (var mattress = RoundedRect(mattressLeft, mattressTop, mattressRight - mattressLeft, mattressBottom - mattressTop, 2.5f))
            g.FillPath(fill, mattress);

        // Pillow on top
        var pillowLeft   = 8.5f;
        var pillowRight  = 16.5f;
        var pillowTop    = 13.5f;
        var pillowBottom = 17f;
        using (var pillow = RoundedRect(pillowLeft, pillowTop, pillowRight - pillowLeft, pillowBottom - pillowTop, 1.5f))
            g.FillPath(fill, pillow);

        // Headboard post
        g.FillRectangle(fill, 4f, 8f, 3f, 16f);

        // Base feet
        g.FillRectangle(fill, 4f, 23.5f, 3f, 3.5f);
        g.FillRectangle(fill, size - 7f, 23.5f, 3f, 3.5f);
    }

    private static void DrawOff(Graphics g, int size)
    {
        var cx = size / 2f;
        var cy = size / 2f;
        var color = Color.FromArgb(0x88, 0xAA, 0xAA, 0xAA);
        using var pen = new Pen(color, 2.4f) { StartCap = LineCap.Round, EndCap = LineCap.Round };

        // Circle with diagonal slash
        var r = 11f;
        g.DrawEllipse(pen, cx - r, cy - r, r * 2, r * 2);
        // Slash at 45°
        var d = r / (float)Math.Sqrt(2);
        g.DrawLine(pen, cx - d, cy + d, cx + d, cy - d);
    }

    private static GraphicsPath RoundedRect(float x, float y, float w, float h, float r)
    {
        var path = new GraphicsPath();
        r = Math.Min(r, Math.Min(w, h) / 2f);
        path.AddArc(x,           y,           r * 2, r * 2, 180, 90);
        path.AddArc(x + w - r * 2, y,         r * 2, r * 2, 270, 90);
        path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2,   0, 90);
        path.AddArc(x,           y + h - r * 2, r * 2, r * 2,  90, 90);
        path.CloseFigure();
        return path;
    }

    private static Icon? TryLoadAppIcon()
    {
        try
        {
            // Embedded as a Resource via the csproj — reachable via pack URI in
            // WPF, or via assembly resource stream here.
            var asm = Assembly.GetExecutingAssembly();
            var resourceName = asm.GetName().Name + ".g.resources";
            // Simpler path: use the .ico file from the executable's directory.
            var exeDir = AppContext.BaseDirectory;
            var icoPath = Path.Combine(exeDir, "AppIcon.ico");
            if (File.Exists(icoPath))
                return new Icon(icoPath);

            // Fall back to PE-embedded icon
            return Icon.ExtractAssociatedIcon(Environment.ProcessPath ?? "");
        }
        catch
        {
            return null;
        }
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyIcon(IntPtr handle);

    private sealed class RelayCommand : ICommand
    {
        private readonly Action<object?> _exec;
        public RelayCommand(Action<object?> exec) => _exec = exec;
        public bool CanExecute(object? parameter) => true;
        public void Execute(object? parameter) => _exec(parameter);
        public event EventHandler? CanExecuteChanged { add { } remove { } }
    }
}
