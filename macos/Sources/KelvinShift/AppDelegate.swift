// KelvinShift – AppDelegate.swift

import AppKit

class AppDelegate: NSObject, NSApplicationDelegate {
    private var engine: ScheduleEngine?
    private var statusBar: StatusBarController?

    func applicationDidFinishLaunching(_ notification: Notification) {
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
