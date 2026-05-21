using System;
using System.Diagnostics;
using Microsoft.Win32;

namespace KelvinShift.Services;

// Toggles the HKCU Run key so KelvinShift starts on login with --tray
// (skip-preferences-window flag). HKCU only — no admin required.
public static class LaunchAtLoginService
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "KelvinShift";

    public static bool IsEnabled()
    {
        using var k = Registry.CurrentUser.OpenSubKey(RunKey);
        return k?.GetValue(ValueName) is string;
    }

    public static void Apply(bool enabled)
    {
        using var k = Registry.CurrentUser.OpenSubKey(RunKey, writable: true)
                      ?? Registry.CurrentUser.CreateSubKey(RunKey);
        if (k is null) return;
        if (enabled)
        {
            var exe = Process.GetCurrentProcess().MainModule?.FileName;
            if (string.IsNullOrEmpty(exe)) return;
            k.SetValue(ValueName, $"\"{exe}\" --tray", RegistryValueKind.String);
        }
        else
        {
            k.DeleteValue(ValueName, throwOnMissingValue: false);
        }
    }
}
