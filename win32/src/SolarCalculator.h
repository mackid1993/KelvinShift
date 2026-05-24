#pragma once

// Port of macos/Sources/KelvinShift/SolarCalculator.swift (and the C# port).
// NOAA Solar Calculator algorithm. +-1 minute accuracy between +-72 deg
// latitude. Must produce identical results to the macOS / former C# build.

#include <windows.h>
#include <optional>

struct SolarTimes
{
    // Minutes from local midnight. May fall outside [0,1440) at extreme
    // latitudes / dates; callers normalize.
    double sunriseMin;
    double sunsetMin;
    double solarNoonMin;
};

namespace SolarCalculator
{
    // localNow supplies the date; the active local UTC offset is read from
    // the OS time-zone settings. Returns nullopt for polar day/night.
    std::optional<SolarTimes> Calculate(const SYSTEMTIME& localNow,
                                        double latitude, double longitude);
}
