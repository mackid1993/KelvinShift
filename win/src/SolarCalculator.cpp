#include "SolarCalculator.h"
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double Rad(double d) { return d * kPi / 180.0; }
double Deg(double r) { return r * 180.0 / kPi; }
double Mod(double a, double n) { double r = fmod(a, n); return r < 0 ? r + n : r; }

double JulianDay(int year, int month, int day)
{
    double y = year, m = month;
    if (m <= 2) { y -= 1; m += 12; }
    double a = floor(y / 100);
    double b = 2 - a + floor(a / 4);
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + day + b - 1524.5;
}

// Active local UTC offset in minutes (e.g. -300 for EST). Matches
// TimeZoneInfo.Local.GetUtcOffset for the current date.
double LocalUtcOffsetMinutes()
{
    TIME_ZONE_INFORMATION tz{};
    DWORD r = GetTimeZoneInformation(&tz);
    LONG extra = 0;
    if (r == TIME_ZONE_ID_DAYLIGHT)      extra = tz.DaylightBias;
    else if (r == TIME_ZONE_ID_STANDARD) extra = tz.StandardBias;
    // UTC = local + Bias; offset of local from UTC is the negation.
    return -(double)(tz.Bias + extra);
}

} // namespace

std::optional<SolarTimes> SolarCalculator::Calculate(const SYSTEMTIME& localNow,
                                                     double latitude, double longitude)
{
    int year = localNow.wYear, month = localNow.wMonth, day = localNow.wDay;

    double jd = JulianDay(year, month, day);
    double T = (jd - 2451545.0) / 36525.0;

    double L0 = Mod(280.46646 + T * (36000.76983 + 0.0003032 * T), 360);
    double M  = 357.52911 + T * (35999.05029 - 0.0001537 * T);
    double e  = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);

    double C = sin(Rad(M)) * (1.914602 - T * (0.004817 + 0.000014 * T))
             + sin(Rad(2 * M)) * (0.019993 - 0.000101 * T)
             + sin(Rad(3 * M)) * 0.000289;

    double sunTL = L0 + C;
    double omega = 125.04 - 1934.136 * T;
    double sunAL = sunTL - 0.00569 - 0.00478 * sin(Rad(omega));

    double obliq0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
    double obliq  = obliq0 + 0.00256 * cos(Rad(omega));

    double decl = Deg(asin(sin(Rad(obliq)) * sin(Rad(sunAL))));

    double y = tan(Rad(obliq / 2)) * tan(Rad(obliq / 2));
    double eot = 4.0 * Deg(
          y * sin(2 * Rad(L0))
        - 2 * e * sin(Rad(M))
        + 4 * e * y * sin(Rad(M)) * cos(2 * Rad(L0))
        - 0.5 * y * y * sin(4 * Rad(L0))
        - 1.25 * e * e * sin(2 * Rad(M)));

    double cosHA = cos(Rad(90.833)) / (cos(Rad(latitude)) * cos(Rad(decl)))
                 - tan(Rad(latitude)) * tan(Rad(decl));
    if (cosHA < -1 || cosHA > 1) return std::nullopt; // polar day/night

    double ha = Deg(acos(cosHA));
    double tzMin = LocalUtcOffsetMinutes();

    double noon = 720.0 - 4.0 * longitude - eot + tzMin;
    double rise = noon - ha * 4.0;
    double set  = noon + ha * 4.0;

    return SolarTimes{ rise, set, noon };
}
