# KelvinShift

**Precise Kelvin-based color temperature scheduling for macOS, powered by Night Shift.**

KelvinShift fills the gap that no other macOS app does: it lets you specify **exact Kelvin values** for daytime and nighttime display color temperatures, with automatic scheduling and smooth transitions — all by driving the built-in Night Shift engine under the hood.

## What It Does

| Feature | Night Shift alone | KelvinShift |
|---|---|---|
| Scheduling (sunrise/sunset or custom) | ✓ warmth slider | ✓ **exact Kelvin values** |
| Separate day vs night targets | ✗ (one warmth level) | ✓ e.g. 5000 K day / 2700 K night |
| Show current color temperature | ✗ | ✓ live in the menu bar |
| Smooth transition ramp | Abrupt toggle | Configurable 1–60 min Hermite curve |
| Custom solar position calculation | Basic sunset/sunrise | Full NOAA algorithm |

## Quick Start

```bash
# 1. Clone or download this folder
cd KelvinShift

# 2. Build
./build.sh

# 3. Install & run
cp -R KelvinShift.app /Applications/
open /Applications/KelvinShift.app
```

A sun icon (☀ 5000K) appears in your menu bar. Click it for status or to open Preferences.

### Requirements

- macOS 12 (Monterey) or later
- Apple Silicon or Intel Mac that supports Night Shift
- Xcode Command Line Tools (`xcode-select --install`)

## Architecture

```
main.swift                 App entry point (menu-bar-only, no Dock icon)
AppDelegate.swift          Lifecycle — wires up all components
NightShiftBridge.swift     Dynamically loads CoreBrightness private framework
                           Maps Kelvin ↔ Night Shift strength (0.0–1.0)
SolarCalculator.swift      NOAA sunrise/sunset algorithm (±1 min accuracy)
ScheduleEngine.swift       15-second timer calculates current Kelvin target
                           Hermite-smoothed transitions between day/night
StatusBarController.swift  Menu bar icon + dropdown with live status
PreferencesWindow.swift    SwiftUI settings panel (hosted in NSWindow)
Settings.swift             ObservableObject backed by UserDefaults
```

### How It Hooks Into Night Shift

KelvinShift uses the private `CBBlueLightClient` class from Apple's CoreBrightness framework. It:

1. **Enables** Night Shift and sets its schedule to **manual** (mode 0) so KelvinShift has exclusive control.
2. Every 15 seconds, calculates the target Kelvin based on the current time and schedule.
3. Converts the Kelvin value to a Night Shift **strength** (0.0 = no shift at 6500 K, 1.0 = max warmth at ~1900 K).
4. Applies the strength via `setStrength:commit:`.

When you quit KelvinShift, it resets Night Shift strength to 0 (native white point). You can then re-enable Night Shift's built-in schedule from System Settings if you want.

## Defaults

| Setting | Default | Notes |
|---|---|---|
| Day Kelvin | 5000 K | Slightly warm daylight |
| Night Kelvin | 2700 K | Warm incandescent |
| Schedule | Solar | Uses NOAA algorithm |
| Location | 41.10, −74.01 | Nanuet, NY |
| Transition | 20 minutes | Smooth Hermite ramp |
| Calibration min K | 1900 K | Night Shift strength 1.0 |

## Calibration

Night Shift's maximum warmth (strength = 1.0) varies slightly by display hardware. Most Apple Silicon Macs reach approximately **1900 K** at full warmth, but some external displays may clip earlier.

If your night temperature feels too warm or not warm enough:

1. Open **Preferences → Advanced Calibration**
2. Adjust "NS max-warmth" (the Kelvin value that strength 1.0 corresponds to)
3. If you have a colorimeter, set Night Shift to max warmth via System Settings and measure the actual correlated color temperature

A higher calibration value (e.g. 2700) compresses the usable range, while a lower value (e.g. 1500) expands it.

## Troubleshooting

**"Could not load CoreBrightness framework"**
- Night Shift may not be supported on your hardware
- On macOS Sequoia+, System Integrity Protection may block private framework access. Try disabling App Sandbox if building with Xcode.

**Night Shift keeps reverting to its own schedule**
- KelvinShift sets Night Shift to manual mode (mode 0) on startup. If another app or a System Settings change overrides this, KelvinShift re-applies on the next 15-second tick.

**Menu bar shows wrong Kelvin**
- Recalibrate in Preferences → Advanced Calibration

**Gatekeeper blocks the app**
- Run: `xattr -cr /Applications/KelvinShift.app` then reopen

## Uninstall

```bash
rm -rf /Applications/KelvinShift.app
defaults delete com.kelvinshift.app
```

## License

MIT
