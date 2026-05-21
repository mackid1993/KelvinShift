# TODO — pick up next session

Items left after the refactor branch. The Windows project builds cleanly; macOS has the matching XAML→SwiftUI changes for the bedtime/visibility cleanup but hasn't been built this session.

## P0 — flicker

The only release-blocking item.

- **Shell-flyout flicker still happens** on some configurations when Action Center / Settings opens. Current architecture:
  - Gamma-only path. `InternalSetDeviceGammaRamp` is the primary write; falls back to public `SetDeviceGammaRamp` when the symbol isn't exported.
  - Watchdog runs on a dedicated `Highest`-priority thread with a native `GetMessage` pump, hooks `EVENT_OBJECT_CREATE..EVENT_OBJECT_SHOW` + system-event range + display/power/session window messages.
  - `Reapply` reads back the current GPU LUT via `GetDeviceGammaRamp` and only writes if it has actually drifted (so the watchdog itself isn't generating flicker from unnecessary writes — that was a problem in earlier iterations).
- What to try next:
  - **Verify the read-back is doing what it should**: add a tick counter to confirm writes happen only when the diagnostic showed `GAMMA RESET DETECTED` previously, not on every event.
  - **Test in isolation** on a system that previously flickered. The read-back gate is the change that should kill the residual flicker — if it doesn't, the next hypothesis is that the `InternalSetDeviceGammaRamp` write itself causes a brief visible LUT update on this specific driver, in which case the fix is to throttle the recovery (one write per N ms max, instead of one per event).

## P1 — screenshot path

- **Universal screenshot trigger** — the gamma path is already screenshot-clean (gamma applied post-framebuffer), so any tool (Snagit, ShareX, Snipping Tool, PrintScreen-to-clipboard) captures clean output as-is. Verify by running through the user's actual screenshot toolchain.

## P2 — UI polish that was deferred

- **Transitions card slider alignment** — the Day↔Night and Night→Bedtime slider rows in the Transitions card use `* / 70 / Auto` columns; the K/B sliders in the Color Temp + Brightness cards use the new `SharedSizeGroup` columns. Aligning them with each other (so the slider tracks start at the same X position across all cards) requires adding a leading label column to the Transitions rows that participates in the same `SharedSizeGroup="Lbl"` scope.
- **Tray menu padding visual check** — the `MenuItem` style sets `Padding=16,10,16,10 + FontSize=14`. Confirm hit targets feel right at the user's DPI.
- **Settings → Display verification** — confirm the read-back gate eliminates the previous "screen reverts to neutral" symptom when opening Settings → Display. If still observed, the next change is to schedule a paced retry burst (10ms × ~5 reapplies, gated on read-back deviation each time) bounded to the next ~50ms after a display-related window message.

## P3 — repo hygiene

- **Run the macOS build** (`cd macos && ./build.sh`) to verify the SwiftUI bedtime-hide changes compile.
- **CLAUDE.md** is mid-refresh — the GammaService and GammaWatchdog sections are updated; verify the rest of the file still matches the actual implementation after this session's churn.
- **`%LOCALAPPDATA%\KelvinShift\debug.log`** rotates at 256 KB. Consider lowering for shipping or only writing on errors.
