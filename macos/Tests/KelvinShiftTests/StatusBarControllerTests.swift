import AppKit
import XCTest
@testable import KelvinShift

@MainActor
final class StatusBarControllerTests: XCTestCase {
    func testPreparedStatusItemHasPermanentIdentityBeforeVisibility() throws {
        let item = StatusBarController.makeStatusItem()
        defer { NSStatusBar.system.removeStatusItem(item) }

        let button = try XCTUnwrap(item.button)
        XCTAssertFalse(item.isVisible)
        XCTAssertEqual(item.autosaveName, "KelvinShift")
        XCTAssertEqual(button.window?.title, "KelvinShift")
        XCTAssertEqual(button.accessibilityIdentifier(), "KelvinShift")
        XCTAssertEqual(button.accessibilityLabel(), "KelvinShift")
        XCTAssertEqual(button.title, "")
    }

    func testDynamicReadoutImageRetainsPermanentAccessibilityLabel() {
        for (symbol, text) in [("sun.max", "6500K"), ("moon", "2700K"), ("power.circle", "Off")] {
            let image = StatusBarController.renderReadout(symbol: symbol, text: text)
            XCTAssertEqual(image.accessibilityDescription, "KelvinShift")
        }
    }
}
