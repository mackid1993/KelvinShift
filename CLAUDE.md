# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repo layout

Two platform-specific source trees, plus shared docs at the root:

```
macos/          Swift / SwiftUI / AppKit — original menu-bar app
win/            C# / WPF — Windows port with Inno Setup installer
README.md       User-facing docs covering both platforms
CLAUDE.md       This file
LICENSE
```

Both ports share the same blackbody table, schedule logic (4-anchor + optional 6-anchor with bedtime), Hermite smoothing constants, and SolarCalculator algorithm. Keep them in sync — port changes either direction.

## macOS

### Build & Run

```bash
cd macos
./build.sh                              # Build release and create .app bundle
cp -R KelvinShift.app /Applications/
open /Applications/KelvinShift.app
```

`build.sh` runs `swift build -c release`, creates the `.app` bundle, generates Info.plist, ad-hoc codesigns, and produces a release-ready `.zip`.

Uninstall:
```bash
rm -rf /Applications/KelvinShift.app
defaults delete com.kelvinshift.app
```

### macOS Architecture

Menu-bar-only app (no Dock icon via `LSUIElement=true`).

- **GammaController** — singleton, applies via CoreGraphics `CGSetDisplayTransferByTable`. 91-entry blackbody lookup table (1000–10000K, 100K intervals, Redshift / Ingo Thies 2013). D65 white point at 6500K.
- **ScheduleEngine** — 15-second timer. Determines phase (day/night/transitioning/rampToBedtime/bedtime), Hermite-smoothed lerp, applies via GammaController, publishes via `NotificationCenter`. Single `computeSchedule(at:)` helper drives `tick`, `restoreScheduledSettings`, and the bedtime demo cycle.
- **SolarCalculator** — NOAA algorithm, ±1 min accuracy ≤72° latitude.
- **Settings** — `ObservableObject` over `UserDefaults`. All properties auto-save and post `Settings.didChange`.
- **PreferencesWindow** — SwiftUI hosted in `NSHostingController` inside `NSWindow` (`isReleasedWhenClosed = false` to persist).

Data flow:
```
Settings.didChange → ScheduleEngine.tick() → GammaController.applyKelvinWithBrightness()
                                           → NotificationCenter.post(stateDidChange)
                                           → StatusBarController updates menu
```

## Windows

### Build & Run

```powershell
cd win
.\build.ps1                  # publish self-contained exe + build Inno installer
.\build.ps1 -SkipInstaller   # publish only
.\build.ps1 -Clean           # clean bin/obj first
```

Output: `win\KelvinShift-1.0.0-Setup.exe` (~70 MB, includes the .NET 8 runtime).

Requires: .NET 8 SDK, Inno Setup 6 (https://jrsoftware.org/isdl.php — installer-only step; publish works without it).

Uninstall via Settings → Apps → KelvinShift. The uninstaller removes the gamma-range registry value and (optionally) `%APPDATA%\KelvinShift\settings.json`.

### Windows Architecture

WPF on .NET 8 (`net8.0-windows10.0.19041.0`), single project. Mica backdrop via WPF-UI, tray via H.NotifyIcon.Wpf, MVVM via CommunityToolkit.Mvvm.

- **GammaService** (`Services/GammaService.cs`) — applies via **`D3DKMTSetGammaRamp`** (kernel-mode WDDM path), NOT the legacy `SetDeviceGammaRamp`. This is what f.lux/CareUEyes use; it bypasses the GDI gamma reset behaviors that cause shell-UI flashing in Light Bulb / Iris. Same 91-entry Redshift table as macOS. Per-monitor adapter enumeration via `EnumDisplayMonitors` + `D3DKMTOpenAdapterFromHdc`. Includes a rotating 0–4 offset on the last ramp value to defeat driver caching of identical writes.
- **GammaWatchdog** (`Services/GammaWatchdog.cs`) — message-only window. Subscribes to `WM_DISPLAYCHANGE`, `WM_WTSSESSION_CHANGE`, `WM_DEVICECHANGE`, `WM_POWERBROADCAST` (with `RegisterPowerSettingNotification` for console/monitor/session display state), `WM_DWMCOMPOSITIONCHANGED`, plus `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)`. On any of these, starts a 2-second burst that reapplies every 50ms. Foreground-window change is the critical anti-flash trigger.
- **ScheduleEngine** (`Services/ScheduleEngine.cs`) — direct port of the Swift version. Same enums, same Hermite math, same `ComputeSchedule(at:)` helper. Uses `DispatcherTimer`.
- **SolarCalculator** (`Services/SolarCalculator.cs`) — 1:1 port of `SolarCalculator.swift`.
- **SettingsService** (`Services/SettingsService.cs`) — manual `INotifyPropertyChanged` properties, debounced 250ms write to `%APPDATA%\KelvinShift\settings.json`, raises `SettingsChanged` on every change.
- **TrayIconService** (`Services/TrayIconService.cs`) — H.NotifyIcon. Dynamic 32×32 glyph (sun/transition/moon/bed/off, phase-colored) drawn at runtime via `DrawingVisual` → `RenderTargetBitmap`. Regenerated on every `StateChanged`.
- **PreferencesWindow** (`Views/PreferencesWindow.xaml`) — Fluent (Mica) window with the same sections as macOS: Color Temperature, Brightness, Bedtime, Schedule, Transition, General. Slider preview gestures hooked from code-behind (`PreviewMouseLeftButtonDown` → `Engine.StartPreview`).
- **LaunchAtLoginService** — toggles `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\KelvinShift = "<exe>" --tray`. No admin needed at runtime.

### Windows-specific install steps

The installer:
- Installs to `%ProgramFiles%\KelvinShift` (admin required).
- Writes `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ICM\GdiIcmGammaRange = 256` (DWORD) to lift the Vista+ gamma-range cap that otherwise clips warm temps below ~3500K. Our D3DKMT path mostly bypasses this anyway, but it's belt-and-suspenders for HDR fallback scenarios.
- Optionally writes the HKCU Run key for auto-start.

## Shared algorithm constants

- **6500K = D65 white point** (no color shift)
- **Transition smoothing**: Hermite `t² × (3 - 2t)`
- **Brightness range**: 0.1–1.0 (gamma-scaled, not backlight)
- **Schedule tick**: 15 seconds
- **Bedtime defaults**: 1900K, 40%, 23:00, disabled
- **Bedtime ramp duration**: implicit — fills the entire window from night-start to bedtime

## Cross-platform parity

When changing shared behavior (blackbody table, schedule logic, bedtime spec, transition math, solar algorithm), update both `macos/Sources/KelvinShift/*.swift` AND `win/src/KelvinShift/Services/*.cs`. The two ports must produce identical Kelvin/brightness values for the same settings + wall-clock time.
