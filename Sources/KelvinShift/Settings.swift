// KelvinShift – Settings.swift

import Foundation
import Combine
import ServiceManagement

final class Settings: ObservableObject {
    static let shared = Settings()
    static let didChange = Notification.Name("KelvinShiftSettingsChanged")

    private let d = UserDefaults.standard

    // ── Color Temperature ──────────────────────────────────

    @Published var dayKelvin: Int {
        didSet { let v = clamp(dayKelvin, 2000, 6500); if dayKelvin != v { dayKelvin = v; return }; save("ks_dayK", v) }
    }
    @Published var nightKelvin: Int {
        didSet { let v = clamp(nightKelvin, 1800, 5500); if nightKelvin != v { nightKelvin = v; return }; save("ks_nightK", v) }
    }

    // ── Brightness ────────────────────────────────────────

    @Published var dayBrightness: Double {
        didSet { let v = clampD(dayBrightness, 0.1, 1.0); if dayBrightness != v { dayBrightness = v; return }; save("ks_dayBrt", v) }
    }
    @Published var nightBrightness: Double {
        didSet { let v = clampD(nightBrightness, 0.1, 1.0); if nightBrightness != v { nightBrightness = v; return }; save("ks_nightBrt", v) }
    }

    // ── Schedule ───────────────────────────────────────────

    @Published var scheduleMode: String {
        didSet { save("ks_schedMode", scheduleMode) }
    }
    @Published var customDayHour: Int {
        didSet { save("ks_cdH", customDayHour) }
    }
    @Published var customDayMinute: Int {
        didSet { save("ks_cdM", customDayMinute) }
    }
    @Published var customNightHour: Int {
        didSet { save("ks_cnH", customNightHour) }
    }
    @Published var customNightMinute: Int {
        didSet { save("ks_cnM", customNightMinute) }
    }

    // ── Location (for solar schedule) ──────────────────────

    @Published var latitude: Double {
        didSet { save("ks_lat", latitude) }
    }
    @Published var longitude: Double {
        didSet { save("ks_lon", longitude) }
    }
    @Published var locationName: String {
        didSet { save("ks_locName", locationName) }
    }

    // ── Transition ─────────────────────────────────────────

    @Published var transitionMinutes: Int {
        didSet { let v = max(transitionMinutes, 1); if transitionMinutes != v { transitionMinutes = v; return }; save("ks_transMins", v) }
    }

    // ── Master toggle ──────────────────────────────────────

    @Published var enabled: Bool {
        didSet { save("ks_enabled", enabled) }
    }

    // ── Launch at Login ────────────────────────────────────

    @Published var launchAtLogin: Bool {
        didSet {
            d.set(launchAtLogin, forKey: "ks_launchAtLogin")
            applyLoginItem()
        }
    }

    /// Whether the OS actually supports SMAppService (macOS 13+).
    let loginItemSupported: Bool

    // ── Init ───────────────────────────────────────────────

    private init() {
        // Check OS support first (used to gate UI and init logic)
        if #available(macOS 13.0, *) {
            loginItemSupported = true
        } else {
            loginItemSupported = false
        }

        dayKelvin         = d.object(forKey: "ks_dayK")       as? Int    ?? 5000
        nightKelvin       = d.object(forKey: "ks_nightK")     as? Int    ?? 2700
        dayBrightness     = d.object(forKey: "ks_dayBrt")     as? Double ?? 1.0
        nightBrightness   = d.object(forKey: "ks_nightBrt")   as? Double ?? 0.8
        scheduleMode      = d.string(forKey: "ks_schedMode")             ?? "custom"
        customDayHour     = d.object(forKey: "ks_cdH")        as? Int    ?? 7
        customDayMinute   = d.object(forKey: "ks_cdM")        as? Int    ?? 0
        customNightHour   = d.object(forKey: "ks_cnH")        as? Int    ?? 20
        customNightMinute = d.object(forKey: "ks_cnM")        as? Int    ?? 0
        latitude          = d.object(forKey: "ks_lat")        as? Double ?? 0.0
        longitude         = d.object(forKey: "ks_lon")        as? Double ?? 0.0
        locationName      = d.string(forKey: "ks_locName")               ?? ""
        transitionMinutes = d.object(forKey: "ks_transMins")  as? Int    ?? 20
        enabled           = d.object(forKey: "ks_enabled")    as? Bool   ?? true

        // Sync launchAtLogin: prefer user's saved preference, re-register if needed
        if #available(macOS 13.0, *) {
            let savedPref = d.object(forKey: "ks_launchAtLogin") as? Bool
            let systemState = SMAppService.mainApp.status == .enabled

            if let pref = savedPref {
                // User has a saved preference — use it and ensure system matches
                launchAtLogin = pref
                if pref && !systemState {
                    // Plist was deleted or login item was removed — re-register
                    try? SMAppService.mainApp.register()
                } else if !pref && systemState {
                    // Somehow enabled when user wanted it off — unregister
                    try? SMAppService.mainApp.unregister()
                }
            } else {
                // No saved preference — sync from system state
                launchAtLogin = systemState
            }
        } else {
            launchAtLogin = d.object(forKey: "ks_launchAtLogin") as? Bool ?? false
        }
    }

    // ── Launch at Login ────────────────────────────────────

    private func applyLoginItem() {
        if #available(macOS 13.0, *) {
            do {
                if launchAtLogin {
                    try SMAppService.mainApp.register()
                } else {
                    try SMAppService.mainApp.unregister()
                }
            } catch {
                NSLog("[KelvinShift] Login item error: \(error)")
            }
        }
    }

    // ── Helpers ────────────────────────────────────────────

    private func save(_ key: String, _ val: Any) {
        d.set(val, forKey: key)
        NotificationCenter.default.post(name: Settings.didChange, object: nil)
    }

    private func clamp(_ v: Int, _ lo: Int, _ hi: Int) -> Int { min(max(v, lo), hi) }
    private func clampD(_ v: Double, _ lo: Double, _ hi: Double) -> Double { min(max(v, lo), hi) }

    var dayTimeLabel: String   { formatTime(customDayHour, customDayMinute) }
    var nightTimeLabel: String { formatTime(customNightHour, customNightMinute) }

    private func formatTime(_ h: Int, _ m: Int) -> String {
        let h12 = h == 0 ? 12 : (h > 12 ? h - 12 : h)
        let sfx = h >= 12 ? "PM" : "AM"
        return String(format: "%d:%02d %@", h12, m, sfx)
    }
}
