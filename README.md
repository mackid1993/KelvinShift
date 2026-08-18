# KelvinShift

**Accurate Kelvin-based color temperature scheduling — macOS and Windows.**

Set **exact Kelvin values** for daytime, nighttime, and (optional) bedtime display color temperatures, with automatic scheduling and smooth transitions. KelvinShift computes its 91-entry blackbody RGB table from primary public-domain sources — Planck's law, the CIE 1931 2° standard observer, and the sRGB linear transform (IEC 61966-2-1) — so 2700 K really is 2700 K. The full derivation is in [`tools/generate_blackbody_table.py`](tools/generate_blackbody_table.py).

Made for personal use; sharing as-is with no support or warranty.

## Install

Pre-built binaries are on the [Releases page](../../releases/latest). The macOS build is signed with a Developer ID and notarized by Apple, so it opens without a Gatekeeper prompt. The Windows build is unsigned and trips SmartScreen the first time — see [First-launch warnings](#first-launch-warnings) below.

### macOS

Download `KelvinShift.zip` from the latest release, unzip, then:

```bash
cp -R KelvinShift.app /Applications/
open /Applications/KelvinShift.app
```

KelvinShift runs as a menu-bar item (no Dock icon). Click the icon for the current phase, schedule, and a Preferences pane.

### Windows

The Windows app is a **pure Win32 / C++ build** — a single ~0.5 MB native executable with no .NET runtime to ship. Download `KelvinShift-2.0.2-Setup.exe` from the [v2.0.2 release](../../releases/tag/v2.0.2) and run it — the Windows build is versioned separately, and 2.0.2 is current for it (the macOS-only 3.0.x releases carry no Windows asset). UAC will prompt — admin is needed to install into `%ProgramFiles%`.

The installer:

- Installs the single exe to `%ProgramFiles%\KelvinShift`
- Optionally enables auto-start with Windows (per-user, no admin at runtime)

KelvinShift runs as a tray icon — left-click for Preferences, right-click for the status menu (current phase / schedule / Exit).

The gamma-range registry tweak that allows color temperatures below ~3500 K is an **opt-in toggle** in Preferences → General; it isn't written at install time, triggers its own UAC prompt when enabled, and is removed when you turn it off or uninstall.

### First-launch warnings

**macOS:** none. The build is Developer ID signed, notarized, and stapled, so it launches straight from the download with no Gatekeeper prompt and no `xattr` incantation.

**Windows (SmartScreen):** *"Windows protected your PC"*. There's no Authenticode certificate behind the Windows build, so click **More info** → **Run anyway**. Only the installer triggers this — the installed exe runs without further prompts.

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

`build.sh` runs `swift build -c release`, assembles the `.app` bundle, generates `Info.plist`, Developer ID signs it with the hardened runtime, submits it to Apple for notarization, staples the ticket, and writes both `KelvinShift.app` and `KelvinShift.zip` (the release artifact).

Signing and notarization need a Developer ID Application certificate in the login keychain plus a `notarytool` credential profile (`xcrun notarytool store-credentials`). Override either with `KS_SIGN_ID` / `KS_NOTARY_PROFILE`. Without a matching certificate the script falls back to ad-hoc signing and skips notarization; `./build.sh --no-notarize` signs but skips the Apple round trip for faster local iteration.

Install:

```bash
cp -R KelvinShift.app /Applications/
open /Applications/KelvinShift.app
```

### Windows

```powershell
cd win32
.\build.ps1                   # compile the native exe + build the Inno installer
.\build.ps1 -SkipInstaller    # compile only (no Inno Setup needed)
.\build.ps1 -Clean            # wipe the build dir first
.\build.ps1 -Debug            # unoptimized build with symbols
```

`build.ps1` locates the MSVC toolchain via `vswhere` automatically — no Visual Studio IDE or `.vcxproj` involved.

Output:

- Native exe: `win32\build\KelvinShift.exe` (~0.5 MB, statically linked — nothing else to ship)
- Installer: `win32\KelvinShift-<version>-Setup.exe` (~2 MB)

To bump the version: edit `win32\src\app.rc` / `win32\src\app.manifest` and `win32\installer\setup.iss` (`MyAppVersion`).

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

The two ports share identical schedule semantics: for the same settings and wall-clock time, both produce the same Kelvin/brightness values, including the same Hermite (`t² × (3 − 2t)`) smoothstep on every transition.

## Architecture

```
macos/    Swift / SwiftUI / AppKit — menu-bar app
win32/      C++ / Win32 / Inno Setup — tray app with installer
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
