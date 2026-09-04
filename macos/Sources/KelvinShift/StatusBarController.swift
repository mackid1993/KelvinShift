// KelvinShift – StatusBarController.swift

import AppKit

final class StatusBarController {

    private let statusItem: NSStatusItem
    private let engine: ScheduleEngine
    private var prefsWC: PreferencesWindowController?
    private var lastRenderedKey: String?

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

        // Stable identity for the life of the process. macOS 26/27 keys menu bar
        // position on this, and menu bar managers (Bartender, Thaw) key their own
        // records on it in turn. It is set once and never changed. The readout is drawn
        // into the image instead of the button title, because a title that changes with
        // temperature re-derives the item's identity on every ramp step and makes the
        // icon — and its neighbours — jump around.
        statusItem.autosaveName = Self.autosaveName
        statusItem.behavior = []
        // Set once. Re-assigning length on every tick makes AppKit re-lay-out the item,
        // and an item with no remembered position gets re-placed when that happens —
        // which is what was relocating the icon and sliding its neighbours sideways.
        statusItem.length = Self.canvasSize.width

        if let button = statusItem.button {
            button.title = ""
            button.imagePosition = .imageOnly
            button.imageScaling = .scaleNone
            button.setAccessibilityIdentifier(Self.autosaveName)
            button.setAccessibilityLabel("KelvinShift")
        }

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
        // Only the image and the accessibility *value* change. title, autosaveName,
        // accessibilityIdentifier and accessibilityLabel stay exactly as set at creation.
        if let btn = statusItem.button {
            let symbol = s.enabled ? phaseSymbol(s.phase) : "power.circle"
            let text = s.enabled ? "\(s.currentKelvin)K" : "Off"

            // Touch the button only when the readout actually changes. The schedule
            // ticks every 15 seconds and usually renders the same thing; assigning an
            // identical image each time still triggers a re-layout, and a re-layout
            // moves an item that has no remembered position.
            let key = "\(symbol)|\(text)"
            if key != lastRenderedKey {
                lastRenderedKey = key
                btn.image = Self.renderReadout(symbol: symbol, text: text)
                btn.setAccessibilityValue(s.enabled ? "\(s.currentKelvin) kelvin" : "Off")
            }
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

    /// Unique and permanent. Never derive this from anything that changes.
    private static let autosaveName = "KelvinShift"

    /// Every symbol the button can ever show. The canvas is sized for the widest of
    /// them so swapping glyphs cannot change the item's footprint.
    private static let allSymbols = ["sun.max", "moon", "bed.double", "power.circle"]

    private static let symbolConfig = NSImage.SymbolConfiguration(pointSize: 14, weight: .regular)
    private static let readoutFont = NSFont.monospacedDigitSystemFont(ofSize: 13, weight: .regular)
    private static let glyphGap: CGFloat = 3

    private static func glyph(_ name: String) -> NSImage? {
        NSImage(systemSymbolName: name, accessibilityDescription: nil)?
            .withSymbolConfiguration(symbolConfig)
    }

    /// One fixed canvas for the life of the process. A glyph and a reading that each
    /// change width would resize the status item on every phase change and ramp step,
    /// shoving every icon to its left. Sizing for the worst case makes the footprint
    /// constant, so the item never moves and never moves anything else.
    private static let canvasSize: NSSize = {
        let widestGlyph = allSymbols.compactMap { glyph($0)?.size.width }.max() ?? 0
        let widestText = ("8888K" as NSString)
            .size(withAttributes: [.font: readoutFont]).width
        return NSSize(
            width: ceil(widestGlyph + glyphGap + widestText),
            height: NSStatusBar.system.thickness
        )
    }()

    /// Draws the phase symbol and the reading into one template image on the fixed
    /// canvas, so the button carries no title and never changes size.
    private static func renderReadout(symbol: String, text: String) -> NSImage {
        let attributed = NSAttributedString(
            string: text,
            attributes: [.font: readoutFont, .foregroundColor: NSColor.black]
        )
        let textSize = attributed.size()
        let mark = glyph(symbol)
        let markSize = mark?.size ?? .zero

        // Glyph pinned left, reading right-aligned in the remaining space, so a shorter
        // reading ("Off") does not slide the glyph or resize the canvas.
        let image = NSImage(size: canvasSize)
        image.lockFocus()
        mark?.draw(
            in: NSRect(
                x: 0,
                y: ((canvasSize.height - markSize.height) / 2).rounded(),
                width: markSize.width,
                height: markSize.height
            )
        )
        attributed.draw(
            at: NSPoint(
                x: canvasSize.width - textSize.width,
                y: ((canvasSize.height - textSize.height) / 2).rounded()
            )
        )
        image.unlockFocus()
        image.isTemplate = true
        return image
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
