using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Threading;
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

        // Skip refresh while the menu is open — re-measuring during user
        // interaction stalls the popup and the user closes it within seconds
        // anyway, so any stale value is short-lived.
        engine.StateChanged += (_, _) => { if (!IsMenuOpen()) Refresh(); };
        settings.SettingsChanged += (_, _) => { if (!IsMenuOpen()) Refresh(); };
    }

    public void Show()
    {
        // Force the icon to be created so it shows up even before the first
        // state change. Set a static fallback first so it never appears blank.
        if (_fallbackIcon is not null)
            _icon.Icon = _fallbackIcon;

        _icon.ForceCreate();
        Refresh();

        // Pre-warm the context menu off-screen at idle so the first user
        // right-click doesn't pay for popup template parsing / first-time
        // layout. The WPF popup HWND is destroyed on close, so this only
        // helps the first show — but first-show JIT is the heaviest hit.
        Application.Current?.Dispatcher.BeginInvoke(
            DispatcherPriority.ApplicationIdle, new Action(PrewarmContextMenu));
    }

    private bool IsMenuOpen() => _icon.ContextMenu is { IsOpen: true };

    private void PrewarmContextMenu()
    {
        if (_icon.ContextMenu is not ContextMenu m) return;
        try
        {
            m.Placement = PlacementMode.AbsolutePoint;
            m.HorizontalOffset = -10000;
            m.VerticalOffset   = -10000;
            m.IsOpen = true;
            m.IsOpen = false;
            m.ClearValue(ContextMenu.PlacementProperty);
            m.ClearValue(ContextMenu.HorizontalOffsetProperty);
            m.ClearValue(ContextMenu.VerticalOffsetProperty);
        }
        catch { }
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
        var m = new ContextMenu
        {
            // Transparent menu background so the Mica backdrop on the popup
            // HWND below shows through. The MenuItems still render their
            // own opaque hover/text — only the menu fill itself is gone.
            Background = System.Windows.Media.Brushes.Transparent,
            // Skip the default WPF drop-shadow render — Mica already gives
            // the popup its visual elevation and the shadow adds noticeable
            // DWM work on every open.
            HasDropShadow = false,
        };

        // Enlarge every menu item AND make the entire padded row hit-test
        // properly. WPF hit-testing skips any pixel with a null Background
        // brush — that's what was making the padding area feel "dead"
        // (visually it's the row, but clicks there fell through). Setting
        // Transparent (zero-alpha but non-null) makes the whole 36px tall
        // row a click target while still letting the Mica backdrop show
        // through. Header alignment Stretch makes the highlighted hover
        // bar span the full popup width.
        var bigItemStyle = new System.Windows.Style(typeof(MenuItem));
        bigItemStyle.Setters.Add(new System.Windows.Setter(
            MenuItem.FontSizeProperty, 14.0));
        bigItemStyle.Setters.Add(new System.Windows.Setter(
            MenuItem.PaddingProperty, new System.Windows.Thickness(16, 10, 16, 10)));
        bigItemStyle.Setters.Add(new System.Windows.Setter(
            MenuItem.BackgroundProperty, System.Windows.Media.Brushes.Transparent));
        bigItemStyle.Setters.Add(new System.Windows.Setter(
            MenuItem.HorizontalContentAlignmentProperty,
            System.Windows.HorizontalAlignment.Stretch));
        m.Resources.Add(typeof(MenuItem), bigItemStyle);
        // Apply Mica backdrop + dark mode on Loaded rather than Opened —
        // Loaded fires after the popup HWND is created but BEFORE Windows
        // shows it, so the menu's first visible frame already has the
        // backdrop. Using Opened (after the popup is visible) caused a
        // perceptible flicker as DWM applied Mica a frame late.
        m.Loaded += (sender, _) =>
        {
            if (sender is not ContextMenu cm) return;
            var source = System.Windows.PresentationSource.FromVisual(cm) as System.Windows.Interop.HwndSource;
            if (source?.Handle is { } hwnd && hwnd != IntPtr.Zero)
            {
                ApplyDarkMode(hwnd);
                ApplyMicaBackdrop(hwnd);
            }
        };
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
            // Render at 64x64 (not 32) so Windows downscales with proper
            // anti-aliasing for any DPI. At 32 our composite transition
            // glyphs become illegible when the taskbar downscales to 16-20px;
            // 64x64 source preserves enough detail to stay readable.
            const int size = 64;
            using var bmp = new Bitmap(size, size, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.CompositingQuality = CompositingQuality.HighQuality;
                // All DrawX helpers were tuned for a virtual 32px canvas;
                // scale the graphics context so they still work at any size.
                g.ScaleTransform(size / 32f, size / 32f);
                const int vsize = 32;

                if (!s.Enabled)
                {
                    DrawOff(g, vsize);
                }
                else
                {
                    // Palette tuned for warm-gamma stability. Warm gamma kills
                    // blue and reduces green, so any color with high B (blue,
                    // purple, cyan) hue-shifts to reddish/orange — looks
                    // "funky", not intentional. We use colors that are
                    // already in the R/G plane: they look the same under warm
                    // gamma as under neutral, just slightly more saturated.
                    //
                    //   sun   — warm gold (unchanged, already R/G)
                    //   moon  — warm cream / moonlight, NOT cool blue
                    //   bed   — warm amber, NOT purple
                    //   trans — warm orange / soft yellow (unchanged)
                    //
                    // Shape carries the day/night/bedtime meaning; color just
                    // adds glanceability. With this palette nothing hue-shifts
                    // when MHC/gamma pushes the display warm.
                    var sun  = Color.FromArgb(0xFF, 0xC8, 0x4A); // warm gold
                    var moon = Color.FromArgb(0xF0, 0xE0, 0xC0); // warm cream
                    var bed  = Color.FromArgb(0xE8, 0x90, 0x58); // warm amber
                    var transToNight = Color.FromArgb(0xFF, 0xA8, 0x60); // warm orange
                    var transToDay   = Color.FromArgb(0xFF, 0xD8, 0x80); // soft yellow

                    switch (s.Phase)
                    {
                        case SchedulePhase.Day:
                            DrawSun(g, vsize, sun);
                            break;
                        case SchedulePhase.Night:
                            DrawMoon(g, vsize, moon);
                            break;
                        case SchedulePhase.Bedtime:
                            DrawBed(g, vsize, bed);
                            break;
                        case SchedulePhase.TransitionToNight:
                            DrawSunMoon(g, vsize, transToNight);
                            break;
                        case SchedulePhase.TransitionToDay:
                            DrawSunMoon(g, vsize, transToDay);
                            break;
                        case SchedulePhase.RampToBedtime:
                            DrawSleepingMoon(g, vsize, moon, bed);
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

    private enum Glyph { Sun, Moon, Bed, SunMoon }

    // Composite sun+moon for day↔night transitions: sun lower-left, crescent
    // moon upper-right. Bumped to 0.75/0.65 scale so it stays clearly visible
    // at 16-20px tray downscale. Color tint = direction (warm orange to night,
    // soft yellow to day).
    private static void DrawSunMoon(Graphics g, int size, Color color)
    {
        var st = g.Save();
        g.TranslateTransform(-size * 0.20f, size * 0.20f);
        g.ScaleTransform(0.75f, 0.75f);
        DrawSun(g, size, color);
        g.Restore(st);

        st = g.Save();
        g.TranslateTransform(size * 0.25f, -size * 0.25f);
        g.ScaleTransform(0.65f, 0.65f);
        DrawMoon(g, size, color);
        g.Restore(st);
    }

    // 4-digit Kelvin value fills the whole canvas. The "K" suffix that mac
    // shows is sacrificed for tray readability — at 16-20px the extra char
    // would cap font size at ~14px (illegible). Without K, the digits
    // render at ~22px in the 32px virtual canvas (~11px after downscale to
    // 16px tray). The K is implicit; full tooltip shows "5000K · 100%".
    private static void DrawGlyphAndKelvin(Graphics g, int vsize, Glyph glyph, Color color, int kelvin)
    {
        var text = kelvin.ToString(System.Globalization.CultureInfo.InvariantCulture);
        var familyName =
            IsFontInstalled("Bahnschrift Condensed") ? "Bahnschrift Condensed" :
            IsFontInstalled("Bahnschrift")           ? "Bahnschrift"           :
            "Segoe UI";

        using var brush = new SolidBrush(color);
        g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAlias;

        // Auto-size to fill ~95% of canvas
        var size = vsize * 1.05f;
        Font? font = null;
        SizeF measured = default;
        for (var i = 0; i < 12; i++)
        {
            font?.Dispose();
            font = new Font(familyName, size, System.Drawing.FontStyle.Bold, GraphicsUnit.Pixel);
            measured = g.MeasureString(text, font, int.MaxValue, StringFormat.GenericTypographic);
            if (measured.Width <= vsize * 0.96f && measured.Height <= vsize * 0.92f) break;
            size *= 0.9f;
        }
        if (font is null) return;
        try
        {
            var x = (vsize - measured.Width) / 2f;
            var y = (vsize - measured.Height) / 2f;
            g.DrawString(text, font, brush, x, y, StringFormat.GenericTypographic);
        }
        finally { font.Dispose(); }

        // Small phase indicator dot in top-left — gives the glance-only
        // signal "day/night/bed" without competing with the text.
        var dotColor = glyph switch
        {
            Glyph.Sun     => Color.FromArgb(0xFF, 0xC8, 0x4A),  // warm gold
            Glyph.Moon    => Color.FromArgb(0xF0, 0xE0, 0xC0),  // warm cream
            Glyph.Bed     => Color.FromArgb(0xE8, 0x90, 0x58),  // warm amber
            Glyph.SunMoon => Color.FromArgb(0xFF, 0xA8, 0x60),  // warm orange
            _ => color,
        };
        using var dotBrush = new SolidBrush(dotColor);
        var dotR = vsize * 0.11f;
        g.FillEllipse(dotBrush, vsize * 0.03f, vsize * 0.03f, dotR * 2, dotR * 2);
    }

    private static bool IsFontInstalled(string name)
    {
        using var col = new System.Drawing.Text.InstalledFontCollection();
        foreach (var f in col.Families)
            if (string.Equals(f.Name, name, StringComparison.OrdinalIgnoreCase)) return true;
        return false;
    }

    // Transition icon: main (source) glyph at near-full size dominates so
    // it's readable at any tray size, plus a small destination badge in
    // the bottom-right corner showing what we're transitioning TO. Far
    // more legible than two equally-sized half-glyphs at small sizes.
    // (arrowColor param kept for ABI compatibility; unused in this design.)
    private static void DrawTransition(Graphics g, int size, Color srcColor, Color dstColor,
        Color arrowColor, Glyph src, Glyph dst)
    {
        // Main glyph at 0.85 scale, top-left so the badge has room
        var state = g.Save();
        g.TranslateTransform(-size * 0.07f, -size * 0.07f);
        g.ScaleTransform(0.85f, 0.85f);
        DrawGlyph(g, size, srcColor, src);
        g.Restore(state);

        // Destination badge in bottom-right corner at ~0.50 scale
        state = g.Save();
        g.TranslateTransform(size * 0.50f, size * 0.50f);
        g.ScaleTransform(0.50f, 0.50f);
        DrawGlyph(g, size, dstColor, dst);
        g.Restore(state);
    }

    private static void DrawGlyph(Graphics g, int size, Color color, Glyph glyph)
    {
        switch (glyph)
        {
            case Glyph.Sun:     DrawSun(g, size, color); break;
            case Glyph.Moon:    DrawMoon(g, size, color); break;
            case Glyph.Bed:     DrawBed(g, size, color); break;
            case Glyph.SunMoon: DrawSunMoon(g, size, color); break;
        }
    }

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

    // Night → bedtime ramp: moon at near-full size with a single bold Z
    // floating above. The Z is universally recognized as "sleep / going to
    // bed" and reads at any tray size.
    private static void DrawSleepingMoon(Graphics g, int size, Color moonColor, Color zColor)
    {
        // Moon shifted slightly down-left so the Z has room
        var state = g.Save();
        g.TranslateTransform(-2f, 2f);
        g.ScaleTransform(0.85f, 0.85f);
        DrawMoon(g, size, moonColor);
        g.Restore(state);

        // Big bold Z in upper-right corner
        using var pen = new Pen(zColor, 2.6f) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round };
        var x = size * 0.55f;
        var y = size * 0.10f;
        var s = size * 0.30f;
        g.DrawLine(pen, x,     y,     x + s, y);     // top
        g.DrawLine(pen, x + s, y,     x,     y + s); // diagonal
        g.DrawLine(pen, x,     y + s, x + s, y + s); // bottom
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
        // Universal power-button glyph — the shape carries "off" regardless
        // of taskbar color, no theme detection needed. A muted desaturated
        // orange reads cleanly on both dark and light Win11 taskbars (high
        // luminance contrast on dark, sufficient saturation on light).
        var cx = size / 2f;
        var cy = size / 2f + 1f; // nudged down to balance the upstroke
        var color = Color.FromArgb(0xFF, 0xE0, 0x90, 0x60);
        using var pen = new Pen(color, 2.6f) { StartCap = LineCap.Round, EndCap = LineCap.Round };

        // Ring with a ~60° gap at the top
        var r = 9f;
        g.DrawArc(pen, cx - r, cy - r, r * 2, r * 2, 300f, 300f);
        // Vertical bar through the gap, from above the ring down to center
        g.DrawLine(pen, cx, cy - r - 2f, cx, cy);
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

    // ── Win11 Mica backdrop on the tray context-menu popup ──────
    // DWMWA_SYSTEMBACKDROP_TYPE = 38 (Win11 22H2+); value 3 = Mica Alt.
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 ensures the popup uses dark-mode
    // borders to match the Mica tint.
    private const int DWMWA_SYSTEMBACKDROP_TYPE = 38;
    private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    private const int DWMSBT_MAINWINDOW = 2;   // Mica
    private const int DWMSBT_TABBEDWINDOW = 4; // Mica Alt

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

    private static void ApplyMicaBackdrop(IntPtr hwnd)
    {
        try
        {
            var value = DWMSBT_TABBEDWINDOW;
            DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, ref value, sizeof(int));
        }
        catch { }
    }

    private static void ApplyDarkMode(IntPtr hwnd)
    {
        try
        {
            var dark = 1;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref dark, sizeof(int));
        }
        catch { }
    }

    private sealed class RelayCommand : ICommand
    {
        private readonly Action<object?> _exec;
        public RelayCommand(Action<object?> exec) => _exec = exec;
        public bool CanExecute(object? parameter) => true;
        public void Execute(object? parameter) => _exec(parameter);
        public event EventHandler? CanExecuteChanged { add { } remove { } }
    }
}
