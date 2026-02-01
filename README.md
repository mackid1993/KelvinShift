# KelvinShift

**Accurate Kelvin-based color temperature scheduling for macOS using direct gamma control.**

KelvinShift lets you specify **exact Kelvin values** for daytime and nighttime display color temperatures, with automatic scheduling and smooth transitions. Unlike other tools that use Night Shift's arbitrary "warmth" slider, KelvinShift uses scientifically accurate blackbody radiation values to set precise color temperatures via CoreGraphics gamma tables.

## Quick Start

```bash
# 1. Clone or download
cd KelvinShift

# 2. Build
./build.sh

# 3. Install & run
cp -R KelvinShift.app /Applications/
open /Applications/KelvinShift.app
```

A moon icon appears in your menu bar showing the current temperature (e.g. "5000K"). Click it for status or to open Preferences.

### Requirements

- macOS 12 (Monterey) or later
- Xcode Command Line Tools (`xcode-select --install`)

## How It Works

KelvinShift uses **direct gamma table manipulation** via CoreGraphics (`CGSetDisplayTransferByTable`) instead of Night Shift. Color temperatures are converted to RGB multipliers using a lookup table based on blackbody radiation calculations from the [Redshift](https://github.com/jonls/redshift) project (CIE color matching functions, Ingo Thies 2013).

Key points:
- **6500K** = D65 white point (RGB 1.0, 1.0, 1.0) — no color shift
- **2700K** = Warm incandescent (RGB 1.0, 0.68, 0.35)
- **1900K** = Candle light (RGB 1.0, 0.52, 0.0)
- Values are interpolated from a 100K-interval lookup table for any temperature

When you quit KelvinShift, gamma is reset to system defaults.

## Architecture

```
main.swift                 App entry point (menu-bar-only, no Dock icon)
AppDelegate.swift          Lifecycle — wires up all components
GammaController.swift      CoreGraphics gamma table manipulation
                           Blackbody lookup table (1000K–10000K)
SolarCalculator.swift      NOAA sunrise/sunset algorithm (±1 min accuracy)
ScheduleEngine.swift       15-second timer calculates current Kelvin target
                           Hermite-smoothed transitions between day/night
StatusBarController.swift  Menu bar icon + dropdown with live status
PreferencesWindow.swift    SwiftUI settings panel (hosted in NSWindow)
Settings.swift             ObservableObject backed by UserDefaults
LocationManager.swift      CoreLocation wrapper for detecting user location
```

## Defaults

| Setting | Default | Notes |
|---|---|---|
| Day Kelvin | 5000 K | Slightly warm daylight |
| Night Kelvin | 2700 K | Warm incandescent |
| Schedule | Custom | Set your own day/night times |
| Transition | 20 minutes | Smooth Hermite ramp |

## Preferences

- **Color Temperature**: Set day and night Kelvin values (with live preview when adjusting sliders)
- **Schedule**: Solar (automatic sunrise/sunset) or custom times
- **Location**: Enter coordinates manually or click "Use Current" to detect via Location Services (displays city/state name)
- **Transition**: How long the smooth ramp takes between day and night (any duration you want)
- **Launch at Login**: Auto-start on login (macOS 13+)

## Troubleshooting

**Gatekeeper blocks the app**
```bash
xattr -cr /Applications/KelvinShift.app
```

**Colors don't change**
- Check System Settings → Privacy & Security → Accessibility (app may need permission)
- Try quitting and relaunching

**Login item doesn't persist**
- KelvinShift auto-repairs the login item if deleted; just relaunch the app

## Uninstall

```bash
rm -rf /Applications/KelvinShift.app
defaults delete com.kelvinshift.app
```

## License

MIT
