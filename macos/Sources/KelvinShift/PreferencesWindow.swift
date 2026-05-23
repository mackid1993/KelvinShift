// KelvinShift – PreferencesWindow.swift

import AppKit
import SwiftUI

// MARK: – Window controller

final class PreferencesWindowController: NSWindowController, NSWindowDelegate {
    convenience init() {
        let view = PreferencesView()
        let hc = NSHostingController(rootView: view)
        let win = NSWindow(contentViewController: hc)
        win.title = "KelvinShift Preferences"
        win.styleMask = [.titled, .closable, .resizable]
        win.setContentSize(NSSize(width: 460, height: 640))
        win.contentMinSize = NSSize(width: 460, height: 360)
        win.center()
        win.isReleasedWhenClosed = false
        self.init(window: win)
        win.delegate = self
    }

    // If the user hides this window (or Cmd-tabs) while a slider drag is
    // somehow still latched, cancel preview so subsequent settings
    // changes apply immediately rather than waiting for the 15s tick.
    func windowDidResignKey(_ notification: Notification) {
        ScheduleEngine.current?.stopPreview()
    }
}

// MARK: – SwiftUI preferences

struct PreferencesView: View {
    @ObservedObject private var s = Settings.shared
    @ObservedObject private var locationManager = LocationManager.shared
    /// Tracks which slider is currently being dragged: "day", "night", "dayBrt", "nightBrt", "bedK", "bedBrt", or nil.
    @State private var previewingSlider: String? = nil
    @GestureState private var isDaySliderPressed = false
    @GestureState private var isNightSliderPressed = false
    @GestureState private var isDayBrtSliderPressed = false
    @GestureState private var isNightBrtSliderPressed = false
    @GestureState private var isBedKSliderPressed = false
    @GestureState private var isBedBrtSliderPressed = false

    /// State for the transition demo
    @State private var isDemoRunning = false
    @State private var demoProgress: Double = 0.0

    var body: some View {
        ScrollView {
        VStack(alignment: .leading, spacing: 16) {

            // ── Color Temperature ──────────────────────
            GroupBox(label: Label("Color Temperature", systemImage: "thermometer")) {
                VStack(alignment: .leading, spacing: 12) {
                    dayKelvinRow
                    nightKelvinRow
                    if s.bedtimeEnabled { bedKelvinRow }

                    previewingIndicator
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            // ── Brightness ────────────────────────────────
            GroupBox(label: Label("Brightness", systemImage: "sun.max")) {
                VStack(alignment: .leading, spacing: 12) {
                    dayBrightnessRow
                    nightBrightnessRow
                    if s.bedtimeEnabled { bedBrightnessRow }

                    Text("Dims the screen via gamma — does not affect backlight")
                        .font(.caption).foregroundColor(.secondary)

                    previewingIndicator
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            // ── Schedule ───────────────────────────────
            GroupBox(label: Label("Schedule", systemImage: "clock")) {
                VStack(alignment: .leading, spacing: 12) {
                    Picker("Mode", selection: $s.scheduleMode) {
                        Text("Solar (sunrise / sunset)").tag("solar")
                        Text("Custom times").tag("custom")
                    }
                    .pickerStyle(.radioGroup)

                    if s.scheduleMode == "solar" {
                        HStack(spacing: 12) {
                            labelled("Lat") {
                                TextField("", value: $s.latitude,
                                          format: .number.precision(.fractionLength(4)))
                                    .frame(width: 90)
                            }
                            labelled("Lon") {
                                TextField("", value: $s.longitude,
                                          format: .number.precision(.fractionLength(4)))
                                    .frame(width: 90)
                            }
                            Button(action: requestLocation) {
                                if locationManager.isLocating {
                                    ProgressIndicator()
                                } else {
                                    Label("Use Current", systemImage: "location.fill")
                                }
                            }
                            .disabled(locationManager.isLocating)
                        }
                        if !s.locationName.isEmpty {
                            HStack(spacing: 4) {
                                Image(systemName: "mappin.circle.fill")
                                    .foregroundColor(.secondary)
                                Text(s.locationName)
                                    .foregroundColor(.secondary)
                            }
                            .font(.caption)
                        }
                        if let error = locationManager.error {
                            Text(error)
                                .font(.caption).foregroundColor(.red)
                        }
                    } else {
                        timeRow("Day starts",   hour: $s.customDayHour,   minute: $s.customDayMinute)
                        timeRow("Night starts", hour: $s.customNightHour, minute: $s.customNightMinute)
                    }

                    Divider().padding(.vertical, 4)

                    Toggle(isOn: $s.bedtimeEnabled) {
                        Label("Enable bedtime ramp", systemImage: "bed.double")
                    }
                    if s.bedtimeEnabled {
                        timeRow("Bedtime", hour: $s.bedtimeHour, minute: $s.bedtimeMinute)
                        Text("After the night setting is reached, the display ramps to the Bed temperature/brightness above. Ramp duration is under Transitions.")
                            .font(.caption).foregroundColor(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            // ── Transitions ────────────────────────────
            GroupBox(label: Label("Transitions", systemImage: "arrow.left.arrow.right")) {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Day ↔ Night")
                        .font(.caption).foregroundColor(.secondary)
                    HStack {
                        TextField("", value: $s.transitionMinutes, format: .number)
                            .frame(width: 60)
                        Text("min")
                        Stepper("", value: $s.transitionMinutes, in: 1...180, step: 5).labelsHidden()
                    }

                    if s.bedtimeEnabled {
                        Text("Night → Bedtime")
                            .font(.caption).foregroundColor(.secondary)
                            .padding(.top, 4)
                        HStack {
                            TextField("", value: $s.bedtimeRampMinutes, format: .number)
                                .frame(width: 60)
                            Text("min")
                            Stepper("", value: $s.bedtimeRampMinutes, in: 1...600, step: 5).labelsHidden()
                        }
                    }

                    Text("Smooth Hermite ramp between phases. The bedtime ramp is clamped to the night-start → bedtime window if too long.")
                        .font(.caption).foregroundColor(.secondary)
                        .fixedSize(horizontal: false, vertical: true)

                    Divider()

                    HStack {
                        Button(action: toggleDemo) {
                            Label(isDemoRunning ? "Stop" : "Preview Cycle",
                                  systemImage: isDemoRunning ? "stop.fill" : "play.fill")
                        }
                        .disabled(previewingSlider != nil)

                        if isDemoRunning {
                            ProgressView(value: demoProgress)
                                .progressViewStyle(.linear)
                            Text("\(Int(demoProgress * 100))%")
                                .font(.caption)
                                .foregroundColor(.secondary)
                                .frame(width: 35, alignment: .trailing)
                        }
                    }

                    Text("Runs through a full day/night cycle in 10 seconds")
                        .font(.caption).foregroundColor(.secondary)
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            // ── General ────────────────────────────────
            GroupBox(label: Label("General", systemImage: "gearshape")) {
                VStack(alignment: .leading, spacing: 8) {
                    if s.loginItemSupported {
                        Toggle("Open at login", isOn: $s.launchAtLogin)
                    } else {
                        Text("Open at login requires macOS 13+")
                            .font(.caption).foregroundColor(.secondary)
                    }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            HStack {
                Spacer()
                Text("Developed by David Brustein")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            .padding(.top, 4)

        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(minWidth: 460)
        // Observe Kelvin changes to drive preview while slider is held
        .onChange(of: s.dayKelvin) { newVal in
            if previewingSlider == "day" {
                ScheduleEngine.current?.updatePreview(newVal)
            }
        }
        .onChange(of: s.nightKelvin) { newVal in
            if previewingSlider == "night" {
                ScheduleEngine.current?.updatePreview(newVal)
            }
        }
        .onChange(of: s.dayBrightness) { newVal in
            if previewingSlider == "dayBrt" {
                ScheduleEngine.current?.updateBrightnessPreview(newVal)
            }
        }
        .onChange(of: s.nightBrightness) { newVal in
            if previewingSlider == "nightBrt" {
                ScheduleEngine.current?.updateBrightnessPreview(newVal)
            }
        }
        .onChange(of: s.bedtimeKelvin) { newVal in
            if previewingSlider == "bedK" {
                ScheduleEngine.current?.updatePreview(newVal)
            }
        }
        .onChange(of: s.bedtimeBrightness) { newVal in
            if previewingSlider == "bedBrt" {
                ScheduleEngine.current?.updateBrightnessPreview(newVal)
            }
        }
    }

    // MARK: – Day / Night Kelvin Rows (with live preview)

    private var dayKelvinRow: some View {
        HStack {
            Text("Day:").frame(width: 46, alignment: .trailing)
            TextField("", value: $s.dayKelvin, format: .number.grouping(.never))
                .frame(width: 60)
            Text("K")
            Stepper("", value: $s.dayKelvin, in: 1000...6500, step: 100).labelsHidden()
            Slider(value: dayKelvinDouble,
                   in: 1000...6500, step: 100)
                .simultaneousGesture(
                    DragGesture(minimumDistance: 0)
                        .updating($isDaySliderPressed) { _, state, _ in
                            state = true
                        }
                )
                .onChange(of: isDaySliderPressed) { pressed in
                    if pressed {
                        previewingSlider = "day"
                        ScheduleEngine.current?.startPreview(s.dayKelvin)
                    } else {
                        previewingSlider = nil
                        ScheduleEngine.current?.stopPreview()
                    }
                }
        }
    }

    private var nightKelvinRow: some View {
        HStack {
            Text("Night:").frame(width: 46, alignment: .trailing)
            TextField("", value: $s.nightKelvin, format: .number.grouping(.never))
                .frame(width: 60)
            Text("K")
            Stepper("", value: $s.nightKelvin, in: 1000...6500, step: 100).labelsHidden()
            Slider(value: nightKelvinDouble,
                   in: 1000...6500, step: 100)
                .simultaneousGesture(
                    DragGesture(minimumDistance: 0)
                        .updating($isNightSliderPressed) { _, state, _ in
                            state = true
                        }
                )
                .onChange(of: isNightSliderPressed) { pressed in
                    if pressed {
                        previewingSlider = "night"
                        ScheduleEngine.current?.startPreview(s.nightKelvin)
                    } else {
                        previewingSlider = nil
                        ScheduleEngine.current?.stopPreview()
                    }
                }
        }
    }

    private var dayBrightnessRow: some View {
        HStack {
            Text("Day:").frame(width: 46, alignment: .trailing)
            TextField("", value: dayBrightnessPercent, format: .number)
                .frame(width: 60)
            Text("%")
            Stepper("", value: $s.dayBrightness, in: 0.1...1.0, step: 0.05).labelsHidden()
            Slider(value: $s.dayBrightness, in: 0.1...1.0, step: 0.05)
                .simultaneousGesture(
                    DragGesture(minimumDistance: 0)
                        .updating($isDayBrtSliderPressed) { _, state, _ in
                            state = true
                        }
                )
                .onChange(of: isDayBrtSliderPressed) { pressed in
                    if pressed {
                        previewingSlider = "dayBrt"
                        ScheduleEngine.current?.startBrightnessPreview(s.dayBrightness)
                    } else {
                        previewingSlider = nil
                        ScheduleEngine.current?.stopPreview()
                    }
                }
        }
    }

    private var nightBrightnessRow: some View {
        HStack {
            Text("Night:").frame(width: 46, alignment: .trailing)
            TextField("", value: nightBrightnessPercent, format: .number)
                .frame(width: 60)
            Text("%")
            Stepper("", value: $s.nightBrightness, in: 0.1...1.0, step: 0.05).labelsHidden()
            Slider(value: $s.nightBrightness, in: 0.1...1.0, step: 0.05)
                .simultaneousGesture(
                    DragGesture(minimumDistance: 0)
                        .updating($isNightBrtSliderPressed) { _, state, _ in
                            state = true
                        }
                )
                .onChange(of: isNightBrtSliderPressed) { pressed in
                    if pressed {
                        previewingSlider = "nightBrt"
                        ScheduleEngine.current?.startBrightnessPreview(s.nightBrightness)
                    } else {
                        previewingSlider = nil
                        ScheduleEngine.current?.stopPreview()
                    }
                }
        }
    }

    private var bedKelvinRow: some View {
        HStack {
            Text("Bed:").frame(width: 46, alignment: .trailing)
            TextField("", value: $s.bedtimeKelvin, format: .number.grouping(.never))
                .frame(width: 60)
            Text("K")
            Stepper("", value: $s.bedtimeKelvin, in: 1000...6500, step: 100).labelsHidden()
            Slider(value: bedKelvinDouble, in: 1000...6500, step: 100)
                .simultaneousGesture(
                    DragGesture(minimumDistance: 0)
                        .updating($isBedKSliderPressed) { _, state, _ in
                            state = true
                        }
                )
                .onChange(of: isBedKSliderPressed) { pressed in
                    if pressed {
                        previewingSlider = "bedK"
                        ScheduleEngine.current?.startPreview(s.bedtimeKelvin)
                    } else {
                        previewingSlider = nil
                        ScheduleEngine.current?.stopPreview()
                    }
                }
        }
    }

    private var bedBrightnessRow: some View {
        HStack {
            Text("Bed:").frame(width: 46, alignment: .trailing)
            TextField("", value: bedBrightnessPercent, format: .number)
                .frame(width: 60)
            Text("%")
            Stepper("", value: $s.bedtimeBrightness, in: 0.1...1.0, step: 0.05).labelsHidden()
            Slider(value: $s.bedtimeBrightness, in: 0.1...1.0, step: 0.05)
                .simultaneousGesture(
                    DragGesture(minimumDistance: 0)
                        .updating($isBedBrtSliderPressed) { _, state, _ in
                            state = true
                        }
                )
                .onChange(of: isBedBrtSliderPressed) { pressed in
                    if pressed {
                        previewingSlider = "bedBrt"
                        ScheduleEngine.current?.startBrightnessPreview(s.bedtimeBrightness)
                    } else {
                        previewingSlider = nil
                        ScheduleEngine.current?.stopPreview()
                    }
                }
        }
    }

    @ViewBuilder
    private var previewingIndicator: some View {
        if previewingSlider != nil {
            HStack(spacing: 4) {
                Image(systemName: "eye.fill")
                Text("Previewing on display — release slider to return to schedule")
            }
            .font(.caption)
            .foregroundColor(.orange)
        }
    }

    // MARK: – Sub-views

    @ViewBuilder
    private func labelled<V: View>(_ text: String, @ViewBuilder content: () -> V) -> some View {
        HStack(spacing: 4) {
            Text(text + ":").foregroundColor(.secondary)
            content()
        }
    }

    /// Time-picker row with a fixed-width trailing-aligned label so
    /// "Day starts" / "Night starts" / "Bedtime" line up vertically.
    @ViewBuilder
    private func timeRow(_ label: String,
                         hour: Binding<Int>,
                         minute: Binding<Int>) -> some View {
        HStack(spacing: 8) {
            Text(label + ":")
                .foregroundColor(.secondary)
                .frame(width: 100, alignment: .trailing)
            hourPicker(hour, minute)
            Spacer(minLength: 0)
        }
    }

    @ViewBuilder
    private func hourPicker(_ hour: Binding<Int>, _ minute: Binding<Int>) -> some View {
        TimeField(hour: hour, minute: minute)
    }

    // MARK: – Binding adapters

    private var transitionBinding: Binding<Double> {
        Binding(get: { Double(s.transitionMinutes) },
                set: { s.transitionMinutes = Int($0) })
    }

    private var dayKelvinDouble: Binding<Double> {
        Binding(get: { Double(s.dayKelvin) },
                set: { s.dayKelvin = Int($0) })
    }

    private var nightKelvinDouble: Binding<Double> {
        Binding(get: { Double(s.nightKelvin) },
                set: { s.nightKelvin = Int($0) })
    }

    private var dayBrightnessPercent: Binding<Int> {
        Binding(get: { Int((s.dayBrightness * 100).rounded()) },
                set: { s.dayBrightness = max(0.1, min(1.0, Double($0) / 100.0)) })
    }

    private var nightBrightnessPercent: Binding<Int> {
        Binding(get: { Int((s.nightBrightness * 100).rounded()) },
                set: { s.nightBrightness = max(0.1, min(1.0, Double($0) / 100.0)) })
    }

    private var bedKelvinDouble: Binding<Double> {
        Binding(get: { Double(s.bedtimeKelvin) },
                set: { s.bedtimeKelvin = Int($0) })
    }

    private var bedBrightnessPercent: Binding<Int> {
        Binding(get: { Int((s.bedtimeBrightness * 100).rounded()) },
                set: { s.bedtimeBrightness = max(0.1, min(1.0, Double($0) / 100.0)) })
    }

    private func h12(_ h: Int) -> String {
        if h == 0  { return "12 AM" }
        if h < 12  { return "\(h) AM" }
        if h == 12 { return "12 PM" }
        return "\(h - 12) PM"
    }

    // MARK: - Location

    private func requestLocation() {
        locationManager.requestLocation { location, name in
            if let loc = location {
                s.latitude = loc.coordinate.latitude
                s.longitude = loc.coordinate.longitude
                s.locationName = name ?? ""
            }
        }
    }

    // MARK: - Transition Demo

    private func toggleDemo() {
        if isDemoRunning {
            ScheduleEngine.current?.stopDemo()
            isDemoRunning = false
            demoProgress = 0.0
        } else {
            ScheduleEngine.current?.onDemoProgressChanged = { progress in
                DispatchQueue.main.async {
                    self.demoProgress = progress
                    if progress >= 1.0 || progress == 0.0 {
                        self.isDemoRunning = ScheduleEngine.current?.isDemoRunning ?? false
                    }
                }
            }
            ScheduleEngine.current?.startDemo(durationSeconds: 10.0)
            isDemoRunning = true
        }
    }
}

// MARK: - Progress Indicator

struct ProgressIndicator: NSViewRepresentable {
    func makeNSView(context: Context) -> NSProgressIndicator {
        let indicator = NSProgressIndicator()
        indicator.style = .spinning
        indicator.controlSize = .small
        indicator.startAnimation(nil)
        return indicator
    }

    func updateNSView(_ nsView: NSProgressIndicator, context: Context) {}
}

// MARK: - Time Field
//
// Pure-SwiftUI replacement for DatePicker(hourAndMinute). The native
// NSDatePicker clips the AM/PM glyph; an NSDatePicker wrapper fixed
// that but injected an AppKit visual style that didn't match the
// rest of the form. This rebuilds the same hh:mm AM/PM affordance
// out of standard TextField + Picker so the style matches the K and
// brightness rows. Stores 24-hour internally (binding is unchanged).

struct TimeField: View {
    @Binding var hour: Int    // 0-23
    @Binding var minute: Int  // 0-59

    private var hour12: Binding<Int> {
        Binding(
            get: {
                let h = hour
                if h == 0 { return 12 }
                if h > 12 { return h - 12 }
                return h
            },
            set: { newH in
                let pm = hour >= 12
                let clamped = max(1, min(12, newH))
                let h24 = clamped == 12 ? 0 : clamped
                hour = h24 + (pm ? 12 : 0)
            }
        )
    }

    private var isPM: Binding<Bool> {
        Binding(
            get: { hour >= 12 },
            set: { pm in
                let h12 = (hour % 12 == 0) ? 12 : (hour % 12)
                let h24 = h12 == 12 ? 0 : h12
                hour = h24 + (pm ? 12 : 0)
            }
        )
    }

    private var minutePadded: Binding<String> {
        Binding(
            get: { String(format: "%02d", minute) },
            set: { newVal in
                let digits = newVal.filter(\.isNumber)
                let v = Int(digits) ?? 0
                minute = max(0, min(59, v))
            }
        )
    }

    var body: some View {
        HStack(spacing: 4) {
            TextField("", value: hour12, format: .number.grouping(.never))
                .frame(width: 30)
                .multilineTextAlignment(.trailing)
            Text(":")
            TextField("", text: minutePadded)
                .frame(width: 30)
                .multilineTextAlignment(.leading)
            Picker("", selection: isPM) {
                Text("AM").tag(false)
                Text("PM").tag(true)
            }
            .labelsHidden()
            .frame(width: 70)
        }
    }
}
