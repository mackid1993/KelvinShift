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

The Windows app is a **pure Win32 / C++ build** — a single ~0.5 MB native executable with no .NET runtime to ship. Download `KelvinShift-1.1.1-Setup.exe` from the latest release and run it. UAC will prompt — admin is needed to install into `%ProgramFiles%`.

The installer:

- Installs the single exe to `%ProgramFiles%\KelvinShift`
- Optionally enables auto-start with Windows (per-user, no admin at runtime)

KelvinShift runs as a tray icon — left-click for Preferences, right-click for the status menu (current phase / schedule / Exit).

The gamma-range registry tweak that allows color temperatures below ~3500 K is an **opt-in toggle** in Preferences → General; it isn't written at install time, triggers its own UAC prompt when enabled, and is removed when you turn it off or uninstall.

### First-launch warnings

There's no paid Apple Developer ID or Authenticode certificate behind these builds, so each OS will warn the first time:

**macOS (Gatekeeper):** *"KelvinShift" cannot be opened because it is from an unidentified developer.* Right-click (or Control-click) the `.app` in Finder → **Open** → **Open** in the confirmation dialog. macOS remembers the choice after that. Alternatively, run `xattr -dr com.apple.quarantine /Applications/KelvinShift.app` once.

**Windows (SmartScreen):** *"Windows protected your PC"*. Click **More info** → **Run anyway**. Only the installer triggers this — the installed exe runs without further prompts.

## Build from source

### Prerequisites

| Platform | Required | Optional |
|---|---|---|
| **macOS** | macOS 12 (Monterey) or later · Xcode Command Line Tools (`xcode-select --install`) · Swift 5.7+ (ships with the CLT) | — |
| **Windows** | Windows 10 (1809) or later · Visual Studio 2022 Build Tools with the **Desktop development with C++** workload (MSVC + Windows SDK) · PowerShell 5.1 or 7+ | [Inno Setup 6](https://jrsoftware.org/isdl.php) — only required to build the `-Setup.exe` installer |

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
.\build.ps1                   # compile the native exe + build the Inno installer
.\build.ps1 -SkipInstaller    # compile only (no Inno Setup needed)
.\build.ps1 -Clean            # wipe the build dir first
.\build.ps1 -Debug            # unoptimized build with symbols
```

`build.ps1` locates the MSVC toolchain via `vswhere` automatically — no Visual Studio IDE or `.vcxproj` involved.

Output:

- Native exe: `win\build\KelvinShift.exe` (~0.5 MB, statically linked — nothing else to ship)
- Installer: `win\KelvinShift-1.1.1-Setup.exe` (~2 MB)

To bump the version: edit `win\src\app.rc` / `win\src\app.manifest` and `win\installer\setup.iss` (`MyAppVersion`).

## Features

- **Exact Kelvin** values for day / night (and optional bedtime), 1000–6500 K
- **Bedtime ramp** — optional third anchor that ramps from your night setting to even warmer/dimmer "winding down" values over a configurable duration before bedtime
- **Schedule**: solar (NOAA sunrise/sunset, ±1 min ≤72° latitude) or custom times
- **Configurable transition** with Hermite smoothing (1–180 min). Transitions end exactly at the scheduled time, and are auto-capped so a long transition can never overrun the next anchor.
- **Live preview** while dragging any slider
- **Demo cycle** — preview a full day → night → bedtime loop in seconds
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
2. **Read-back-gated watchdog.** A message-only window listens for `WM_DISPLAYCHANGE`, `WM_WTSSESSION_CHANGE`, `WM_DEVICECHANGE`, `WM_POWERBROADCAST` (with `RegisterPowerSettingNotification`) and `WM_DWMCOMPOSITIONCHANGED`; a dedicated high-priority thread with its own `GetMessage` pump adds a `SetWinEventHook` for foreground / menu / dialog activations. On each event the watchdog reads the current GPU LUT via `GetDeviceGammaRamp` and only re-writes if it has actually drifted — unnecessary writes are themselves visible as flicker, so suppressing them is what keeps the display quiet. A 30-second heartbeat backstops anything the events miss; it's deliberately infrequent so the process sits at 0% CPU when idle.

6500 K = D65 white point (no color shift). 2700 K ≈ warm incandescent. 1900 K ≈ candlelight.

## Configuration

Settings live at:

- **macOS:** `defaults read com.kelvinshift.app` (`UserDefaults` under `ks_*` keys)
- **Windows:** `%APPDATA%\KelvinShift\settings.json` — editable directly; the app rereads on every restart. Writes are debounced 250 ms while you drag sliders.

The two ports share identical schedule semantics: for the same settings and wall-clock time, both produce the same Kelvin/brightness values (modulo one intentional divergence — macOS uses Hermite smoothing on the lerp while Windows uses linear).

## Architecture

```
macos/    Swift / SwiftUI / AppKit — menu-bar app
win/      C++ / Win32 / Inno Setup — tray app with installer
```

See [CLAUDE.md](CLAUDE.md) for the full architectural notes (per-platform component breakdown, data flow, shared constants).

## Troubleshooting

**Colors don't change at all.** Another color-temperature utility is already controlling the gamma ramp — only one can at a time. Disable any other tool (including the OS's built-in color shift feature) and restart KelvinShift.

**Warm temps look clipped (Windows).** Enable the gamma-range toggle in Preferences → General — it writes `GdiIcmGammaRange = 256` to HKLM (UAC prompt) to lift the Vista+ GDI cap. The `mscms` write path mostly bypasses this anyway, but it's belt-and-suspenders for HDR fallback paths.

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

Licensed under the [PolyForm Strict License 1.0.0](https://polyformproject.org/licenses/strict/1.0.0).
See [LICENSE](LICENSE) for the full text and [NOTICE](NOTICE) for the copyright notice.

Copyright (c) 2026 David Brustein
