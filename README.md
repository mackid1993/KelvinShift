# KelvinShift

**Accurate Kelvin-based color temperature scheduling — macOS and Windows.**

Set **exact Kelvin values** for daytime, nighttime, and (optional) bedtime display color temperatures, with automatic scheduling and smooth transitions. KelvinShift uses scientifically accurate blackbody radiation values from the Redshift project (CIE color matching, Ingo Thies 2013) so 2700 K really is 2700 K.

Made for personal use; sharing as-is with no support or warranty.

## Install

Pre-built binaries are on the [Releases page](../../releases/latest). Both builds are **signed ad-hoc only**, so each OS warns the first time you launch — see [First-launch warnings](#first-launch-warnings) below.

### macOS

Download `KelvinShift.app.zip` from the latest release, unzip, then:

```bash
cp -R KelvinShift.app /Applications/
open /Applications/KelvinShift.app
```

KelvinShift runs as a menu-bar item (no Dock icon). Click the icon for the current phase, schedule, and a Preferences pane.

### Windows

Download `KelvinShift-1.1.0-Setup.exe` from the latest release and run it. UAC will prompt — admin is needed for the gamma-range registry tweak (so warm temps below ~3500 K aren't clipped).

The installer:

- Installs to `%ProgramFiles%\KelvinShift`
- Writes `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ICM\GdiIcmGammaRange = 256` (DWORD) to lift the Vista+ GDI gamma-range cap
- Optionally enables auto-start with Windows (per-user, no admin at runtime)

KelvinShift runs as a tray icon. Right-click for current phase / Preferences / Exit.

### First-launch warnings

There's no paid Apple Developer ID or Authenticode certificate behind these builds, so each OS will warn the first time:

**macOS (Gatekeeper):** *"KelvinShift" cannot be opened because it is from an unidentified developer.* Right-click (or Control-click) the `.app` in Finder → **Open** → **Open** in the confirmation dialog. macOS remembers the choice after that. Alternatively, run `xattr -dr com.apple.quarantine /Applications/KelvinShift.app` once.

**Windows (SmartScreen):** *"Windows protected your PC"*. Click **More info** → **Run anyway**. Only the installer triggers this — the installed exe runs without further prompts.

## Build from source

### Prerequisites

| Platform | Required | Optional |
|---|---|---|
| **macOS** | macOS 12 (Monterey) or later · Xcode Command Line Tools (`xcode-select --install`) · Swift 5.7+ (ships with the CLT) | — |
| **Windows** | Windows 10 (1809) or later · [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0) · PowerShell 5.1 or 7+ | [Inno Setup 6](https://jrsoftware.org/isdl.php) — only required to build the `-Setup.exe` installer |

### macOS

```bash
cd macos
./build.sh
```

`build.sh` runs `swift build -c release`, assembles the `.app` bundle, generates `Info.plist`, ad-hoc codesigns, and writes both `KelvinShift.app` and `KelvinShift.app.zip` (the release artifact).

Install:

```bash
cp -R KelvinShift.app /Applications/
open /Applications/KelvinShift.app
```

### Windows

```powershell
cd win
.\build.ps1                   # publish self-contained exe + Inno installer
.\build.ps1 -SkipInstaller    # publish only (no Inno Setup needed)
.\build.ps1 -Clean            # remove bin/obj first
```

Output:

- Self-contained exe: `win\src\KelvinShift\bin\Release\net8.0-windows10.0.19041.0\win-x64\publish\KelvinShift.exe` (~77 MB, bundles the .NET 8 runtime)
- Installer: `win\KelvinShift-1.1.0-Setup.exe` (~72 MB)

To bump the version: edit `win\src\KelvinShift\KelvinShift.csproj` (`<Version>`) and `win\installer\setup.iss` (`MyAppVersion`).

## Features

- **Exact Kelvin** values for day / night (and optional bedtime), 1000–6500 K
- **Bedtime ramp** — optional third anchor that ramps from your night setting to even warmer/dimmer "winding down" values over a configurable duration before bedtime
- **Schedule**: solar (NOAA sunrise/sunset, ±1 min ≤72° latitude) or custom times
- **Configurable transition** with Hermite smoothing (1–180 min). Transitions end exactly at the scheduled time, and are auto-capped so a long transition can never overrun the next anchor.
- **Live preview** while dragging any slider
- **Demo cycle** — see a full day/night/(bedtime) loop in 10 seconds
- **Multi-monitor** support
- **No shell-UI flash** on Windows — gamma watchdog with read-back checks only writes when the LUT has actually drifted

## Defaults

| Setting | Default |
|---|---|
| Day Kelvin | 5000 K |
| Day brightness | 100% |
| Night Kelvin | 2700 K |
| Night brightness | 80% |
| Bedtime Kelvin | 1900 K (disabled by default) |
| Bedtime brightness | 40% |
| Bedtime time | 11:00 PM |
| Bedtime ramp | 60 minutes |
| Schedule | Custom times (Day 7:00 AM, Night 8:00 PM) |
| Day ↔ Night transition | 20 minutes |

## How it works

Both ports use the same 91-entry blackbody lookup table (1000 K – 10000 K, 100 K intervals) and the same scheduling logic. The platform-specific bit is how gamma actually gets applied.

**macOS** — `CGSetDisplayTransferByTable` (CoreGraphics).

**Windows** — `InternalSetDeviceGammaRamp` exported from `mscms.dll`. Two design choices keep shell UI flash-free:

1. **`InternalSetDeviceGammaRamp` instead of the public `SetDeviceGammaRamp`.** Resolved dynamically via `GetProcAddress` at startup; falls back to the public API if the symbol isn't present. Writing the LUT at this layer bypasses Windows' calibration-tracking reset on shell-flyout open and ignores the `GdiIcmGammaRange` registry cap.
2. **Read-back-gated watchdog.** A dedicated `Highest`-priority thread runs a native `GetMessage` pump that hooks `WM_DISPLAYCHANGE`, `WM_WTSSESSION_CHANGE`, `WM_DEVICECHANGE`, `WM_POWERBROADCAST` (with `RegisterPowerSettingNotification`), `WM_DWMCOMPOSITIONCHANGED`, plus `SetWinEventHook` for object-show / system-sound events. On each event the watchdog reads the current GPU LUT via `GetDeviceGammaRamp` and only writes if it has drifted — unnecessary writes are themselves visible as flicker, so suppressing them is what keeps the display quiet. A 1-second heartbeat covers events the hooks miss.

6500 K = D65 white point (no color shift). 2700 K ≈ warm incandescent. 1900 K ≈ candlelight.

## Configuration

Settings live at:

- **macOS:** `defaults read com.kelvinshift.app` (`UserDefaults` under `ks_*` keys)
- **Windows:** `%APPDATA%\KelvinShift\settings.json` — editable directly; the app rereads on every restart. Writes are debounced 250 ms while you drag sliders.

The two ports share identical schedule semantics: for the same settings and wall-clock time, both produce the same Kelvin/brightness values (modulo one intentional divergence — macOS uses Hermite smoothing on the lerp while Windows uses linear).

## Architecture

```
macos/    Swift / SwiftUI / AppKit — menu-bar app
win/      C# / WPF / Inno Setup — tray app with installer
```

See [CLAUDE.md](CLAUDE.md) for the full architectural notes (per-platform component breakdown, data flow, shared constants).

## Troubleshooting

**Colors don't change at all.** Another color-temperature utility is already controlling the gamma ramp — only one can at a time. Disable any other tool (including the OS's built-in color shift feature) and restart KelvinShift.

**Warm temps look clipped (Windows).** The installer should have written `GdiIcmGammaRange = 256` to HKLM. If you installed without admin, or this got removed, re-run the installer. (The `mscms` write path mostly bypasses this anyway, but it's belt-and-suspenders for HDR fallback paths.)

**Tray icon missing on startup (Windows).** Right-click the taskbar → **Taskbar settings** → **Other system tray icons** → enable "KelvinShift".

**Auto-start not working (Windows).** Check `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` for a `KelvinShift` entry. Toggle the "Start with Windows" option in Preferences to rewrite it.

**Auto-start not working (macOS).** Requires macOS 13+ (`SMAppService`). Toggle "Open at login" in Preferences. Older macOS versions show the option disabled.

## Uninstall

**macOS:**

```bash
rm -rf /Applications/KelvinShift.app
defaults delete com.kelvinshift.app
```

**Windows:** Settings → Apps → KelvinShift. The uninstaller removes the gamma-range registry value and offers to remove `%APPDATA%\KelvinShift`.

## License

Copyright 2026

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
