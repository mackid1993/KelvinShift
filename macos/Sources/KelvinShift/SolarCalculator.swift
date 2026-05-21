// KelvinShift – SolarCalculator.swift
//
// Sunrise / sunset calculation using the NOAA Solar Calculator algorithm.
// Accuracy: ± 1 minute for latitudes between ± 72°.

import Foundation

struct SolarTimes {
    let sunrise: Date
    let sunset: Date
    let solarNoon: Date
}

enum SolarCalculator {

    /// Returns sunrise, sunset, and solar noon for the given date and location.
    /// Returns `nil` for polar day / polar night (no sunrise or sunset).
    static func calculate(
        date: Date,
        latitude: Double,
        longitude: Double,
        timeZone: TimeZone = .current
    ) -> SolarTimes? {

        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = timeZone
        let startOfDay = cal.startOfDay(for: date)

        let year  = cal.component(.year,  from: date)
        let month = cal.component(.month, from: date)
        let day   = cal.component(.day,   from: date)

        // Julian day number (at noon UT)
        let jd = julianDay(year: year, month: month, day: day)
        let T  = (jd - 2451545.0) / 36525.0          // Julian century

        // Geometric mean longitude of the Sun (degrees)
        let L0 = fmod(280.46646 + T * (36000.76983 + 0.0003032 * T), 360)

        // Geometric mean anomaly of the Sun (degrees)
        let M = 357.52911 + T * (35999.05029 - 0.0001537 * T)

        // Eccentricity of Earth's orbit
        let e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T)

        // Sun equation of center (degrees)
        let C = sin(rad(M)) * (1.914602 - T * (0.004817 + 0.000014 * T))
              + sin(rad(2 * M)) * (0.019993 - 0.000101 * T)
              + sin(rad(3 * M)) * 0.000289

        // Sun true longitude & apparent longitude
        let sunTL  = L0 + C
        let omega  = 125.04 - 1934.136 * T
        let sunAL  = sunTL - 0.00569 - 0.00478 * sin(rad(omega))

        // Mean obliquity of the ecliptic
        let obliq0 = 23.0
            + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0
        let obliq  = obliq0 + 0.00256 * cos(rad(omega))

        // Solar declination (degrees)
        let decl = deg(asin(sin(rad(obliq)) * sin(rad(sunAL))))

        // Equation of time (minutes)
        let y = tan(rad(obliq / 2)) * tan(rad(obliq / 2))
        let eot = 4.0 * deg(
              y * sin(2 * rad(L0))
            - 2 * e * sin(rad(M))
            + 4 * e * y * sin(rad(M)) * cos(2 * rad(L0))
            - 0.5 * y * y * sin(4 * rad(L0))
            - 1.25 * e * e * sin(2 * rad(M))
        )

        // Hour angle at sunrise / sunset (using 90.833° for atmospheric refraction)
        let cosHA = (cos(rad(90.833)) / (cos(rad(latitude)) * cos(rad(decl))))
                  - tan(rad(latitude)) * tan(rad(decl))

        guard cosHA >= -1, cosHA <= 1 else { return nil }   // polar day or night
        let ha = deg(acos(cosHA))

        // Time-zone offset in minutes from UTC
        let tzMin = Double(timeZone.secondsFromGMT(for: date)) / 60.0

        // Solar noon, sunrise, sunset – all in minutes from local midnight
        let noon    = 720.0 - 4.0 * longitude - eot + tzMin
        let riseMn  = noon - ha * 4.0
        let setMn   = noon + ha * 4.0

        return SolarTimes(
            sunrise:   startOfDay.addingTimeInterval(riseMn * 60),
            sunset:    startOfDay.addingTimeInterval(setMn  * 60),
            solarNoon: startOfDay.addingTimeInterval(noon   * 60)
        )
    }

    // MARK: – Helpers

    private static func julianDay(year: Int, month: Int, day: Int) -> Double {
        var y = Double(year), m = Double(month)
        if m <= 2 { y -= 1; m += 12 }
        let a = floor(y / 100)
        let b = 2 - a + floor(a / 4)
        return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + Double(day) + b - 1524.5
    }

    private static func rad(_ d: Double) -> Double { d * .pi / 180 }
    private static func deg(_ r: Double) -> Double { r * 180 / .pi }
}
