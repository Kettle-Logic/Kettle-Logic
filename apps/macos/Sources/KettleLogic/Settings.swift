import SwiftUI

/// The keys and defaults of the app's settings, stored in the user defaults.
/// `register` is called once at launch, so every reader sees the same default.
enum Preferences {
    static let autosaveKey = "autosave"
    static let confirmDeleteKey = "confirmDelete"
    static let unitsKey = "units"
    static let defaultEquipmentKey = "defaultEquipment"
    /// The folder new recipes are saved in, as a path. Empty means the default folder.
    static let recipeFolderKey = "recipeFolder"
    /// Whether the brew-day window keeps the display awake while it is open.
    static let keepDisplayOnKey = "keepDisplayOn"
    /// Whether the recipe view shows only what a new brewer needs. Advanced tabs and fields stay
    /// reachable by turning this off, and a tab that already holds data is never hidden.
    static let simpleModeKey = "simpleMode"
    /// Whether the user has been asked which interface they want. Unset on a first launch, which shows
    /// the choice sheet. The answer is stored in `simpleModeKey`, and can be changed in Settings.
    static let interfaceChosenKey = "interfaceChosen"
    /// Whether the recipe's tabs are grouped under Recipe, Ingredients, Process, and Results. Off by default
    /// while the layout settles.
    static let groupedTabsKey = "groupedTabs"
    /// Whether the app checks GitHub for updates when it starts. A check the user asks for always runs.
    static let checkForUpdatesKey = "checkForUpdates"

    static var defaults: [String: Any] {
        [
            simpleModeKey: true,
            interfaceChosenKey: false,
            groupedTabsKey: true,
            checkForUpdatesKey: true,
            autosaveKey: true,
            confirmDeleteKey: true,
            unitsKey: UnitSystem.metric.rawValue,
            defaultEquipmentKey: Equipment.builtIn[0].name,
            recipeFolderKey: "",
            keepDisplayOnKey: true,
        ]
    }

    /// The default brewing system for a new recipe, by name. A name that no system has is resolved by
    /// `EquipmentLibrary.profile(named:)`.
    static var defaultEquipmentName: String {
        UserDefaults.standard.string(forKey: defaultEquipmentKey) ?? Equipment.builtIn[0].name
    }
}

/// The unit system for measured values the user types or reads.
enum UnitSystem: String, CaseIterable, Identifiable {
    case metric
    case us

    var id: Self { self }

    static var current: UnitSystem {
        UnitSystem(rawValue: UserDefaults.standard.string(forKey: Preferences.unitsKey) ?? "") ?? .metric
    }

    var title: String {
        switch self {
        case .metric: "Metric"
        case .us: "US customary"
        }
    }

    /// The engine's unit for a volume, such as the batch size.
    var volumeUnit: String { self == .metric ? "l" : "gal" }

    /// The mass unit for a grain or other bulk fermentable.
    var bulkMassUnit: String { self == .metric ? "kg" : "lb" }

    /// The mass unit for hops and other small weights.
    var smallMassUnit: String { self == .metric ? "g" : "oz" }

    /// A volume in liters, converted to this system's volume unit for display.
    func displayVolume(liters: Double) -> Double {
        self == .metric ? liters : liters / Self.litersPerGallon
    }

    /// The symbol shown beside a volume the user types or reads.
    var volumeSymbol: String { self == .metric ? "L" : "gal" }

    /// A volume ratio, per unit of grain, as the mash views show it.
    var grainRatioSymbol: String { self == .metric ? "L/kg" : "qt/lb" }

    /// How many display volume units one liter is: 1 for metric, gallons per liter for US.
    var volumeFactor: Double { self == .metric ? 1 : 1 / Self.litersPerGallon }

    /// How many display ratio units one L/kg is: 1 for metric, quarts per pound for US.
    var grainRatioFactor: Double { self == .metric ? 1 : Self.quartsPerLiter / Self.poundsPerKilogram }

    /// The symbol for a temperature the user types or reads.
    var temperatureSymbol: String { self == .metric ? "°C" : "°F" }

    /// A temperature in °C, converted to this system's unit for display.
    func displayTemperature(celsius: Double) -> Double {
        self == .metric ? celsius : celsius * 9 / 5 + 32
    }

    /// A temperature typed in this system's unit, as °C.
    func celsius(fromDisplay value: Double) -> Double {
        self == .metric ? value : (value - 32) * 5 / 9
    }

    /// A temperature in °C as text in this system's unit, such as "66 °C" or "151 °F".
    func temperatureText(celsius: Double) -> String {
        let value = displayTemperature(celsius: celsius)
        return "\(value.formatted(.number.precision(.fractionLength(0...1)))) \(temperatureSymbol)"
    }

    /// A binding to a temperature stored in °C, read and written in this system's unit.
    func temperature(_ celsius: Binding<Double>) -> Binding<Double> {
        Binding(
            get: { displayTemperature(celsius: celsius.wrappedValue) },
            set: { celsius.wrappedValue = self.celsius(fromDisplay: $0) })
    }

    /// The same for a temperature that may be empty.
    func temperature(_ celsius: Binding<Double?>) -> Binding<Double?> {
        Binding(
            get: { celsius.wrappedValue.map { displayTemperature(celsius: $0) } },
            set: { celsius.wrappedValue = $0.map { self.celsius(fromDisplay: $0) } })
    }

    /// The unit a yeast starter is sized in: liters, or quarts in the US system, where a gallon is too large to read well.
    var starterSymbol: String { self == .metric ? "L" : "qt" }

    /// How many starter units one liter is.
    var starterFactor: Double { self == .metric ? 1 : Self.quartsPerLiter }

    /// A starter volume typed in this system's unit, as liters text. Metric text passes through unchanged.
    func litersText(starter text: String) -> String {
        guard self == .us, let value = Double(text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")) else {
            return text
        }
        return String(format: "%.4f", value / Self.quartsPerLiter)
    }

    /// A starter volume in liters text, as this system's starter unit to two places. Metric text passes through unchanged.
    func starterText(liters text: String) -> String {
        guard self == .us, let value = Double(text) else { return text }
        return String(format: "%.2f", value * Self.quartsPerLiter)
    }

    /// A volume typed in this system's unit, as liters text for the engine. Text that is not a number is returned as typed.
    func litersText(volume: String) -> String {
        guard self == .us, let value = Double(volume) else { return volume }
        return String(format: "%.3f", value * Self.litersPerGallon)
    }

    /// A volume in liters, as text in this system's volume unit, to two decimal places.
    func volumeText(liters: String) -> String {
        guard self == .us, let value = Double(liters) else { return liters }
        return String(format: "%.2f", value / Self.litersPerGallon)
    }

    static let litersPerGallon = 3.785411784
    static let quartsPerLiter = 1.056688
    static let poundsPerKilogram = 2.204623
}

/// The app's settings window, opened from the app menu with Cmd+, .
struct SettingsView: View {
    @AppStorage(Preferences.autosaveKey) private var autosave = true
    @AppStorage(Preferences.confirmDeleteKey) private var confirmDelete = true
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric
    @AppStorage(Preferences.defaultEquipmentKey) private var defaultEquipment = Equipment.builtIn[0].name
    @AppStorage(Preferences.recipeFolderKey) private var recipeFolder = ""
    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true
    @AppStorage(Preferences.groupedTabsKey) private var groupedTabs = true
    @AppStorage(Preferences.checkForUpdatesKey) private var checkForUpdates = true
    @Environment(EquipmentLibrary.self) private var equipment
    @Environment(UpdateCenter.self) private var updates

    var body: some View {
        Form {
            Section {
                Toggle("Simple mode", isOn: $simpleMode)
                Toggle("Group recipe tabs", isOn: $groupedTabs)
            } header: {
                Text("Interface")
            } footer: {
                Text("Shows only the basics for a new brewer: ingredients, results, style, and the brew-day "
                    + "procedure. Water chemistry, mash, yeast starter, boil-off, and detailed ingredient "
                    + "fields are hidden, and the app assumes typical values for them. Nothing is deleted, "
                    + "and anything a recipe already uses stays visible. Also in the View menu. "
                    + "Grouping puts the recipe's tabs under four headings, Recipe, Ingredients, Process, and "
                    + "Results, which ⌘1 to ⌘4 switch between.")
            }

            Section {
                Toggle("Autosave changes", isOn: $autosave)
            } header: {
                Text("Saving")
            } footer: {
                Text("Autosave writes changes to the file the recipe was opened from or saved to, a few "
                    + "seconds after each change. A new recipe is saved only with Save As.")
            }

            Section {
                LabeledContent("Folder") {
                    Text((RecipeLibrary.folder.path(percentEncoded: false) as NSString).abbreviatingWithTildeInPath)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .textSelection(.enabled)
                }
                HStack {
                    Button("Choose…") {
                        if let url = FilePanels.chooseFolder() {
                            recipeFolder = url.path(percentEncoded: false)
                        }
                    }
                    Button("Use Default") { recipeFolder = "" }
                        .disabled(recipeFolder.isEmpty)
                }
            } header: {
                Text("Recipe Folder")
            } footer: {
                Text("The recipes in this folder open when the app starts, and new recipes are saved here. "
                    + "The Open, Save As, and Journal CSV panels start in it. A new folder takes effect at the "
                    + "next launch. The default is KettleLogic/Recipes in your home folder.")
            }

            Section {
                Toggle("Check for updates when Kettle Logic starts", isOn: $checkForUpdates)
                Button("Check for Updates…") {
                    Task { await updates.check(automatically: false) }
                }
            } header: {
                Text("Updates")
            } footer: {
                Text("Updates come from GitHub. The engine, which does the calculations, and the app can be updated "
                    + "separately or together. Each download is checked against the list of updates and must be "
                    + "signed by the same developer. The new version starts the next time Kettle Logic opens.")
            }

            Section {
                Picker("Units", selection: $units) {
                    ForEach(UnitSystem.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                Text("Units")
            } footer: {
                Text("Sets the units a new ingredient starts in, the units volumes are shown in, and the "
                    + "batch size a brewing system is applied with. Existing ingredients keep their units.")
            }

            Section {
                Picker("Default brewing system", selection: defaultEquipmentChoice) {
                    ForEach(equipment.all) { Text($0.name).tag($0.name) }
                }
                Toggle("Confirm before deleting a recipe", isOn: $confirmDelete)
            } header: {
                Text("Recipes")
            } footer: {
                Text("The brewing system a recipe starts with. Deleting a recipe can always be undone.")
            }
        }
        .formStyle(.grouped)
        .frame(width: 480)
        .scenePadding()
    }

    /// The default system by name. A saved name that no system has shows the system a recipe falls back to.
    private var defaultEquipmentChoice: Binding<String> {
        Binding(
            get: { equipment.profile(named: defaultEquipment).name },
            set: { defaultEquipment = $0 })
    }
}
