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
    private var miSchedule: NSMenuItem!
    private var miEnabled:  NSMenuItem!

    init(engine: ScheduleEngine) {
        self.engine = engine
        self.statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
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
                btn.title = "○ Off"
            } else {
                let icon: String = {
                    switch s.phase {
                    case .day:               return "☀"
                    case .night:             return "☾"
                    case .transitionToNight: return "☀→☾"
                    case .transitionToDay:   return "☾→☀"
                    }
                }()
                btn.title = "\(icon) \(s.currentKelvin)K"
            }
            btn.font = NSFont.monospacedDigitSystemFont(ofSize: 0, weight: .regular)
        }

        // ── Drop-down items ────────────────────────────
        miCurrent.title  = "Current: \(s.currentKelvin) K"
        miPhase.title    = phaseLabel(s.phase)
        miDay.title      = "☀  Day:   \(s.dayKelvin) K"
        miNight.title    = "☾  Night: \(s.nightKelvin) K"
        miSchedule.title = scheduleLabel(s)
        miEnabled.state  = s.enabled ? .on : .off
    }

    private func phaseLabel(_ p: SchedulePhase) -> String {
        switch p {
        case .day:               return "☀  Daytime"
        case .night:             return "☾  Nighttime"
        case .transitionToNight: return "☀→☾  Transitioning to Night"
        case .transitionToDay:   return "☾→☀  Transitioning to Day"
        }
    }

    private func scheduleLabel(_ s: ScheduleState) -> String {
        let set = Settings.shared
        if set.scheduleMode == "solar" {
            let f = DateFormatter(); f.timeStyle = .short
            let r = s.sunriseTime.map { f.string(from: $0) } ?? "–"
            let t = s.sunsetTime.map  { f.string(from: $0) } ?? "–"
            return "Schedule: Solar  ↑\(r)  ↓\(t)"
        }
        return "Schedule: \(set.dayTimeLabel) – \(set.nightTimeLabel)"
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
