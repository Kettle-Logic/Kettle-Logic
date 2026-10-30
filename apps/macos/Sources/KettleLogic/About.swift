import AppKit
import CKettleLogic

/// The About panel, from the app menu. The panel shows the app's version from the bundle, and the credits line
/// shows the engine's version, which the engine reports.
@MainActor
enum About {
    static func show() {
        let engine = String(cString: kl_engine_version())
        let credits = NSAttributedString(
            string: "Engine version \(engine)",
            attributes: [.font: NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)]
        )
        NSApplication.shared.orderFrontStandardAboutPanel(options: [.credits: credits])
    }
}
