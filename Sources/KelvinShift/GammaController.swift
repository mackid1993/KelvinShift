// KelvinShift – GammaController.swift
//
// Direct gamma table manipulation for accurate color temperature control.
// Uses CoreGraphics CGSetDisplayTransferByTable instead of Night Shift.
//
// Color temperature values are based on blackbody radiation calculations
// from the Redshift project (Ingo Thies, 2013).

import Foundation
import CoreGraphics

final class GammaController {

    static let shared = GammaController()

    /// Blackbody color lookup table: RGB values at 100K intervals from 1000K to 10000K.
    /// Values from Redshift project, based on CIE color matching functions.
    /// At 6500K the values are (1.0, 1.0, 1.0) representing D65 white point.
    private static let blackbodyTable: [(r: Float, g: Float, b: Float)] = [
        (1.00000000, 0.18172716, 0.00000000), // 1000K
        (1.00000000, 0.25503671, 0.00000000), // 1100K
        (1.00000000, 0.30942099, 0.00000000), // 1200K
        (1.00000000, 0.35357379, 0.00000000), // 1300K
        (1.00000000, 0.39091524, 0.00000000), // 1400K
        (1.00000000, 0.42322816, 0.00000000), // 1500K
        (1.00000000, 0.45159884, 0.00000000), // 1600K
        (1.00000000, 0.47675916, 0.00000000), // 1700K
        (1.00000000, 0.49923747, 0.00000000), // 1800K
        (1.00000000, 0.51943421, 0.00000000), // 1900K
        (1.00000000, 0.54360078, 0.08679949), // 2000K
        (1.00000000, 0.56618736, 0.14065513), // 2100K
        (1.00000000, 0.58734976, 0.18362641), // 2200K
        (1.00000000, 0.60724493, 0.22137978), // 2300K
        (1.00000000, 0.62600248, 0.25591950), // 2400K
        (1.00000000, 0.64373109, 0.28819679), // 2500K
        (1.00000000, 0.66052319, 0.31873863), // 2600K
        (1.00000000, 0.67645822, 0.34786758), // 2700K
        (1.00000000, 0.69160518, 0.37579588), // 2800K
        (1.00000000, 0.70602449, 0.40267128), // 2900K
        (1.00000000, 0.71976951, 0.42860152), // 3000K
        (1.00000000, 0.73288760, 0.45366838), // 3100K
        (1.00000000, 0.74542112, 0.47793608), // 3200K
        (1.00000000, 0.75740814, 0.50145662), // 3300K
        (1.00000000, 0.76888303, 0.52427322), // 3400K
        (1.00000000, 0.77987699, 0.54642268), // 3500K
        (1.00000000, 0.79041843, 0.56793692), // 3600K
        (1.00000000, 0.80053332, 0.58884417), // 3700K
        (1.00000000, 0.81024551, 0.60916971), // 3800K
        (1.00000000, 0.81957693, 0.62893653), // 3900K
        (1.00000000, 0.82854786, 0.64816570), // 4000K
        (1.00000000, 0.83717703, 0.66687674), // 4100K
        (1.00000000, 0.84548188, 0.68508786), // 4200K
        (1.00000000, 0.85347859, 0.70281616), // 4300K
        (1.00000000, 0.86118227, 0.72007777), // 4400K
        (1.00000000, 0.86860704, 0.73688797), // 4500K
        (1.00000000, 0.87576611, 0.75326132), // 4600K
        (1.00000000, 0.88267187, 0.76921169), // 4700K
        (1.00000000, 0.88933596, 0.78475236), // 4800K
        (1.00000000, 0.89576933, 0.79989606), // 4900K
        (1.00000000, 0.90198230, 0.81465502), // 5000K
        (1.00000000, 0.90963069, 0.82838210), // 5100K
        (1.00000000, 0.91710889, 0.84190889), // 5200K
        (1.00000000, 0.92441842, 0.85523742), // 5300K
        (1.00000000, 0.93156127, 0.86836903), // 5400K
        (1.00000000, 0.93853986, 0.88130458), // 5500K
        (1.00000000, 0.94535695, 0.89404470), // 5600K
        (1.00000000, 0.95201559, 0.90658983), // 5700K
        (1.00000000, 0.95851906, 0.91894041), // 5800K
        (1.00000000, 0.96487079, 0.93109690), // 5900K
        (1.00000000, 0.97107439, 0.94305985), // 6000K
        (1.00000000, 0.97713351, 0.95482993), // 6100K
        (1.00000000, 0.98305189, 0.96640795), // 6200K
        (1.00000000, 0.98883326, 0.97779486), // 6300K
        (1.00000000, 0.99448139, 0.98899179), // 6400K
        (1.00000000, 1.00000000, 1.00000000), // 6500K (D65)
        (0.98947904, 0.99348723, 1.00000000), // 6600K
        (0.97940448, 0.98722715, 1.00000000), // 6700K
        (0.96975025, 0.98120637, 1.00000000), // 6800K
        (0.96049223, 0.97541240, 1.00000000), // 6900K
        (0.95160805, 0.96983355, 1.00000000), // 7000K
        (0.94303638, 0.96443333, 1.00000000), // 7100K
        (0.93480451, 0.95923080, 1.00000000), // 7200K
        (0.92689056, 0.95421394, 1.00000000), // 7300K
        (0.91927697, 0.94937330, 1.00000000), // 7400K
        (0.91194747, 0.94470005, 1.00000000), // 7500K
        (0.90488690, 0.94018594, 1.00000000), // 7600K
        (0.89808115, 0.93582323, 1.00000000), // 7700K
        (0.89151710, 0.93160469, 1.00000000), // 7800K
        (0.88518247, 0.92752354, 1.00000000), // 7900K
        (0.87906581, 0.92357340, 1.00000000), // 8000K
        (0.87315640, 0.91974827, 1.00000000), // 8100K
        (0.86744421, 0.91604254, 1.00000000), // 8200K
        (0.86191983, 0.91245088, 1.00000000), // 8300K
        (0.85657444, 0.90896831, 1.00000000), // 8400K
        (0.85139976, 0.90559011, 1.00000000), // 8500K
        (0.84638799, 0.90231183, 1.00000000), // 8600K
        (0.84153180, 0.89912926, 1.00000000), // 8700K
        (0.83682430, 0.89603843, 1.00000000), // 8800K
        (0.83225897, 0.89303558, 1.00000000), // 8900K
        (0.82782969, 0.89011714, 1.00000000), // 9000K
        (0.82353066, 0.88727974, 1.00000000), // 9100K
        (0.81935641, 0.88452017, 1.00000000), // 9200K
        (0.81530175, 0.88183541, 1.00000000), // 9300K
        (0.81136180, 0.87922257, 1.00000000), // 9400K
        (0.80753191, 0.87667891, 1.00000000), // 9500K
        (0.80380769, 0.87420182, 1.00000000), // 9600K
        (0.80018497, 0.87178882, 1.00000000), // 9700K
        (0.79665980, 0.86943756, 1.00000000), // 9800K
        (0.79322843, 0.86714579, 1.00000000), // 9900K
        (0.78988728, 0.86491137, 1.00000000), // 10000K
    ]

    private var savedGammaRamps: [CGDirectDisplayID: (r: [CGGammaValue], g: [CGGammaValue], b: [CGGammaValue])] = [:]
    private var currentKelvin: Int = 6500

    private init() {
        saveOriginalGamma()
    }

    // MARK: - Public API

    /// Apply a color temperature to all displays.
    /// - Parameter kelvin: Color temperature in Kelvin (1000-10000)
    @discardableResult
    func applyKelvin(_ kelvin: Int) -> Bool {
        let clamped = max(1000, min(10000, kelvin))
        currentKelvin = clamped

        let rgb = Self.kelvinToRGB(clamped)
        return applyRGBMultipliers(r: rgb.r, g: rgb.g, b: rgb.b)
    }

    /// Get the current applied color temperature.
    func getCurrentKelvin() -> Int {
        return currentKelvin
    }

    /// Reset gamma to original values (6500K equivalent).
    func resetGamma() {
        currentKelvin = 6500
        CGDisplayRestoreColorSyncSettings()
    }

    // MARK: - Color Temperature Conversion

    /// Convert Kelvin to RGB multipliers using blackbody lookup table with interpolation.
    static func kelvinToRGB(_ kelvin: Int) -> (r: Float, g: Float, b: Float) {
        let clamped = max(1000, min(10000, kelvin))

        // Calculate table index and interpolation factor
        let index = (clamped - 1000) / 100
        let alpha = Float((clamped - 1000) % 100) / 100.0

        // Handle edge case at 10000K
        if index >= blackbodyTable.count - 1 {
            return blackbodyTable[blackbodyTable.count - 1]
        }

        // Linear interpolation between two table entries
        let low = blackbodyTable[index]
        let high = blackbodyTable[index + 1]

        return (
            r: low.r + alpha * (high.r - low.r),
            g: low.g + alpha * (high.g - low.g),
            b: low.b + alpha * (high.b - low.b)
        )
    }

    // MARK: - Gamma Table Manipulation

    private func getDisplayIDs() -> [CGDirectDisplayID] {
        var displayCount: UInt32 = 0
        CGGetOnlineDisplayList(0, nil, &displayCount)

        var displays = [CGDirectDisplayID](repeating: 0, count: Int(displayCount))
        CGGetOnlineDisplayList(displayCount, &displays, &displayCount)

        return displays
    }

    private func saveOriginalGamma() {
        for displayID in getDisplayIDs() {
            let capacity = CGDisplayGammaTableCapacity(displayID)
            var sampleCount: UInt32 = 0

            var redTable = [CGGammaValue](repeating: 0, count: Int(capacity))
            var greenTable = [CGGammaValue](repeating: 0, count: Int(capacity))
            var blueTable = [CGGammaValue](repeating: 0, count: Int(capacity))

            let result = CGGetDisplayTransferByTable(
                displayID,
                capacity,
                &redTable,
                &greenTable,
                &blueTable,
                &sampleCount
            )

            if result == .success {
                savedGammaRamps[displayID] = (
                    r: Array(redTable.prefix(Int(sampleCount))),
                    g: Array(greenTable.prefix(Int(sampleCount))),
                    b: Array(blueTable.prefix(Int(sampleCount)))
                )
            }
        }
    }

    private func applyRGBMultipliers(r: Float, g: Float, b: Float) -> Bool {
        var success = true

        for displayID in getDisplayIDs() {
            let capacity = CGDisplayGammaTableCapacity(displayID)
            let count = Int(capacity)

            // Create linear ramps scaled by the RGB multipliers
            var redTable = [CGGammaValue](repeating: 0, count: count)
            var greenTable = [CGGammaValue](repeating: 0, count: count)
            var blueTable = [CGGammaValue](repeating: 0, count: count)

            for i in 0..<count {
                let value = CGGammaValue(i) / CGGammaValue(count - 1)
                redTable[i] = value * CGGammaValue(r)
                greenTable[i] = value * CGGammaValue(g)
                blueTable[i] = value * CGGammaValue(b)
            }

            let result = CGSetDisplayTransferByTable(
                displayID,
                UInt32(count),
                redTable,
                greenTable,
                blueTable
            )

            if result != .success {
                NSLog("[KelvinShift] Failed to set gamma for display \(displayID)")
                success = false
            }
        }

        return success
    }
}
