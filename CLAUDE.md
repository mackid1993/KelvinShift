# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
./build.sh                              # Build release and create .app bundle
cp -R KelvinShift.app /Applications/    # Install
open /Applications/KelvinShift.app      # Run
```

The build script runs `swift build -c release`, creates the app bundle structure, copies the icon, generates Info.plist, and ad-hoc codesigns.

To uninstall and clear preferences:
```bash
rm -rf /Applications/KelvinShift.app
defaults delete com.kelvinshift.app
```

## Architecture

KelvinShift is a menu-bar-only macOS app (no Dock icon via `LSUIElement=true`) that adjusts display color temperature via direct gamma table manipulation.

### Core Components

**GammaController** - Singleton that manipulates display gamma tables via CoreGraphics `CGSetDisplayTransferByTable`. Contains a 91-entry blackbody lookup table (1000K-10000K at 100K intervals) from the Redshift project. Interpolates between table entries for any Kelvin value.

**ScheduleEngine** - Runs a 15-second timer that:
1. Determines current phase (day/night/transitioning) based on schedule mode
2. Calculates target Kelvin and brightness using Hermite-smoothed interpolation during transitions
3. Applies via GammaController
4. Publishes state via NotificationCenter for UI updates

Also handles preview mode (when user drags sliders) and demo mode (10-second day/night cycle preview).

**SolarCalculator** - NOAA algorithm for sunrise/sunset calculation. Returns times accurate to ±1 minute.

**Settings** - ObservableObject backed by UserDefaults. All properties auto-save on change and post `Settings.didChange` notification.

### Data Flow

```
Settings.didChange → ScheduleEngine.tick() → GammaController.applyKelvinWithBrightness()
                                           → NotificationCenter.post(stateDidChange)
                                           → StatusBarController updates menu
```

### Key Constants

- 6500K = D65 white point (no color shift)
- Timer interval: 15 seconds
- Transition uses Hermite smoothing: `t² × (3 - 2t)`
- Brightness range: 0.1-1.0 (gamma-based, not backlight)

## SwiftUI in AppKit

PreferencesWindow uses `NSHostingController` to embed SwiftUI in an `NSWindow`. The window is menu-bar-only and uses `isReleasedWhenClosed = false` to persist across open/close cycles.
