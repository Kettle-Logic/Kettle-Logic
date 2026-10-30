import AppKit
import SwiftUI

/// A brewing system, with typical starting figures. The batch size, efficiency, and
/// boil-off depend on the kettle, the grain bed, and how hard the boil runs, so
/// measure your own system and change the recipe's figures to match.
struct EquipmentProfile: Identifiable, Codable {
    var name: String
    var batchLiters: String
    var efficiencyPercent: String
    var evaporationPercent: String
    var trubLossLiters: String
    var boilMinutes: Int
    /// "separate tun" or "all in one", as the engine names the mash system.
    var mashSystem: String
    var note: String

    var id: String { name }

    /// The starting figures on one line, with the batch in the unit system the user chose.
    func summary(in units: UnitSystem) -> String {
        "\(units.volumeText(liters: batchLiters)) \(units.volumeSymbol) batch · \(efficiencyPercent)% efficiency · "
            + "\(evaporationPercent)% boil-off per hour · \(mashSystem)"
    }
}

enum Equipment {
    /// The engine's built-in brewing systems. The first is the default.
    static let builtIn: [EquipmentProfile] = BuiltinData.load("equipment")
}

/// The brewing systems in the ingredient directory. The user's own systems can be made, changed,
/// duplicated, and deleted. A built-in system is copied to make a change to it. Each one shows its
/// starting figures and the note on where they come from.
struct EquipmentDirectoryView: View {
    let store: DocumentStore
    @State private var search = ""
    @State private var request: EquipmentRequest?

    var body: some View {
        let custom = store.equipment.custom.filter(matches)
        let builtIn = Equipment.builtIn.filter(matches)

        List {
            if !custom.isEmpty {
                Section("Your Equipment") {
                    ForEach(custom) { profile in
                        Button {
                            request = EquipmentRequest(
                                title: "Edit Brewing System", profile: profile, replacing: profile.name)
                        } label: {
                            EquipmentRow(profile: profile)
                        }
                        .buttonStyle(.plain)
                        .contextMenu {
                            Button("Edit") {
                                request = EquipmentRequest(
                                    title: "Edit Brewing System", profile: profile, replacing: profile.name)
                            }
                            Button("Duplicate") {
                                store.attempt { try store.equipment.duplicate(profile.name) }
                            }
                            Button("Delete", role: .destructive) {
                                store.attempt { try store.equipment.remove(profile.name) }
                            }
                        }
                    }
                }
            }

            if !builtIn.isEmpty {
                Section("Built-in") {
                    ForEach(builtIn) { profile in
                        Button {
                            request = EquipmentRequest(
                                title: "Save as Brewing System", profile: copy(of: profile), replacing: nil)
                        } label: {
                            EquipmentRow(profile: profile)
                        }
                        .buttonStyle(.plain)
                        .contextMenu {
                            Button("Save a Copy…") {
                                request = EquipmentRequest(
                                    title: "Save as Brewing System", profile: copy(of: profile), replacing: nil)
                            }
                        }
                    }
                }
            }
        }
        .overlay {
            if custom.isEmpty && builtIn.isEmpty {
                ContentUnavailableView.search(text: search)
            }
        }
        .toolbar {
            ToolbarItem {
                Button("New Brewing System", systemImage: "plus") {
                    request = EquipmentRequest(
                        title: "New Brewing System", profile: blank, replacing: nil)
                }
                .help("Make your own brewing system")
            }
        }
        .searchable(text: $search, prompt: "Search brewing systems")
        .navigationTitle(LibraryPage.equipment.title)
        .sheet(item: $request) { request in
            EquipmentEditor(request: request, library: store.equipment)
        }
    }

    private func matches(_ profile: EquipmentProfile) -> Bool {
        search.isEmpty || profile.name.localizedCaseInsensitiveContains(search)
    }

    /// A built-in system's figures, under a name that says it is a copy.
    private func copy(of profile: EquipmentProfile) -> EquipmentProfile {
        var copy = profile
        copy.name = "\(profile.name) copy"
        return copy
    }

    /// A new system, starting from the figures of the first built-in one.
    private var blank: EquipmentProfile {
        var profile = Equipment.builtIn[0]
        profile.name = ""
        profile.note = ""
        return profile
    }
}

/// A brewing system's name, with its starting figures and its note under it.
struct EquipmentRow: View {
    let profile: EquipmentProfile
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(profile.name)
            Text(profile.summary(in: units))
                .font(.caption)
                .foregroundStyle(.secondary)
                .monospacedDigit()
            if !profile.note.isEmpty {
                Text(profile.note)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 2)
    }
}

/// The editor to open on the equipment page. `replacing` is the custom system the editor changes,
/// or nil to save a new one.
struct EquipmentRequest: Identifiable {
    let id = UUID()
    let title: String
    let profile: EquipmentProfile
    let replacing: String?
}

/// Generates a brew-day procedure from the recipe, as sections of the recipe's form.
/// The user gives the boil length, and the brewing system sets the mash steps. The
/// additions are placed by the times on each ingredient, counted back from the end of the boil.
struct ProcedureSection: View {
    let document: RecipeDocument
    let recipe: UInt64
    /// The brewing system chosen in the recipe's equipment section.
    let profile: EquipmentProfile

    @State private var boil: String
    @State private var steps: [ProcedureStep]?
    @State private var errorMessage: String?
    /// The boil length the current steps were generated with.
    @State private var generatedMinutes: Int?
    @Environment(\.openWindow) private var openWindow

    init(document: RecipeDocument, recipe: UInt64, profile: EquipmentProfile) {
        self.document = document
        self.recipe = recipe
        self.profile = profile
        _boil = State(initialValue: String(profile.boilMinutes))
    }

    var body: some View {
        Group {
            Section {
                LabeledContent("Brewing system", value: profile.name)
                LabeledContent("Boil length (minutes)") {
                    NumberField(title: "Minutes", text: $boil)
                }
                Button("Generate Procedure", action: generate)
            } header: {
                Text("Brew-Day Procedure")
            } footer: {
                Text("Each addition is placed by the time set on its ingredient, counted back from the end of "
                    + "this boil. An addition timed longer than the boil is flagged in red.")
            }

            if let errorMessage {
                Section {
                    Text(errorMessage).foregroundStyle(.red)
                }
            }

            if let steps {
                Section {
                    ForEach(Array(steps.enumerated()), id: \.offset) { _, step in
                        ProcedureStepRow(step: step)
                    }
                    Button("Copy Procedure", action: copy)
                    Button("Open Brew-Day View", action: openBrewDay)
                } header: {
                    Text("Procedure")
                }
            }
        }
        .onChange(of: profile.name) { boil = String(profile.boilMinutes) }
        .onChange(of: recipe) {
            steps = nil
            errorMessage = nil
        }
    }

    private func generate() {
        guard let minutes = Int(trimmed(boil)), (1...600).contains(minutes) else {
            steps = nil
            errorMessage = "Enter a boil length from 1 to 600 minutes."
            return
        }
        do {
            steps = try document.procedure(recipe, boilMinutes: minutes, system: profile.mashSystem)
            generatedMinutes = minutes
            errorMessage = nil
        } catch {
            steps = nil
            errorMessage = error.localizedDescription
        }
    }

    /// Opens the generated procedure in its own window, as a large checklist for the brew day.
    private func openBrewDay() {
        guard let steps, let generatedMinutes else { return }
        let name = document.snapshot.recipes.first { $0.id == recipe }?.name ?? "Recipe"
        openWindow(id: "brew-day", value: BrewDayPlan(
            id: UUID(), recipeName: name, boilMinutes: generatedMinutes, steps: steps))
    }

    private func copy() {
        guard let steps else { return }
        let text = steps.map { step in
            let when = step.minute.map { " · \($0) min" } ?? ""
            return "\(step.phase)\(when): \(step.text)"
        }
        .joined(separator: "\n")
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}

/// One step of the procedure: its phase and, for the boil, its minute, then the text.
struct ProcedureStepRow: View {
    let step: ProcedureStep

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(step.minute.map { "\(step.phase) · \($0) min" } ?? step.phase)
                .font(.caption)
                .foregroundStyle(.secondary)
                .monospacedDigit()
            Text(step.text)
                .foregroundStyle(step.warning ? Color.red : Color.primary)
        }
    }
}
