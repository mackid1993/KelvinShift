#pragma once

// Toggles the HKCU Run key so KelvinShift starts on login with --tray.
// HKCU only — no admin required. Port of LaunchAtLoginService.cs.

namespace LaunchAtLoginService
{
    bool IsEnabled();
    void Apply(bool enabled);
}
