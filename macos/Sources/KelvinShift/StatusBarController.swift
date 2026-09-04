// KelvinShift – StatusBarController.swift

import AppKit

final class StatusBarController {

    private let statusItem: NSStatusItem
    private let engine: ScheduleEngine
    private var prefsWC: PreferencesWindowController?

    // Dynamic menu items
    private var miCurrent:  NSMenuItem!
    private var miPhase:    NSMenuItem!
    private var miDay:      NSMenuItem!
    private var miNight:    NSMenuItem!
    private var miBedtime:  NSMenuItem!
    private var miSchedule: NSMenuItem!
    private var miEnabled:  NSMenuItem!

    init(engine: ScheduleEngine) {
        self.engine = engine
        self.statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        // The status item's title tracks the current temperature, and AppKit derives the
        // position autosave key from the title when no explicit name is set. That minted a
        // fresh identity on every ramp step (" 2700K", " 2638K", " 2460K", ...), so macOS
        // never had a remembered position and the icon moved on each update. Pin a stable
        // name once, before any title is applied, and never change it.
        self.statusItem.autosaveName = "KelvinShift"
        buildMenu()
        refresh()

        NotificationCenter.default.addObserver(
            self, selector: #selector(onStateChange),
            name: ScheduleEngine.stateDidChange, object: nil
        )
    }

    // MARK: – Menu construction

    private func buildMenu() {
        let m = NSMenu()

        miCurrent = addItem(m, "")
        miPhase   = addItem(m, "")
        m.addItem(.separator())
        miDay     = addItem(m, "")
        miNight   = addItem(m, "")
        miBedtime = addItem(m, "")
        miSchedule = addItem(m, "")
        m.addItem(.separator())

        miEnabled = NSMenuItem(title: "Enabled", action: #selector(toggleEnabled), keyEquivalent: "e")
        miEnabled.target = self
        m.addItem(miEnabled)

        m.addItem(.separator())
        let pref = NSMenuItem(title: "Preferences…", action: #selector(openPrefs), keyEquivalent: ",")
        pref.target = self
        m.addItem(pref)

        let quit = NSMenuItem(title: "Quit KelvinShift", action: #selector(doQuit), keyEquivalent: "q")
        quit.target = self
        m.addItem(quit)

        statusItem.menu = m
    }

    private func addItem(_ menu: NSMenu, _ title: String) -> NSMenuItem {
        let mi = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        mi.isEnabled = false
        menu.addItem(mi)
        return mi
    }

    // MARK: – Refresh

    @objc private func onStateChange() {
        DispatchQueue.main.async { [weak self] in self?.refresh() }
    }

    private func refresh() {
        let s = engine.state

        // ── Status bar button ──────────────────────────
        if let btn = statusItem.button {
            if !s.enabled {
                btn.image = symbolImage("power.circle")
                btn.title = " Off"
            } else {
                btn.image = symbolImage(phaseSymbol(s.phase))
                btn.title = " \(s.currentKelvin)K"
            }
            btn.imagePosition = .imageLeading
            btn.font = NSFont.monospacedDigitSystemFont(ofSize: 0, weight: .regular)
        }

        // ── Drop-down items ────────────────────────────
        let currentBrtPct = Int((s.currentBrightness * 100).rounded())
        let dayBrtPct = Int((s.dayBrightness * 100).rounded())
        let nightBrtPct = Int((s.nightBrightness * 100).rounded())

        miCurrent.title  = "Current: \(s.currentKelvin) K  ·  \(currentBrtPct)%"
        miPhase.title    = phaseLabel(s.phase)
        miPhase.image    = symbolImage(phaseSymbol(s.phase))

        miDay.title      = "Day:     \(s.dayKelvin) K  ·  \(dayBrtPct)%"
        miDay.image      = symbolImage("sun.max")
        miNight.title    = "Night:   \(s.nightKelvin) K  ·  \(nightBrtPct)%"
        miNight.image    = symbolImage("moon")

        let bedBrtPct = Int((s.bedtimeBrightness * 100).rounded())
        miBedtime.title    = "Bedtime: \(s.bedtimeKelvin) K  ·  \(bedBrtPct)%"
        miBedtime.image    = symbolImage("bed.double")
        miBedtime.isHidden = !s.bedtimeEnabled

        miSchedule.title = scheduleLabel(s)
        miEnabled.state  = s.enabled ? .on : .off
    }

    private func phaseSymbol(_ p: SchedulePhase) -> String {
        switch p {
        case .day:               return "sun.max"
        case .night:             return "moon"
        case .transitionToNight: return "moon"        // heading to night
        case .transitionToDay:   return "sun.max"     // heading to day
        case .rampToBedtime:     return "bed.double"  // heading to bedtime
        case .bedtime:           return "bed.double"
        }
    }

    private func symbolImage(_ name: String) -> NSImage? {
        let cfg = NSImage.SymbolConfiguration(pointSize: 14, weight: .regular)
        let img = NSImage(systemSymbolName: name, accessibilityDescription: nil)?
            .withSymbolConfiguration(cfg)
        img?.isTemplate = true
        return img
    }

    private func phaseLabel(_ p: SchedulePhase) -> String {
        switch p {
        case .day:               return "Daytime"
        case .night:             return "Nighttime"
        case .transitionToNight: return "Transitioning to Night"
        case .transitionToDay:   return "Transitioning to Day"
        case .rampToBedtime:     return "Ramping to Bedtime"
        case .bedtime:           return "Bedtime"
        }
    }

    private func scheduleLabel(_ s: ScheduleState) -> String {
        let set = Settings.shared
        if set.scheduleMode == "solar" {
            let f = DateFormatter(); f.timeStyle = .short
            let r = s.sunriseTime.map { f.string(from: $0) } ?? "–"
            let t = s.sunsetTime.map  { f.string(from: $0) } ?? "–"
            let bed = set.bedtimeEnabled ? "  ·  Bed \(set.bedtimeTimeLabel)" : ""
            return "Schedule: Solar  ↑\(r)  ↓\(t)\(bed)"
        }
        let bed = set.bedtimeEnabled ? "  ·  Bed \(set.bedtimeTimeLabel)" : ""
        return "Schedule: \(set.dayTimeLabel) – \(set.nightTimeLabel)\(bed)"
    }

    // MARK: – Actions

    @objc private func toggleEnabled() {
        Settings.shared.enabled.toggle()
    }

    @objc private func openPrefs() {
        if prefsWC == nil { prefsWC = PreferencesWindowController() }
        prefsWC?.showWindow(nil)
        prefsWC?.window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc private func doQuit() {
        engine.stop()
        NSApp.terminate(nil)
    }
}
