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
        win.setContentSize(NSSize(width: 460, height: 560))
        win.center()
        win.isReleasedWhenClosed = false
        self.init(window: win)
    }
}

// MARK: – SwiftUI preferences

struct PreferencesView: View {
    @ObservedObject private var s = Settings.shared
    @State private var showCalibration = false

    /// Tracks which slider is currently being dragged: "day", "night", or nil.
    @State private var previewingSlider: String? = nil

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

                    if previewingSlider != nil {
                        HStack(spacing: 4) {
                            Image(systemName: "eye.fill")
                            Text("Previewing on display — release slider to return to schedule")
                        }
                        .font(.caption)
                        .foregroundColor(.orange)
                    }
                }
                .padding(.vertical, 4)
            }

            // ── Schedule ───────────────────────────────
            GroupBox(label: Label("Schedule", systemImage: "clock")) {
                VStack(alignment: .leading, spacing: 10) {
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
                        }
                        Text("Default: Nanuet, NY  (41.10, −74.01)")
                            .font(.caption).foregroundColor(.secondary)
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
                .padding(.vertical, 4)
            }

            // ── Transition ─────────────────────────────
            GroupBox(label: Label("Transition", systemImage: "arrow.left.arrow.right")) {
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Slider(value: transitionBinding, in: 1...60, step: 1)
                        Text("\(s.transitionMinutes) min").monospacedDigit().frame(width: 50)
                    }
                    Text("Duration of the smooth ramp between day and night temperatures")
                        .font(.caption).foregroundColor(.secondary)
                }
                .padding(.vertical, 4)
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
                .padding(.vertical, 4)
            }

            // ── Calibration ────────────────────────────
            DisclosureGroup("Advanced Calibration", isExpanded: $showCalibration) {
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Text("NS max-warmth ≈")
                        TextField("", value: $s.calibrationMinK, format: .number)
                            .frame(width: 60)
                        Text("K")
                        Stepper("", value: $s.calibrationMinK, in: 1200...3500, step: 100)
                            .labelsHidden()
                    }
                    Text("""
                        What Kelvin value does Night Shift strength 1.0 correspond to \
                        on your display? Default 1900 K. Use a colorimeter to measure \
                        if you want higher accuracy. Raise this value (e.g. 2700) if \
                        your display clips before reaching true 1900 K.
                        """)
                        .font(.caption).foregroundColor(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .padding(.top, 4)
            }

            Spacer()
        }
        .padding()
        .frame(width: 460, height: 560)
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
                   in: 2000...6500, step: 100,
                   onEditingChanged: { editing in
                        if editing {
                            previewingSlider = "day"
                            ScheduleEngine.current?.startPreview(s.dayKelvin)
                        } else {
                            previewingSlider = nil
                            ScheduleEngine.current?.stopPreview()
                        }
                   })
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
                   in: 1800...5500, step: 100,
                   onEditingChanged: { editing in
                        if editing {
                            previewingSlider = "night"
                            ScheduleEngine.current?.startPreview(s.nightKelvin)
                        } else {
                            previewingSlider = nil
                            ScheduleEngine.current?.stopPreview()
                        }
                   })
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

    @ViewBuilder
    private func hourPicker(_ hour: Binding<Int>, _ minute: Binding<Int>) -> some View {
        HStack(spacing: 2) {
            Picker("", selection: hour) {
                ForEach(0..<24, id: \.self) { h in Text(h12(h)).tag(h) }
            }
            .frame(width: 80).labelsHidden()

            Picker("", selection: minute) {
                ForEach([0, 15, 30, 45], id: \.self) { m in
                    Text(String(format: ":%02d", m)).tag(m)
                }
            }
            .frame(width: 52).labelsHidden()
        }
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

    private func h12(_ h: Int) -> String {
        if h == 0  { return "12 AM" }
        if h < 12  { return "\(h) AM" }
        if h == 12 { return "12 PM" }
        return "\(h - 12) PM"
    }
}
