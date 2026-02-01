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
    let currentBrightness: Double
    let dayKelvin: Int
    let nightKelvin: Int
    let dayBrightness: Double
    let nightBrightness: Double
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
    private var previewBrightness: Double?

    // MARK: – Transition Demo

    /// Timer for running the transition demo
    private var demoTimer: Timer?
    /// Current progress through the demo (0.0 to 1.0 represents a full day cycle)
    private(set) var demoProgress: Double = 0.0
    /// Whether the demo is currently running
    private(set) var isDemoRunning: Bool = false
    /// Callback for demo progress updates
    var onDemoProgressChanged: ((Double) -> Void)?

    /// Called when the user begins dragging a Kelvin slider.
    func startPreview(_ kelvin: Int) {
        previewKelvin = kelvin
        previewBrightness = nil
        gamma.applyKelvin(kelvin)
    }

    /// Called on every slider value change while dragging.
    func updatePreview(_ kelvin: Int) {
        guard previewKelvin != nil else { return }
        previewKelvin = kelvin
        gamma.applyKelvin(kelvin)
    }

    /// Called when the user begins dragging a brightness slider.
    func startBrightnessPreview(_ brightness: Double) {
        previewBrightness = brightness
        previewKelvin = nil
        // Use current scheduled kelvin with preview brightness
        gamma.applyKelvinWithBrightness(state.currentKelvin, brightness: brightness)
    }

    /// Called on every brightness slider value change while dragging.
    func updateBrightnessPreview(_ brightness: Double) {
        guard previewBrightness != nil else { return }
        previewBrightness = brightness
        gamma.applyKelvinWithBrightness(state.currentKelvin, brightness: brightness)
    }

    /// Called when the user releases the slider. The engine immediately
    /// re-applies the schedule's correct Kelvin.
    func stopPreview() {
        previewKelvin = nil
        previewBrightness = nil
        restoreScheduledSettings()
    }

    /// Restores the scheduled Kelvin and brightness based on current time
    private func restoreScheduledSettings() {
        let now = Date()
        let (dayMin, nightMin, _, _) = scheduleTimes(for: now)
        let nowMin = minutesFromMidnight(now)
        let tranMin = settings.transitionMinutes
        let nightTransStart = wrap(nightMin - tranMin)
        let dayTransStart = wrap(dayMin - tranMin)

        let kelvin: Int
        let brightness: Double
        if inRange(nowMin, from: dayMin, to: nightTransStart) {
            kelvin = settings.dayKelvin
            brightness = settings.dayBrightness
        } else if inRange(nowMin, from: nightTransStart, to: nightMin) {
            let p = progress(nowMin, from: nightTransStart, length: tranMin)
            kelvin = lerp(settings.dayKelvin, settings.nightKelvin, p)
            brightness = lerpD(settings.dayBrightness, settings.nightBrightness, p)
        } else if inRange(nowMin, from: nightMin, to: dayTransStart) {
            kelvin = settings.nightKelvin
            brightness = settings.nightBrightness
        } else {
            let p = progress(nowMin, from: dayTransStart, length: tranMin)
            kelvin = lerp(settings.nightKelvin, settings.dayKelvin, p)
            brightness = lerpD(settings.nightBrightness, settings.dayBrightness, p)
        }

        gamma.applyKelvinWithBrightness(kelvin, brightness: brightness)
    }

    /// Starts the transition demo, cycling through a full day in the specified duration
    /// - Parameter durationSeconds: Total duration for one full day cycle (default: 10 seconds)
    func startDemo(durationSeconds: Double = 10.0) {
        guard !isDemoRunning else { return }
        isDemoRunning = true
        demoProgress = 0.0

        // Cancel any slider preview
        previewKelvin = nil
        previewBrightness = nil

        let updateInterval = 1.0 / 60.0 // 60 fps for smooth animation
        let progressPerTick = updateInterval / durationSeconds

        demoTimer = Timer.scheduledTimer(withTimeInterval: updateInterval, repeats: true) { [weak self] _ in
            guard let self = self else { return }

            self.demoProgress += progressPerTick
            if self.demoProgress >= 1.0 {
                self.stopDemo()
                return
            }

            self.applyDemoSettings(at: self.demoProgress)
            self.onDemoProgressChanged?(self.demoProgress)
        }
        RunLoop.current.add(demoTimer!, forMode: .common)

        // Apply initial settings
        applyDemoSettings(at: 0.0)
        onDemoProgressChanged?(0.0)
    }

    /// Stops the transition demo and restores scheduled settings
    func stopDemo() {
        demoTimer?.invalidate()
        demoTimer = nil
        isDemoRunning = false
        demoProgress = 0.0
        onDemoProgressChanged?(0.0)
        restoreScheduledSettings()
    }

    /// Applies color temperature and brightness for a given position in the demo cycle
    /// - Parameter demoProgress: Progress through demo (0.0 = day, 0.5 = night, 1.0 = back to day)
    private func applyDemoSettings(at demoProgress: Double) {
        // Simple cycle: day → night → day
        // First half (0.0 to 0.5): transition from day to night
        // Second half (0.5 to 1.0): transition from night to day

        let kelvin: Int
        let brightness: Double

        if demoProgress < 0.5 {
            // Transitioning from day to night (0.0 → 0.5 maps to 0.0 → 1.0)
            let p = demoProgress * 2.0
            kelvin = lerp(settings.dayKelvin, settings.nightKelvin, p)
            brightness = lerpD(settings.dayBrightness, settings.nightBrightness, p)
        } else {
            // Transitioning from night to day (0.5 → 1.0 maps to 0.0 → 1.0)
            let p = (demoProgress - 0.5) * 2.0
            kelvin = lerp(settings.nightKelvin, settings.dayKelvin, p)
            brightness = lerpD(settings.nightBrightness, settings.dayBrightness, p)
        }

        gamma.applyKelvinWithBrightness(kelvin, brightness: brightness)
    }

    // MARK: – Init

    init() {
        self.state = ScheduleState(
            phase: .day, currentKelvin: 6500, currentBrightness: 1.0,
            dayKelvin: 5000, nightKelvin: 2700,
            dayBrightness: 1.0, nightBrightness: 0.8,
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
        guard previewKelvin == nil && previewBrightness == nil && !isDemoRunning else { return }
        tick()
    }

    private func tick() {
        guard previewKelvin == nil && previewBrightness == nil && !isDemoRunning else { return }

        guard settings.enabled else {
            gamma.resetGamma()
            publish(.day, kelvin: 6500, brightness: 1.0, sunrise: nil, sunset: nil, next: nil, enabled: false)
            return
        }

        let now = Date()
        let (dayMin, nightMin, sunrise, sunset) = scheduleTimes(for: now)
        let nowMin   = minutesFromMidnight(now)
        let tranMin  = settings.transitionMinutes

        let nightTransStart = wrap(nightMin - tranMin)
        let dayTransStart   = wrap(dayMin   - tranMin)

        let kelvin: Int
        let brightness: Double
        let phase: SchedulePhase
        var next: Date? = nil

        if inRange(nowMin, from: dayMin, to: nightTransStart) {
            phase  = .day
            kelvin = settings.dayKelvin
            brightness = settings.dayBrightness
            next   = todayAt(nightTransStart, relativeTo: now)

        } else if inRange(nowMin, from: nightTransStart, to: nightMin) {
            phase  = .transitionToNight
            let p  = progress(nowMin, from: nightTransStart, length: tranMin)
            kelvin = lerp(settings.dayKelvin, settings.nightKelvin, p)
            brightness = lerpD(settings.dayBrightness, settings.nightBrightness, p)
            next   = todayAt(nightMin, relativeTo: now)

        } else if inRange(nowMin, from: nightMin, to: dayTransStart) {
            phase  = .night
            kelvin = settings.nightKelvin
            brightness = settings.nightBrightness
            next   = todayAt(dayTransStart, relativeTo: now)

        } else {
            phase  = .transitionToDay
            let p  = progress(nowMin, from: dayTransStart, length: tranMin)
            kelvin = lerp(settings.nightKelvin, settings.dayKelvin, p)
            brightness = lerpD(settings.nightBrightness, settings.dayBrightness, p)
            next   = todayAt(dayMin, relativeTo: now)
        }

        gamma.applyKelvinWithBrightness(kelvin, brightness: brightness)
        publish(phase, kelvin: kelvin, brightness: brightness, sunrise: sunrise, sunset: sunset, next: next, enabled: true)
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

    private func lerpD(_ a: Double, _ b: Double, _ t: Double) -> Double {
        let s = t * t * (3 - 2 * t)
        return a + (b - a) * s
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
        _ phase: SchedulePhase, kelvin: Int, brightness: Double,
        sunrise: Date?, sunset: Date?, next: Date?, enabled: Bool
    ) {
        state = ScheduleState(
            phase: phase, currentKelvin: kelvin, currentBrightness: brightness,
            dayKelvin: settings.dayKelvin, nightKelvin: settings.nightKelvin,
            dayBrightness: settings.dayBrightness, nightBrightness: settings.nightBrightness,
            sunriseTime: sunrise, sunsetTime: sunset,
            nextEvent: next, enabled: enabled
        )
        NotificationCenter.default.post(name: Self.stateDidChange, object: state)
    }
}
