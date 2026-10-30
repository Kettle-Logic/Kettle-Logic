import Foundation
import Observation

// Water profiles saved from the Water Profile tool, to load into the Water tab of any recipe. A saved
// profile keeps its salts as grams per liter, so each recipe scales them to its own mash water.

/// The salts the Water Profile tool adds. The raw value is the tool's input name.
enum WaterProfileSalt: String, CaseIterable {
    case gypsum, calciumChloride, epsomSalt, magnesiumChloride, tableSalt, bakingSoda, chalk, picklingLime

    /// The salt's name in the engine, and in a recipe's water settings.
    var kind: String {
        switch self {
        case .gypsum: "gypsum"
        case .calciumChloride: "calcium chloride"
        case .epsomSalt: "epsom salt"
        case .magnesiumChloride: "magnesium chloride"
        case .tableSalt: "table salt"
        case .bakingSoda: "baking soda"
        case .chalk: "chalk"
        case .picklingLime: "pickling lime"
        }
    }
}

/// A salt in a saved profile: its kind, and its grams for each liter of water.
struct SavedSalt: Codable, Equatable {
    var kind: String
    var gramsPerLiter: Double
}

/// A water profile saved from the Water Profile tool.
struct SavedWaterProfile: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    /// The start water in mg/L, before the reverse osmosis share.
    var start: IonsInput
    /// The share of reverse osmosis water, in percent.
    var roPercent: Double
    var salts: [SavedSalt]
    /// The target water, when the tool was comparing with one.
    var target: IonsInput?

    /// Reads a profile from the Water Profile tool's inputs, as they were typed. Returns nil when the volume is not
    /// above zero, because the salts are kept per liter.
    init?(name: String, inputs: [String: String]) {
        func number(_ key: String) -> Double {
            Double((inputs[key] ?? "").trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")) ?? 0
        }
        let liters = number("volume")
        guard liters > 0 else { return nil }

        self.name = name
        start = IonsInput(
            calcium: number("calcium"), magnesium: number("magnesium"), sodium: number("sodium"),
            sulfate: number("sulfate"), chloride: number("chloride"), bicarbonate: number("bicarbonate"))
        roPercent = number("roPercent")
        salts = WaterProfileSalt.allCases.compactMap { salt in
            let grams = number(salt.rawValue)
            return grams > 0 ? SavedSalt(kind: salt.kind, gramsPerLiter: grams / liters) : nil
        }
        target = inputs["useTarget"] == "1" ? IonsInput(
            calcium: number("targetCalcium"), magnesium: number("targetMagnesium"), sodium: number("targetSodium"),
            sulfate: number("targetSulfate"), chloride: number("targetChloride"),
            bicarbonate: number("targetBicarbonate")) : nil
    }

    /// Sets a recipe's water to this profile. The salts are scaled to the recipe's mash water, and the profile's
    /// target replaces the recipe's only when the profile has one.
    func apply(to water: inout WaterSettings, mashLiters: Double) {
        water.baseName = "Custom"
        water.base = start
        water.roPercent = roPercent
        water.leaveOutSalts = false
        water.salts = salts.map { SaltInput(kind: $0.kind, grams: $0.gramsPerLiter * mashLiters) }
        if let target {
            water.useTarget = true
            water.target = target
        }
    }
}

/// The water profiles the user saved, kept as JSON in the app's Application Support folder. A file that cannot be
/// read starts the library empty, and the next save replaces it.
@MainActor
@Observable
final class WaterProfileLibrary {
    private(set) var profiles: [SavedWaterProfile] = []
    private let file: URL?

    init() {
        file = Self.defaultFile
        profiles = load()
    }

    /// Saves a profile as new, or replaces the saved profile with the same name, so saving again updates it.
    func save(_ profile: SavedWaterProfile) throws {
        if let index = profiles.firstIndex(where: { $0.name == profile.name }) {
            var replacement = profile
            replacement.id = profiles[index].id
            profiles[index] = replacement
        } else {
            profiles.append(profile)
        }
        try write()
    }

    func remove(_ id: UUID) throws {
        profiles.removeAll { $0.id == id }
        try write()
    }

    private static var defaultFile: URL? {
        guard let support = try? FileManager.default.url(
            for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        else { return nil }
        let folder = support.appendingPathComponent("KettleLogic", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder.appendingPathComponent("water-profiles.json")
    }

    private func load() -> [SavedWaterProfile] {
        guard let file, let data = try? Data(contentsOf: file) else { return [] }
        return (try? JSONDecoder().decode([SavedWaterProfile].self, from: data)) ?? []
    }

    private func write() throws {
        guard let file else {
            throw EngineError(message: "The water profile library has no folder to save to.")
        }
        try JSONEncoder().encode(profiles).write(to: file, options: .atomic)
    }
}
