# KelvinShift for Windows

Accurate Kelvin-based color temperature scheduling for Windows, ported from the [macOS version](../macos/).

## Install

Grab the latest `KelvinShift-X.Y.Z-Setup.exe` and run it. UAC will prompt — admin is needed for the gamma-range registry tweak (so warm temps below ~3500K aren't clipped).

The installer:
- Installs to `C:\Program Files\KelvinShift`
- Lifts the Vista+ GDI gamma-range cap (`HKLM\...\ICM\GdiIcmGammaRange = 256`)
- Optionally enables auto-start with Windows

Uninstall via **Settings → Apps → KelvinShift** (or Add/Remove Programs). Settings are cleaned up on uninstall after confirmation.

## Build from source

Prerequisites:

- **.NET 8 SDK** — https://dotnet.microsoft.com/download/dotnet/8.0
- **Inno Setup 6** (optional, only for the installer step) — https://jrsoftware.org/isdl.php

```powershell
cd win
.\build.ps1                  # publish + installer
.\build.ps1 -SkipInstaller   # publish only (no Inno needed)
.\build.ps1 -Clean           # remove bin/obj first
```

Output:
- Self-contained exe: `src\KelvinShift\bin\Release\net8.0-windows10.0.19041.0\win-x64\publish\KelvinShift.exe` (~77 MB, bundles .NET runtime)
- Installer: `win\KelvinShift-1.0.0-Setup.exe` (~72 MB)

To bump the version: edit `src\KelvinShift\KelvinShift.csproj` (`<Version>`) and `installer\setup.iss` (`MyAppVersion`).

## How it works

KelvinShift adjusts color temperature by writing per-channel gamma ramps to each monitor. Two reasons this version doesn't flash on shell UI (unlike Light Bulb, Iris, and most blue-light filters):

1. **`D3DKMTSetGammaRamp`, not `SetDeviceGammaRamp`.** The kernel-mode WDDM API bypasses the GDI gamma-reset paths that cause flashes when Action Center / Settings / UAC composes. This is the same path f.lux and CareUEyes use.

2. **Aggressive event watchdog.** A message-only window listens for `WM_DISPLAYCHANGE`, `WM_WTSSESSION_CHANGE`, `WM_DEVICECHANGE`, `WM_POWERBROADCAST` (display state changes), and — most importantly — `EVENT_SYSTEM_FOREGROUND` (via `SetWinEventHook`). Any of these triggers a 2-second burst that reapplies gamma every 50ms, beating Windows' transient resets.

Color temperatures use the 91-entry Redshift blackbody table (CIE color matching functions, Ingo Thies 2013). D65 white point at 6500K. Identical to the macOS build.

## Configuration

Settings live at `%APPDATA%\KelvinShift\settings.json`. Editing the file directly works; the app rereads on every restart. Writes are debounced 250ms while you drag sliders.

## Features

Same as macOS:

- **Day / Night** color temperature (1800–6500K) and brightness (10–100%)
- **Bedtime ramp** (optional): a slow ramp from night to a third anchor — even warmer/dimmer values — across the full interval from night-start to your bedtime
- **Schedule**: solar (sunrise/sunset, NOAA algorithm) or custom times
- **Transition**: configurable duration with Hermite smoothing
- **Preview cycle**: 10-second day → night → (bedtime) → day demo
- **Live preview**: drag any slider to see the change immediately
- **Tray glyph** that changes with phase (☀ ☾ 🛏 etc.) + current Kelvin in tooltip

## Troubleshooting

**Colors don't change** — Make sure you're not running another color-temp tool (Windows Night Light, f.lux, Iris). Only one can own the gamma ramp at a time.

**Warm temps look clipped** — The installer should have written `GdiIcmGammaRange = 256` to HKLM. If you installed without admin or this got removed, re-run the installer.

**Tray icon is missing on startup** — Right-click the taskbar → Taskbar settings → Other system tray icons → enable "KelvinShift".

**Auto-start not working** — Check `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` for a "KelvinShift" entry. Toggle the "Start with Windows" option in Preferences to rewrite it.
