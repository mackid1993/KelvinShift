// KelvinShift – PreferencesWindow.swift

import AppKit
import SwiftUI

// MARK: – Window controller

final class PreferencesWindowController: NSWindowController {
    convenience init() {
        let view = PreferencesView()
        let hc = NSHostingController(rootView: view)
        let win = NSWindow(contentViewController: hc)
        win.title = "KelvinShift Preferences"
        win.styleMask = [.titled, .closable]
        win.setContentSize(NSSize(width: 460, height: 880))
        win.center()
        win.isReleasedWhenClosed = false
        self.init(window: win)
    }
}

// MARK: – SwiftUI preferences

struct PreferencesView: View {
    @ObservedObject private var s = Settings.shared
    @ObservedObject private var locationManager = LocationManager.shared
    /// Tracks which slider is currently being dragged: "day", "night", "dayBrt", "nightBrt", or nil.
    @State private var previewingSlider: String? = nil
    @GestureState private var isDaySliderPressed = false
    @GestureState private var isNightSliderPressed = false
    @GestureState private var isDayBrtSliderPressed = false
    @GestureState private var isNightBrtSliderPressed = false

    /// State for the transition demo
    @State private var isDemoRunning = false
    @State private var demoProgress: Double = 0.0

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {

            // ── Color Temperature ──────────────────────
            GroupBox(label: Label("Color Temperature", systemImage: "thermometer")) {
                VStack(alignment: .leading, spacing: 12) {
                    dayKelvinRow
                    Text("Recommended 5000–6500 K for daytime use")
                        .font(.caption).foregroundColor(.secondary)

                    nightKelvinRow
                    Text("Recommended 2200–3000 K for nighttime use")
                        .font(.caption).foregroundColor(.secondary)

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
                        HStack(spacing: 16) {
                            labelled("Day starts") {
                                hourPicker($s.customDayHour, $s.customDayMinute)
                            }
                            labelled("Night starts") {
                                hourPicker($s.customNightHour, $s.customNightMinute)
                            }
                        }
                    }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            // ── Transition ─────────────────────────────
            GroupBox(label: Label("Transition", systemImage: "arrow.left.arrow.right")) {
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        TextField("", value: $s.transitionMinutes, format: .number)
                            .frame(width: 60)
                        Text("minutes")
                        Stepper("", value: $s.transitionMinutes, in: 1...600, step: 5).labelsHidden()
                    }
                    Text("Duration of the smooth ramp between day and night temperatures")
                        .font(.caption).foregroundColor(.secondary)

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

            Spacer()
        }
        .padding()
        .frame(width: 460, height: 880)
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
    }

    // MARK: – Day / Night Kelvin Rows (with live preview)

    private var dayKelvinRow: some View {
        HStack {
            Text("Day:").frame(width: 46, alignment: .trailing)
            TextField("", value: $s.dayKelvin, format: .number)
                .frame(width: 60)
            Text("K")
            Stepper("", value: $s.dayKelvin, in: 2000...6500, step: 100).labelsHidden()
            Slider(value: dayKelvinDouble,
                   in: 2000...6500, step: 100)
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
            TextField("", value: $s.nightKelvin, format: .number)
                .frame(width: 60)
            Text("K")
            Stepper("", value: $s.nightKelvin, in: 1800...5500, step: 100).labelsHidden()
            Slider(value: nightKelvinDouble,
                   in: 1800...5500, step: 100)
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

    private var previewingIndicator: some View {
        HStack(spacing: 4) {
            Image(systemName: "eye.fill")
            Text("Previewing on display — release slider to return to schedule")
        }
        .font(.caption)
        .foregroundColor(.orange)
        .opacity(previewingSlider != nil ? 1 : 0)
    }

    // MARK: – Sub-views

    @ViewBuilder
    private func labelled<V: View>(_ text: String, @ViewBuilder content: () -> V) -> some View {
        HStack(spacing: 4) {
            Text(text + ":").foregroundColor(.secondary)
            content()
        }
    }

    @ViewBuilder
    private func hourPicker(_ hour: Binding<Int>, _ minute: Binding<Int>) -> some View {
        let dateBinding = Binding<Date>(
            get: {
                var components = DateComponents()
                components.hour = hour.wrappedValue
                components.minute = minute.wrappedValue
                return Calendar.current.date(from: components) ?? Date()
            },
            set: { newDate in
                let components = Calendar.current.dateComponents([.hour, .minute], from: newDate)
                hour.wrappedValue = components.hour ?? 0
                minute.wrappedValue = components.minute ?? 0
            }
        )
        DatePicker("", selection: dateBinding, displayedComponents: .hourAndMinute)
            .labelsHidden()
            .frame(width: 100)
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
