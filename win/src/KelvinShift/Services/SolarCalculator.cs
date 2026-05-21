using System;

namespace KelvinShift.Services;

// C# port of macos/Sources/KelvinShift/SolarCalculator.swift.
// NOAA Solar Calculator algorithm. ±1 minute accuracy between ±72° latitude.
public static class SolarCalculator
{
    public readonly record struct SolarTimes(DateTime Sunrise, DateTime Sunset, DateTime SolarNoon);

    public static SolarTimes? Calculate(DateTime date, double latitude, double longitude, TimeZoneInfo? tz = null)
    {
        tz ??= TimeZoneInfo.Local;
        var local = TimeZoneInfo.ConvertTime(date, tz);
        var startOfDay = new DateTime(local.Year, local.Month, local.Day, 0, 0, 0, DateTimeKind.Unspecified);

        var jd = JulianDay(local.Year, local.Month, local.Day);
        var T = (jd - 2451545.0) / 36525.0;

        var L0 = Mod(280.46646 + T * (36000.76983 + 0.0003032 * T), 360);
        var M  = 357.52911 + T * (35999.05029 - 0.0001537 * T);
        var e  = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);

        var C = Math.Sin(Rad(M)) * (1.914602 - T * (0.004817 + 0.000014 * T))
              + Math.Sin(Rad(2 * M)) * (0.019993 - 0.000101 * T)
              + Math.Sin(Rad(3 * M)) * 0.000289;

        var sunTL = L0 + C;
        var omega = 125.04 - 1934.136 * T;
        var sunAL = sunTL - 0.00569 - 0.00478 * Math.Sin(Rad(omega));

        var obliq0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
        var obliq  = obliq0 + 0.00256 * Math.Cos(Rad(omega));

        var decl = Deg(Math.Asin(Math.Sin(Rad(obliq)) * Math.Sin(Rad(sunAL))));

        var y = Math.Tan(Rad(obliq / 2)) * Math.Tan(Rad(obliq / 2));
        var eot = 4.0 * Deg(
              y * Math.Sin(2 * Rad(L0))
            - 2 * e * Math.Sin(Rad(M))
            + 4 * e * y * Math.Sin(Rad(M)) * Math.Cos(2 * Rad(L0))
            - 0.5 * y * y * Math.Sin(4 * Rad(L0))
            - 1.25 * e * e * Math.Sin(2 * Rad(M))
        );

        var cosHA = Math.Cos(Rad(90.833)) / (Math.Cos(Rad(latitude)) * Math.Cos(Rad(decl)))
                  - Math.Tan(Rad(latitude)) * Math.Tan(Rad(decl));
        if (cosHA < -1 || cosHA > 1) return null; // polar day/night

        var ha = Deg(Math.Acos(cosHA));
        var tzMin = tz.GetUtcOffset(local).TotalMinutes;

        var noon  = 720.0 - 4.0 * longitude - eot + tzMin;
        var rise  = noon - ha * 4.0;
        var set   = noon + ha * 4.0;

        return new SolarTimes(
            startOfDay.AddMinutes(rise),
            startOfDay.AddMinutes(set),
            startOfDay.AddMinutes(noon)
        );
    }

    private static double JulianDay(int year, int month, int day)
    {
        double y = year, m = month;
        if (m <= 2) { y -= 1; m += 12; }
        var a = Math.Floor(y / 100);
        var b = 2 - a + Math.Floor(a / 4);
        return Math.Floor(365.25 * (y + 4716)) + Math.Floor(30.6001 * (m + 1)) + day + b - 1524.5;
    }

    private static double Rad(double d) => d * Math.PI / 180.0;
    private static double Deg(double r) => r * 180.0 / Math.PI;
    private static double Mod(double a, double n) { var r = a % n; return r < 0 ? r + n : r; }
}
