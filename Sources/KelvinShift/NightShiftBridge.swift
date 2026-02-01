// KelvinShift – NightShiftBridge.swift
//
// Pure-Swift bridge to macOS Night Shift via the private CoreBrightness framework.
// Uses ObjC runtime to call CBBlueLightClient without linking at compile time.
//
// Night Shift strength mapping:
//   0.0 = no color shift  (≈ 6500 K, native display white point)
//   1.0 = maximum warmth  (≈ 1900 K on most Macs)
//
// The exact min-Kelvin at strength 1.0 varies by hardware; user can calibrate
// via the nightShiftMinKelvin / nightShiftMaxKelvin settings.

import Foundation

final class NightShiftBridge {

    private let client: NSObject

    // MARK: – Calibration

    /// Display Kelvin when Night Shift strength = 0.0 (no shift).
    static var maxKelvin: Double = 6500

    /// Display Kelvin when Night Shift strength = 1.0 (full warmth).
    /// Measured ≈ 1900 K on most Apple Silicon Macs.
    /// Set to 2700 if your display clips before reaching true 1900 K.
    static var minKelvin: Double = 1900

    // MARK: – Init

    init?() {
        let path = "/System/Library/PrivateFrameworks/CoreBrightness.framework/CoreBrightness"
        guard dlopen(path, RTLD_NOW) != nil else {
            NSLog("[KelvinShift] dlopen CoreBrightness failed")
            return nil
        }
        guard let cls = NSClassFromString("CBBlueLightClient") as? NSObject.Type else {
            NSLog("[KelvinShift] CBBlueLightClient class not found")
            return nil
        }
        self.client = cls.init()
    }

    // MARK: – Night Shift Control

    /// Set warmth strength (0.0 – 1.0) and commit immediately.
    @discardableResult
    func setStrength(_ strength: Float) -> Bool {
        let sel = NSSelectorFromString("setStrength:commit:")
        guard client.responds(to: sel) else { return false }
        typealias Fn = @convention(c) (AnyObject, Selector, Float, Bool) -> Bool
        let fn = unsafeBitCast(client.method(for: sel), to: Fn.self)
        return fn(client, sel, max(0, min(1, strength)), true)
    }

    /// Enable or disable Night Shift.
    @discardableResult
    func setEnabled(_ enabled: Bool) -> Bool {
        let sel = NSSelectorFromString("setEnabled:")
        guard client.responds(to: sel) else { return false }
        typealias Fn = @convention(c) (AnyObject, Selector, Bool) -> Bool
        let fn = unsafeBitCast(client.method(for: sel), to: Fn.self)
        return fn(client, sel, enabled)
    }

    /// Set schedule mode: 0 = manual (off), 1 = sunset-to-sunrise, 2 = custom schedule.
    /// We use 0 (manual) so KelvinShift controls timing exclusively.
    @discardableResult
    func setMode(_ mode: Int32) -> Bool {
        let sel = NSSelectorFromString("setMode:")
        guard client.responds(to: sel) else { return false }
        typealias Fn = @convention(c) (AnyObject, Selector, Int32) -> Bool
        let fn = unsafeBitCast(client.method(for: sel), to: Fn.self)
        return fn(client, sel, mode)
    }

    /// Read current Night Shift strength.
    func getStrength() -> Float? {
        let sel = NSSelectorFromString("getStrength:")
        guard client.responds(to: sel) else { return nil }
        var strength: Float = 0
        typealias Fn = @convention(c) (AnyObject, Selector, UnsafeMutablePointer<Float>) -> Bool
        let fn = unsafeBitCast(client.method(for: sel), to: Fn.self)
        guard fn(client, sel, &strength) else { return nil }
        return strength
    }

    // MARK: – Kelvin ↔ Strength Conversion

    /// Map a Kelvin value to Night Shift strength (0.0 – 1.0).
    static func kelvinToStrength(_ kelvin: Int) -> Float {
        let s = (maxKelvin - Double(kelvin)) / (maxKelvin - minKelvin)
        return Float(max(0, min(1, s)))
    }

    /// Map a Night Shift strength back to approximate Kelvin.
    static func strengthToKelvin(_ strength: Float) -> Int {
        let k = maxKelvin - Double(strength) * (maxKelvin - minKelvin)
        return Int(max(minKelvin, min(maxKelvin, k)).rounded())
    }

    /// Convenience: apply a Kelvin value directly.
    @discardableResult
    func applyKelvin(_ kelvin: Int) -> Bool {
        setStrength(NightShiftBridge.kelvinToStrength(kelvin))
    }
}
