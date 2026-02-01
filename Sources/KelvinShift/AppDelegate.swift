// KelvinShift – AppDelegate.swift

import AppKit

class AppDelegate: NSObject, NSApplicationDelegate {
    private var bridge: NightShiftBridge?
    private var engine: ScheduleEngine?
    private var statusBar: StatusBarController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Hide from Dock — menu bar only
        NSApp.setActivationPolicy(.accessory)

        guard let bridge = NightShiftBridge() else {
            let alert = NSAlert()
            alert.messageText = "KelvinShift Error"
            alert.informativeText = """
                Could not load the CoreBrightness framework. \
                Night Shift may not be supported on this Mac, \
                or macOS has restricted access to the private API.
                """
            alert.alertStyle = .critical
            alert.addButton(withTitle: "Quit")
            alert.runModal()
            NSApp.terminate(nil)
            return
        }

        self.bridge = bridge
        let engine = ScheduleEngine(bridge: bridge)
        self.engine = engine
        self.statusBar = StatusBarController(engine: engine)
        engine.start()
    }

    func applicationWillTerminate(_ notification: Notification) {
        engine?.stop()
    }
}
