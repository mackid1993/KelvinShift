# KelvinShift

**Accurate Kelvin-based color temperature scheduling — macOS and Windows.**

Set **exact Kelvin values** for daytime, nighttime, and (optional) bedtime display color temperatures, with automatic scheduling and smooth transitions. Unlike Night Shift or Windows Night Light, KelvinShift uses scientifically accurate blackbody radiation values (Redshift / CIE color matching, Ingo Thies 2013) so 2700K really is 2700K.

Made for personal use; sharing as-is with no support or warranty.

## Install

### macOS

```bash
cd macos
./build.sh
cp -R KelvinShift.app /Applications/
open /Applications/KelvinShift.app
```

Requires macOS 12 (Monterey) or later and Xcode Command Line Tools.

### Windows

Run [`KelvinShift-X.Y.Z-Setup.exe`](win/) — UAC will prompt (admin needed for the gamma-range registry tweak).

Build from source:

```powershell
cd win
.\build.ps1
```

Requires .NET 8 SDK; Inno Setup 6 is needed only for the installer step.

## Features

- **Exact Kelvin** values for day / night (and optional bedtime)
- **Bedtime ramp** — slow, optional ramp from night settings to even warmer/dimmer "winding down" values, filling the full interval from night-start to your bedtime
- **Schedule**: solar (NOAA sunrise/sunset, ±1 min) or custom times
- **Configurable transition** with Hermite smoothing
- **Live preview** while dragging any slider
- **Demo cycle** — see a full day/night/(bedtime) loop in 10 seconds
- **Multi-monitor** support
- **No shell-UI flash** on Windows — gamma watchdog with read-back checks reapplies only when Windows resets the LUT

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
| Schedule | Custom times |
| Transition | 20 minutes |

## How it works

Both ports use the same 91-entry blackbody lookup table (1000K–10000K, 100K intervals) and the same scheduling logic. The platform-specific bit is how gamma gets applied:

- **macOS** — `CGSetDisplayTransferByTable` (CoreGraphics).
- **Windows** — `D3DKMTSetGammaRamp` (kernel-mode WDDM). This is the path f.lux uses, NOT the legacy `SetDeviceGammaRamp` that Light Bulb uses and that causes visible flashes when Action Center / Settings / UAC compose. Plus an aggressive watchdog that reapplies on every shell event.

6500K = D65 white point (no color shift). 2700K ≈ warm incandescent. 1900K ≈ candlelight.

## Architecture

See [CLAUDE.md](CLAUDE.md) for the full architectural notes (per-platform component breakdown, data flow, shared constants).

```
macos/    Swift / SwiftUI / AppKit — menu-bar app
win/      C# / WPF / Inno Setup — tray app with installer
```

## Uninstall

**macOS:**
```bash
rm -rf /Applications/KelvinShift.app
defaults delete com.kelvinshift.app
```

**Windows:** Settings → Apps → KelvinShift. The uninstaller cleans up the gamma-range registry value and offers to remove `%APPDATA%\KelvinShift`.

## License

Copyright 2026

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
