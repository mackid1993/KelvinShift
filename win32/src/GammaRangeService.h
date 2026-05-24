#pragma once

// Manages the optional HKLM\...\ICM\GdiIcmGammaRange = 256 registry value,
// which lifts the Windows heuristic that clips warm temperatures below
// ~3500K. Opt-in: writing HKLM needs elevation, so RequestChange re-launches
// this exe with the --set-gamma-range flag under the "runas" verb (UAC).
// Port of GammaRangeService.cs.

namespace GammaRangeService
{
    constexpr int   EnabledValue = 256;
    constexpr wchar_t CliFlag[] = L"--set-gamma-range";

    bool IsEnabled();

    // Re-launch self elevated to set/clear the value. Returns true if the
    // elevated child exited 0.
    bool RequestChange(bool enable);

    // Called in the elevated child to actually write HKLM. Returns a process
    // exit code (0 = success).
    int ApplyRegistryValue(int value);
}
