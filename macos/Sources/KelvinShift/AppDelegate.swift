// KelvinShift – AppDelegate.swift

import AppKit

class AppDelegate: NSObject, NSApplicationDelegate {
    private var engine: ScheduleEngine?
    private var statusBar: StatusBarController?

    /// Menu bar managers can only track an item whose owner is a real, identifiable
    /// app bundle. Refuse to create status items from a stray executable, a copy run
    /// out of the build directory, or a second instance racing the first.
    private func validateBundleIdentity() -> Bool {
        let bundle = Bundle.main
        guard bundle.bundleURL.pathExtension == "app",
              bundle.bundleIdentifier == "com.kelvinshift.app",
              bundle.executableURL?.lastPathComponent == "KelvinShift"
        else {
            return false
        }
        let others = NSRunningApplication
            .runningApplications(withBundleIdentifier: "com.kelvinshift.app")
            .filter { $0 != .current }
        return others.isEmpty
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        guard validateBundleIdentity() else {
            NSApp.terminate(nil)
            return
        }

        // Hide from Dock — menu bar only
        NSApp.setActivationPolicy(.accessory)

        let engine = ScheduleEngine()
        self.engine = engine
        self.statusBar = StatusBarController(engine: engine)
        engine.start()
    }

    func applicationWillTerminate(_ notification: Notification) {
        engine?.stop()
    }
}
