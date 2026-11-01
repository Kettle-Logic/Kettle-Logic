import AppKit
import SwiftUI

@main
struct KettleLogicApp: App {
    @State private var store: DocumentStore
    @State private var waterProfiles: WaterProfileLibrary
    @State private var updates = UpdateCenter()
    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true

    init() {
        // The engine loads first, before anything calls it: the defaults below read the built-in equipment, and the
        // store and water profiles read the built-in data. Property initializers run before this body, so none of
        // those can be one.
        if let message = EngineLoader.load() {
            Self.engineFailed(message)
        }

        // A plain executable, started from a shell, is not a regular app. Without this
        // it has no menu bar and does not take keyboard focus.
        NSApplication.shared.setActivationPolicy(.regular)
        UserDefaults.standard.register(defaults: Preferences.defaults)
        Appearance.apply()

        _store = State(initialValue: DocumentStore())
        _waterProfiles = State(initialValue: WaterProfileLibrary())
    }

    var body: some Scene {
        WindowGroup {
            ContentView(store: store)
                .environment(waterProfiles)
                .environment(updates)
                .sheet(isPresented: $updates.isPresented) {
                    UpdateView(center: updates)
                }
                .onAppear { NSApplication.shared.activate() }
                .task {
                    // A first launch, or an upgrade that has not been told about telemetry, starts it once the user has
                    // answered the sheet that asks.
                    if UserDefaults.standard.bool(forKey: Preferences.interfaceChosenKey),
                       UserDefaults.standard.bool(forKey: Preferences.telemetryNoticeShownKey) {
                        Analytics.start()
                    }
                    if UserDefaults.standard.bool(forKey: Preferences.checkForUpdatesKey) {
                        await updates.check(automatically: true)
                    }
                }
        }
        .defaultSize(width: 1280, height: 800)
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button("About Kettle Logic") { About.show() }
            }

            CommandGroup(after: .appInfo) {
                Button("Check for Updates…") {
                    Task { await updates.check(automatically: false) }
                }
            }

            CommandGroup(replacing: .newItem) {
                Button("New Recipe") { store.newRecipe() }
                    .keyboardShortcut("n")
                Button("New Recipe from a Style…") { store.showsWizard = true }
                    .keyboardShortcut("n", modifiers: [.command, .shift])
                Button("Open…") { store.open() }
                    .keyboardShortcut("o")
            }

            CommandGroup(replacing: .saveItem) {
                Button("Save") { store.save() }
                    .keyboardShortcut("s")
                    .disabled(store.document == nil)
                Button("Save As…") { store.saveAs() }
                    .keyboardShortcut("s", modifiers: [.command, .shift])
                    .disabled(store.document == nil)
            }

            // Undo and redo use the engine's command stack, and its labels.
            CommandGroup(replacing: .undoRedo) {
                Button(undoTitle) { store.undo() }
                    .keyboardShortcut("z")
                    .disabled(!(store.document?.canUndo ?? false))
                Button(redoTitle) { store.redo() }
                    .keyboardShortcut("z", modifiers: [.command, .shift])
                    .disabled(!(store.document?.canRedo ?? false))
            }

            CommandGroup(after: .toolbar) {
                Toggle("Simple Mode", isOn: $simpleMode)
                    .keyboardShortcut("m", modifiers: [.command, .option])
            }

            ToolMenus()
            HelpMenu()
        }

        // The user guide, from the Help menu. It opens in its own window.
        Window("Kettle Logic Help", id: "help") {
            HelpView()
        }

        // A brewing tool or unit converter, from the Tools and Unit Tools menus. Each opens its own window.
        WindowGroup("Tool", id: "tool", for: ToolKind.self) { $kind in
            if let kind {
                ToolView(kind: kind)
                    .environment(waterProfiles)
            }
        }

        // A brew-day checklist, opened from a generated procedure. Each procedure opens its own window.
        WindowGroup("Brew Day", id: "brew-day", for: BrewDayPlan.self) { $plan in
            if let plan = $plan.wrappedValue {
                BrewDayView(plan: plan)
            }
        }

        // Shown from the app menu with Cmd+, .
        Settings {
            SettingsView()
                .environment(store.equipment)
                .environment(updates)
        }
    }

    /// The app cannot run without an engine, so this says why and quits.
    private static func engineFailed(_ message: String) -> Never {
        let alert = NSAlert()
        alert.messageText = "Kettle Logic cannot start"
        alert.informativeText = message
        alert.runModal()
        exit(1)
    }

    private var undoTitle: String {
        guard let label = store.document?.undoLabel, !label.isEmpty else { return "Undo" }
        return "Undo \(label)"
    }

    private var redoTitle: String {
        guard let label = store.document?.redoLabel, !label.isEmpty else { return "Redo" }
        return "Redo \(label)"
    }
}
