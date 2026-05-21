using System;
using System.Diagnostics;
using Microsoft.Win32;

namespace KelvinShift.Services;

// Manages the optional HKLM\...\ICM\GdiIcmGammaRange = 256 registry value.
// This disables the Windows "unreadable screen" heuristic that otherwise
// clips warm temperatures below ~3500K. Opt-in: users who only need mild
// warming don't need it, so we don't force the system modification on
// install. Elevation is required to write HKLM, so we re-launch ourselves
// with --set-gamma-range <value> and the "runas" verb to trigger UAC.

public static class GammaRangeService
{
    private const string KeyPath = @"SOFTWARE\Microsoft\Windows NT\CurrentVersion\ICM";
    private const string ValueName = "GdiIcmGammaRange";
    public const int EnabledValue = 256;

    // CLI flag used when the app re-launches itself elevated to perform the
    // registry write/delete. Handled in App.OnStartup before normal init.
    public const string CliFlag = "--set-gamma-range";

    public static bool IsEnabled()
    {
        try
        {
            using var k = Registry.LocalMachine.OpenSubKey(KeyPath);
            return k?.GetValue(ValueName) is int v && v == EnabledValue;
        }
        catch
        {
            return false;
        }
    }

    /// <summary>Re-launch self elevated to set/clear the value.</summary>
    /// <returns>true if the elevated process completed successfully.</returns>
    public static bool RequestChange(bool enable)
    {
        try
        {
            var exe = Environment.ProcessPath;
            if (string.IsNullOrEmpty(exe)) return false;
            var psi = new ProcessStartInfo
            {
                FileName = exe,
                Arguments = $"{CliFlag} {(enable ? EnabledValue : 0)}",
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden,
                CreateNoWindow = true,
            };
            using var p = Process.Start(psi);
            if (p is null) return false;
            p.WaitForExit(10_000);
            return p.ExitCode == 0;
        }
        catch
        {
            return false; // user cancelled UAC, or other failure
        }
    }

    /// <summary>Called by the elevated child process to actually write the value.</summary>
    public static int ApplyFromCli(string[] args)
    {
        for (var i = 0; i < args.Length; i++)
        {
            if (!args[i].Equals(CliFlag, StringComparison.OrdinalIgnoreCase)) continue;
            if (i + 1 >= args.Length) return 2;
            if (!int.TryParse(args[i + 1], out var v)) return 2;
            try
            {
                using var k = Registry.LocalMachine.OpenSubKey(KeyPath, writable: true)
                              ?? Registry.LocalMachine.CreateSubKey(KeyPath);
                if (k is null) return 3;
                if (v == EnabledValue)
                    k.SetValue(ValueName, EnabledValue, RegistryValueKind.DWord);
                else
                    k.DeleteValue(ValueName, throwOnMissingValue: false);
                return 0;
            }
            catch
            {
                return 4;
            }
        }
        return 1;
    }
}
