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
    case rampToBedtime, bedtime
}

struct ScheduleState {
    let phase: SchedulePhase
    let currentKelvin: Int
    let currentBrightness: Double
    let dayKelvin: Int
    let nightKelvin: Int
    let dayBrightness: Double
    let nightBrightness: Double
    let bedtimeEnabled: Bool
    let bedtimeKelvin: Int
    let bedtimeBrightness: Double
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
        let r = computeSchedule(at: Date())
        gamma.applyKelvinWithBrightness(r.kelvin, brightness: r.brightness)
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

    /// Applies color temperature and brightness for a given position in the demo cycle.
    /// - When bedtime is disabled: 2-segment day → night → day cycle.
    /// - When bedtime is enabled: 3-segment day → night → bedtime → day cycle.
    private func applyDemoSettings(at demoProgress: Double) {
        let kelvin: Int
        let brightness: Double

        if settings.bedtimeEnabled {
            let third = 1.0 / 3.0
            if demoProgress < third {
                let p = demoProgress * 3.0
                kelvin     = lerp (settings.dayKelvin,     settings.nightKelvin,     p)
                brightness = lerpD(settings.dayBrightness, settings.nightBrightness, p)
            } else if demoProgress < 2.0 * third {
                let p = (demoProgress - third) * 3.0
                kelvin     = lerp (settings.nightKelvin,     settings.bedtimeKelvin,     p)
                brightness = lerpD(settings.nightBrightness, settings.bedtimeBrightness, p)
            } else {
                let p = (demoProgress - 2.0 * third) * 3.0
                kelvin     = lerp (settings.bedtimeKelvin,     settings.dayKelvin,     p)
                brightness = lerpD(settings.bedtimeBrightness, settings.dayBrightness, p)
            }
        } else {
            if demoProgress < 0.5 {
                let p = demoProgress * 2.0
                kelvin     = lerp (settings.dayKelvin,     settings.nightKelvin,     p)
                brightness = lerpD(settings.dayBrightness, settings.nightBrightness, p)
            } else {
                let p = (demoProgress - 0.5) * 2.0
                kelvin     = lerp (settings.nightKelvin,     settings.dayKelvin,     p)
                brightness = lerpD(settings.nightBrightness, settings.dayBrightness, p)
            }
        }

        gamma.applyKelvinWithBrightness(kelvin, brightness: brightness)
    }

    // MARK: – Init

    init() {
        self.state = ScheduleState(
            phase: .day, currentKelvin: 6500, currentBrightness: 1.0,
            dayKelvin: 5000, nightKelvin: 2700,
            dayBrightness: 1.0, nightBrightness: 0.8,
            bedtimeEnabled: false, bedtimeKelvin: 1900, bedtimeBrightness: 0.4,
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
        let (_, _, sunrise, sunset) = scheduleTimes(for: now)
        let r = computeSchedule(at: now)
        let next = todayAt(r.nextMinute, relativeTo: now)

        gamma.applyKelvinWithBrightness(r.kelvin, brightness: r.brightness)
        publish(r.phase, kelvin: r.kelvin, brightness: r.brightness, sunrise: sunrise, sunset: sunset, next: next, enabled: true)
    }

    // MARK: – Shared schedule computation
    //
    // Computes the target (kelvin, brightness, phase, next-anchor) for a given
    // wall-clock instant. Walks a 4-anchor timeline (day, transitionToNight,
    // night, transitionToDay) when bedtime is disabled, and a 6-anchor timeline
    // (… rampToBedtime, bedtime …) when enabled. The morning transitionToDay
    // sources from bedtime values when active, else from night values.
    //
    // Bedtime is "active" only if the wall-time falls strictly between
    // night-start and the next day-start in clockwise order — invalid configs
    // (e.g. bedtime earlier than night-start) silently fall back to the
    // 4-anchor model so the user never gets a stuck schedule.
    private func computeSchedule(at now: Date)
        -> (kelvin: Int, brightness: Double, phase: SchedulePhase, nextMinute: Int)
    {
        let (dayMin, nightMin, _, _) = scheduleTimes(for: now)
        let nowMin = minutesFromMidnight(now)
        let tranMin = settings.transitionMinutes
        let nightTransStart = wrap(nightMin - tranMin)
        let dayTransStart   = wrap(dayMin   - tranMin)

        let useBedtime = bedtimeIsActive(dayMin: dayMin, nightMin: nightMin)
        let bedMin = wrap(settings.bedtimeHour * 60 + settings.bedtimeMinute)

        // Morning transition source: bedtime values when active, else night.
        let morningFromK = useBedtime ? settings.bedtimeKelvin     : settings.nightKelvin
        let morningFromB = useBedtime ? settings.bedtimeBrightness : settings.nightBrightness

        if inRange(nowMin, from: dayMin, to: nightTransStart) {
            return (settings.dayKelvin, settings.dayBrightness, .day, nightTransStart)
        }
        if inRange(nowMin, from: nightTransStart, to: nightMin) {
            let p = progress(nowMin, from: nightTransStart, length: tranMin)
            return (lerp(settings.dayKelvin, settings.nightKelvin, p),
                    lerpD(settings.dayBrightness, settings.nightBrightness, p),
                    .transitionToNight, nightMin)
        }
        if inRange(nowMin, from: dayTransStart, to: dayMin) {
            let p = progress(nowMin, from: dayTransStart, length: tranMin)
            return (lerp(morningFromK, settings.dayKelvin, p),
                    lerpD(morningFromB, settings.dayBrightness, p),
                    .transitionToDay, dayMin)
        }
        // Past nightMin, before dayTransStart.
        if useBedtime {
            // Explicit bedtime ramp duration. Held night until rampStart,
            // then linearly (Hermite) ramp to bedtime values across
            // `bedtimeRampMinutes`. Clamped to the night→bedtime interval
            // so an over-long ramp just starts at night-start.
            let nightToBed = wrap(bedMin - nightMin)
            let rampLen = min(settings.bedtimeRampMinutes, nightToBed)
            let rampStart = wrap(bedMin - rampLen)

            if inRange(nowMin, from: nightMin, to: rampStart) {
                return (settings.nightKelvin, settings.nightBrightness, .night, rampStart)
            }
            if inRange(nowMin, from: rampStart, to: bedMin) {
                let p = progress(nowMin, from: rampStart, length: rampLen)
                return (lerp(settings.nightKelvin, settings.bedtimeKelvin, p),
                        lerpD(settings.nightBrightness, settings.bedtimeBrightness, p),
                        .rampToBedtime, bedMin)
            }
            return (settings.bedtimeKelvin, settings.bedtimeBrightness, .bedtime, dayTransStart)
        }
        return (settings.nightKelvin, settings.nightBrightness, .night, dayTransStart)
    }

    /// True iff bedtime is enabled AND falls strictly after night-start and
    /// strictly before the next day-start (in clockwise wrap order).
    private func bedtimeIsActive(dayMin: Int, nightMin: Int) -> Bool {
        guard settings.bedtimeEnabled else { return false }
        let bedMin = wrap(settings.bedtimeHour * 60 + settings.bedtimeMinute)
        let nightToDay = wrap(dayMin - nightMin)
        let nightToBed = wrap(bedMin - nightMin)
        return nightToBed > 0 && nightToBed < nightToDay
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

    // Linear interpolation — constant rate of change so a fixed time
    // slice always moves the same color delta no matter where in the
    // ramp you are. (Previously a Hermite smoothstep, which is flat at
    // both ends and made long-transition slices near 0 or 1 barely
    // move the color even when the slider said you were well into it.)
    private func lerp(_ a: Int, _ b: Int, _ t: Double) -> Int {
        return a + Int((Double(b - a) * t).rounded())
    }

    private func lerpD(_ a: Double, _ b: Double, _ t: Double) -> Double {
        return a + (b - a) * t
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
            bedtimeEnabled: settings.bedtimeEnabled,
            bedtimeKelvin: settings.bedtimeKelvin,
            bedtimeBrightness: settings.bedtimeBrightness,
            sunriseTime: sunrise, sunsetTime: sunset,
            nextEvent: next, enabled: enabled
        )
        NotificationCenter.default.post(name: Self.stateDidChange, object: state)
    }
}
