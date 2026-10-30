import SwiftUI

/// The Help menu's item for the app's documentation. It opens the Help window.
struct HelpMenu: Commands {
    @Environment(\.openWindow) private var openWindow

    var body: some Commands {
        CommandGroup(replacing: .help) {
            Button("Kettle Logic Help") { openWindow(id: "help") }
                .keyboardShortcut("?")
        }
    }
}
