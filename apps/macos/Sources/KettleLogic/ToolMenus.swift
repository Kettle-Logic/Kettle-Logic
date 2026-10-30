import SwiftUI

/// The Tools and Unit Tools menus. Each item opens its tool in its own window.
struct ToolMenus: Commands {
    @Environment(\.openWindow) private var openWindow

    var body: some Commands {
        CommandMenu("Tools") {
            ForEach(ToolKind.calculators) { kind in
                Button(kind.spec.title) { openWindow(value: kind) }
            }
        }

        CommandMenu("Unit Tools") {
            ForEach(ToolKind.converters) { kind in
                Button(kind.spec.title) { openWindow(value: kind) }
            }
        }
    }
}
