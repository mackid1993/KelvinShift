// KelvinShift – ScheduleEngine.swift
//
// Runs a 15-second timer that:
//   1. Determines the current phase (day / night / transitioning).
//   2. Calculates the target Kelvin for this instant.
//   3. Applies it via direct gamma table manipulation.
//   4. Publishes state for the status-bar UI.
//
// Also supports a "preview" mode where an external caller (the Preferences
// sliders) can temporarily override the display Kelvin while the user drags.

import Foundation

// MARK: – Public State

enum SchedulePhase: String {
    case day, night, transitionToNight, transitionToDay
}

struct ScheduleState {
    let phase: SchedulePhase
    let currentKelvin: Int
    let dayKelvin: Int
    let nightKelvin: Int
    let sunriseTime: Date?
    let sunsetTime: Date?
    let nextEvent: Date?
    let enabled: Bool
}

// MARK: – Engine

final class ScheduleEngine {

    static let stateDidChange = Notification.Name("KSStateChanged")

    /// Convenient shared reference so PreferencesView can call preview methods.
    static weak var current: ScheduleEngine?

    private let gamma = GammaController.shared
    private let settings = Settings.shared
    private var timer: Timer?

    private(set) var state: ScheduleState

    // MARK: – Preview

    /// When non-nil, the engine skips applying its scheduled Kelvin;
    /// the preview value is applied directly instead.
    private var previewKelvin: Int?

    /// Called when the user begins dragging a Kelvin slider.
    func startPreview(_ kelvin: Int) {
        previewKelvin = kelvin
        gamma.applyKelvin(kelvin)
    }

    /// Called on every slider value change while dragging.
    func updatePreview(_ kelvin: Int) {
        guard previewKelvin != nil else { return }
        previewKelvin = kelvin
        gamma.applyKelvin(kelvin)
    }

    /// Called when the user releases the slider. The engine immediately
    /// re-applies the schedule's correct Kelvin.
    func stopPreview() {
        previewKelvin = nil
        // Force immediate tick to restore scheduled temperature
        let now = Date()
        let (dayMin, nightMin, _, _) = scheduleTimes(for: now)
        let nowMin = minutesFromMidnight(now)
        let tranMin = settings.transitionMinutes
        let nightTransStart = wrap(nightMin - tranMin)
        let dayTransStart = wrap(dayMin - tranMin)

        let kelvin: Int
        if inRange(nowMin, from: dayMin, to: nightTransStart) {
            kelvin = settings.dayKelvin
        } else if inRange(nowMin, from: nightTransStart, to: nightMin) {
            let p = progress(nowMin, from: nightTransStart, length: tranMin)
            kelvin = lerp(settings.dayKelvin, settings.nightKelvin, p)
        } else if inRange(nowMin, from: nightMin, to: dayTransStart) {
            kelvin = settings.nightKelvin
        } else {
            let p = progress(nowMin, from: dayTransStart, length: tranMin)
            kelvin = lerp(settings.nightKelvin, settings.dayKelvin, p)
        }

        gamma.applyKelvin(kelvin)
    }

    // MARK: – Init

    init() {
        self.state = ScheduleState(
            phase: .day, currentKelvin: 6500,
            dayKelvin: 5000, nightKelvin: 2700,
            sunriseTime: nil, sunsetTime: nil,
            nextEvent: nil, enabled: true
        )
        Self.current = self

        NotificationCenter.default.addObserver(
            self, selector: #selector(onSettingsChanged),
            name: Settings.didChange, object: nil
        )
    }

    func start() {
        tick()                     // apply immediately
        timer = Timer.scheduledTimer(withTimeInterval: 15, repeats: true) { [weak self] _ in
            self?.tick()
        }
        RunLoop.current.add(timer!, forMode: .common)
    }

    func stop() {
        timer?.invalidate()
        timer = nil
        gamma.resetGamma()
    }

    // MARK: – Timer callback

    @objc private func onSettingsChanged() {
        guard previewKelvin == nil else { return }
        tick()
    }

    private func tick() {
        guard previewKelvin == nil else { return }

        guard settings.enabled else {
            gamma.resetGamma()
            publish(.day, kelvin: 6500, sunrise: nil, sunset: nil, next: nil, enabled: false)
            return
        }

        let now = Date()
        let (dayMin, nightMin, sunrise, sunset) = scheduleTimes(for: now)
        let nowMin   = minutesFromMidnight(now)
        let tranMin  = settings.transitionMinutes

        let nightTransStart = wrap(nightMin - tranMin)
        let dayTransStart   = wrap(dayMin   - tranMin)

        let kelvin: Int
        let phase: SchedulePhase
        var next: Date? = nil

        if inRange(nowMin, from: dayMin, to: nightTransStart) {
            phase  = .day
            kelvin = settings.dayKelvin
            next   = todayAt(nightTransStart, relativeTo: now)

        } else if inRange(nowMin, from: nightTransStart, to: nightMin) {
            phase  = .transitionToNight
            let p  = progress(nowMin, from: nightTransStart, length: tranMin)
            kelvin = lerp(settings.dayKelvin, settings.nightKelvin, p)
            next   = todayAt(nightMin, relativeTo: now)

        } else if inRange(nowMin, from: nightMin, to: dayTransStart) {
            phase  = .night
            kelvin = settings.nightKelvin
            next   = todayAt(dayTransStart, relativeTo: now)

        } else {
            phase  = .transitionToDay
            let p  = progress(nowMin, from: dayTransStart, length: tranMin)
            kelvin = lerp(settings.nightKelvin, settings.dayKelvin, p)
            next   = todayAt(dayMin, relativeTo: now)
        }

        gamma.applyKelvin(kelvin)
        publish(phase, kelvin: kelvin, sunrise: sunrise, sunset: sunset, next: next, enabled: true)
    }

    // MARK: – Schedule helpers

    private func scheduleTimes(for date: Date) -> (Int, Int, Date?, Date?) {
        if settings.scheduleMode == "solar",
           let s = SolarCalculator.calculate(date: date,
                                             latitude: settings.latitude,
                                             longitude: settings.longitude) {
            return (minutesFromMidnight(s.sunrise),
                    minutesFromMidnight(s.sunset),
                    s.sunrise, s.sunset)
        }
        return (settings.customDayHour   * 60 + settings.customDayMinute,
                settings.customNightHour * 60 + settings.customNightMinute,
                nil, nil)
    }

    // MARK: – Circular-clock math

    private func wrap(_ m: Int) -> Int { ((m % 1440) + 1440) % 1440 }

    private func inRange(_ t: Int, from: Int, to: Int) -> Bool {
        from <= to
            ? (t >= from && t < to)
            : (t >= from || t < to)
    }

    private func progress(_ t: Int, from: Int, length: Int) -> Double {
        let elapsed = wrap(t - from)
        return min(1, Double(elapsed) / Double(max(1, length)))
    }

    private func lerp(_ a: Int, _ b: Int, _ t: Double) -> Int {
        let s = t * t * (3 - 2 * t)
        return a + Int((Double(b - a) * s).rounded())
    }

    private func minutesFromMidnight(_ date: Date) -> Int {
        let c = Calendar.current
        return c.component(.hour, from: date) * 60 + c.component(.minute, from: date)
    }

    private func todayAt(_ minutes: Int, relativeTo ref: Date) -> Date {
        let startOfDay = Calendar.current.startOfDay(for: ref)
        var d = startOfDay.addingTimeInterval(Double(minutes) * 60)
        if d < ref { d = d.addingTimeInterval(86400) }
        return d
    }

    // MARK: – State publishing

    private func publish(
        _ phase: SchedulePhase, kelvin: Int,
        sunrise: Date?, sunset: Date?, next: Date?, enabled: Bool
    ) {
        state = ScheduleState(
            phase: phase, currentKelvin: kelvin,
            dayKelvin: settings.dayKelvin, nightKelvin: settings.nightKelvin,
            sunriseTime: sunrise, sunsetTime: sunset,
            nextEvent: next, enabled: enabled
        )
        NotificationCenter.default.post(name: Self.stateDidChange, object: state)
    }
}
