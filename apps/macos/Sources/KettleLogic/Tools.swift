import CKettleLogic
import Foundation
import SwiftUI

// The brewing tools and unit converters in the Tools and Unit Tools menus. The engine describes
// each one's inputs and results and does the arithmetic, and ToolView shows the form.

/// A tool's results: named numbers, and the notes that go with them.
struct ToolOutput: Decodable {
    let results: [String: Double]
    let notes: [String]
}

/// Runs a tool or converter in the engine. The inputs are the text the user typed, and the engine reads each
/// one exactly. The engine's names and inputs are listed in docs/calc.md, "Tools".
enum ToolEngine {
    static func run(_ tool: String, _ inputs: [String: String]) throws -> ToolOutput {
        let json = String(decoding: try JSONEncoder().encode(inputs), as: UTF8.self)
        var error: UnsafeMutablePointer<CChar>?
        guard let text = kl_tool_json(tool, json, &error) else {
            let message = error.map { String(cString: $0) } ?? "The tool could not run."
            kl_string_free(error)
            throw EngineError(message: message)
        }
        defer { kl_string_free(text) }
        return try JSONDecoder().decode(ToolOutput.self, from: Data(String(cString: text).utf8))
    }
}

/// One input on a tool's form.
struct ToolField: Identifiable, Decodable {
    enum Kind {
        case number
        case toggle
        case choice([Choice])
    }

    struct Choice: Hashable, Decodable {
        let value: String
        let label: String
    }

    let key: String
    let label: String
    let kind: Kind
    let defaultValue: String
    /// The toggle that must be on for this field to show and to count as an input, or nil to always show it.
    let enabledBy: String?

    var id: String { key }

    private enum CodingKeys: String, CodingKey { case key, label, kind, defaultValue, choices, enabledBy }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        key = try c.decode(String.self, forKey: .key)
        label = try c.decode(String.self, forKey: .label)
        defaultValue = try c.decode(String.self, forKey: .defaultValue)
        enabledBy = try c.decodeIfPresent(String.self, forKey: .enabledBy)
        switch try c.decode(String.self, forKey: .kind) {
        case "number": kind = .number
        case "toggle": kind = .toggle
        case "choice": kind = .choice(try c.decode([Choice].self, forKey: .choices))
        case let other:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: c, debugDescription: "Unknown tool field kind \(other).")
        }
    }
}

/// One result on a tool's form, with the unit it is shown in.
struct ToolReadout: Identifiable, Decodable {
    let key: String
    let label: String
    let unit: String
    /// The toggle that must be on for this readout to show, or nil to always show it.
    let enabledBy: String?

    var id: String { key }
}

/// A group of inputs that runs one engine tool, with its results and a note under them.
struct ToolSection: Identifiable, Decodable {
    let title: String
    let footer: String?
    let tool: String
    let fields: [ToolField]
    let readouts: [ToolReadout]

    var id: String { tool + title }
}

/// A tool window: its sections, in order.
struct ToolSpec: Decodable {
    let title: String
    let sections: [ToolSection]
}

// MARK: Catalog

/// The tool forms the engine describes (`core/src/catalog/tool_specs.cpp`): which tools there are, in menu order,
/// and each one's sections, fields, defaults, and notes.
private struct ToolCatalog: Decodable {
    let calculators: [String]
    let converters: [String]
    let specs: [String: ToolSpec]
}

private let toolCatalog = BuiltinData.load("tools", as: ToolCatalog.self)

/// The windows that the Tools and Unit Tools menus open. The raw value is the engine's name for the tool, and it
/// is stable, so a window can be restored.
enum ToolKind: String, CaseIterable, Identifiable, Codable, Hashable {
    // Tools
    case waterProfile, infusion, decoction, mashAdjustment, mashPh, spargeAcid, yeastStarter, hydrometer, refractometer
    case alcohol, hopBitterness, hopAge, meadNutrient, backsweetening, sulfiting, boilOff, dilution
    case gravityAdjustment, weightVolume, carbonation
    // Unit tools
    case temperature, gravityUnits, pressure, weight, volume

    var id: Self { self }

    static let calculators: [ToolKind] = toolCatalog.calculators.compactMap { ToolKind(rawValue: $0) }

    static let converters: [ToolKind] = toolCatalog.converters.compactMap { ToolKind(rawValue: $0) }

    var spec: ToolSpec {
        guard let spec = toolCatalog.specs[rawValue] else { fatalError("The engine has no form for the \(rawValue) tool.") }
        return spec
    }
}

// MARK: Window

/// A tool window. The form runs the engine on every change, so the results follow the inputs without a save.
struct ToolView: View {
    let kind: ToolKind

    /// The text in each field, by its key. Fields that share a key in two sections share a value.
    @State private var values: [String: String] = [:]
    @State private var saveName = ""
    @State private var saveMessage: String?

    @Environment(WaterProfileLibrary.self) private var waterProfiles

    private var spec: ToolSpec { kind.spec }

    /// Starts the form with `prefill` already in its fields, by field key. A field not in it starts at its default.
    init(kind: ToolKind, prefill: [String: String] = [:]) {
        self.kind = kind
        _values = State(initialValue: prefill)
    }

    var body: some View {
        Form {
            ForEach(spec.sections) { section in
                Section {
                    ForEach(section.fields.filter { isOn($0.enabledBy) }) { field in
                        fieldView(field)
                    }
                    // A section with no readouts only supplies inputs to the results of its tool.
                    if !section.readouts.isEmpty {
                        resultsView(section)
                    }
                } header: {
                    Text(section.title)
                } footer: {
                    if let footer = section.footer {
                        Text(footer)
                    }
                }
            }
            if kind == .waterProfile {
                saveSection
            }
        }
        .formStyle(.grouped)
        .onAppear { Analytics.record(.featureUsed, ["feature": .word("tool_\(kind.rawValue)")]) }
        .textFieldStyle(.roundedBorder)
        .navigationTitle(spec.title)
    }

    // MARK: Pieces

    @ViewBuilder
    private func fieldView(_ field: ToolField) -> some View {
        switch field.kind {
        case .number:
            LabeledContent(field.label) {
                TextField("", text: binding(field))
                    .multilineTextAlignment(.trailing)
                    .frame(width: 110)
            }
        case .toggle:
            Toggle(field.label, isOn: Binding(
                get: { (values[field.key] ?? field.defaultValue) == "1" },
                set: { values[field.key] = $0 ? "1" : "0" }
            ))
        case .choice(let choices):
            Picker(field.label, selection: binding(field)) {
                ForEach(choices, id: \.value) { choice in
                    Text(choice.label).tag(choice.value)
                }
            }
        }
    }

    @ViewBuilder
    private func resultsView(_ section: ToolSection) -> some View {
        switch run(section) {
        case .success(let output):
            ForEach(section.readouts.filter { isOn($0.enabledBy) }) { readout in
                LabeledContent(readout.label) {
                    Text(displayValue(output.results[readout.key]) + (readout.unit.isEmpty ? "" : " \(readout.unit)"))
                        .monospacedDigit()
                }
            }
            ForEach(output.notes, id: \.self) { note in
                Text(note).font(.callout).foregroundStyle(.secondary)
            }
        case .failure(let error):
            Text(error.localizedDescription).foregroundStyle(.secondary)
        }
    }

    // MARK: Values

    private func binding(_ field: ToolField) -> Binding<String> {
        Binding(
            get: { values[field.key] ?? field.defaultValue },
            set: { values[field.key] = $0 }
        )
    }

    /// The text now in a field, by its key, with the field's default when it has not been changed.
    private func text(_ key: String) -> String {
        values[key] ?? spec.sections.flatMap(\.fields).first { $0.key == key }?.defaultValue ?? ""
    }

    /// Whether a field or readout shows: a toggle it names must be on. A field with no toggle always shows.
    private func isOn(_ toggle: String?) -> Bool {
        guard let toggle else { return true }
        return text(toggle) == "1"
    }

    /// The inputs a section's tool runs on. Sections of the same tool with no readouts supply inputs too, so
    /// the inputs can sit in several groups, one per step. Hidden fields are left out.
    private func inputs(for section: ToolSection) -> [String: String] {
        let supplying = spec.sections.filter { $0.tool == section.tool && $0.readouts.isEmpty }
        return inputs(in: supplying + [section])
    }

    /// The shown inputs of the given sections.
    private func inputs(in sections: [ToolSection]) -> [String: String] {
        var inputs: [String: String] = [:]
        for field in sections.flatMap(\.fields) where isOn(field.enabledBy) {
            inputs[field.key] = text(field.key)
        }
        return inputs
    }

    /// Runs one section's tool on its inputs.
    private func run(_ section: ToolSection) -> Result<ToolOutput, Error> {
        Result { try ToolEngine.run(section.tool, inputs(for: section)) }
    }

    // MARK: Saving a water profile

    private var saveSection: some View {
        Section {
            TextField("Name", text: $saveName)
            Button("Save Profile") { saveProfile() }
                .disabled(saveName.trimmingCharacters(in: .whitespaces).isEmpty)
            if let saveMessage {
                Text(saveMessage).foregroundStyle(.secondary)
            }
        } header: {
            Text("Save This Profile")
        } footer: {
            Text("Saves the start water, the reverse osmosis share, the salts, and the target when it is on. The salts "
                + "are kept as grams per liter, so a recipe scales them to its own mash water. Load a saved profile in "
                + "the Water tab of any recipe.")
        }
    }

    private func saveProfile() {
        let inputs = inputs(in: spec.sections.filter { $0.tool == "water.profile" && $0.readouts.isEmpty })
        guard let profile = SavedWaterProfile(name: saveName.trimmingCharacters(in: .whitespaces), inputs: inputs) else {
            saveMessage = "Enter a water volume above zero before saving."
            return
        }
        do {
            try waterProfiles.save(profile)
            saveMessage = "Saved “\(profile.name)”. Load it in the Water tab of a recipe."
        } catch {
            saveMessage = error.localizedDescription
        }
    }

    private func displayValue(_ value: Double?) -> String {
        guard let value else { return "—" }
        return value.formatted(.number.precision(.significantDigits(1...6)))
    }
}
