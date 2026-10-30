import Foundation
import Observation

/// The brewing systems the user made, kept as JSON in the app's Application Support folder. A file that
/// cannot be read starts the library empty, and the next save replaces it. With the built-in systems, these
/// are the choices a recipe and the settings offer.
@MainActor
@Observable
final class EquipmentLibrary {
    private(set) var custom: [EquipmentProfile] = []
    private let file: URL?

    init() {
        file = Self.defaultFile
        custom = load()
    }

    /// Every brewing system: the built-in ones, then the user's own.
    var all: [EquipmentProfile] { Equipment.builtIn + custom }

    /// The system with `name`, or the first built-in system when no system has that name, such as after a
    /// custom one is deleted.
    func profile(named name: String) -> EquipmentProfile {
        all.first { $0.name == name } ?? Equipment.builtIn[0]
    }

    /// Saves a system as a new one, or as the change to the custom system called `replacing`. Each name is used
    /// by one system only, so a name that another system has is refused.
    func save(_ profile: EquipmentProfile, replacing original: String?) throws {
        var profile = profile
        profile.name = trimmed(profile.name)
        guard !profile.name.isEmpty else {
            throw EngineError(message: "A brewing system needs a name.")
        }
        guard !all.contains(where: { $0.name == profile.name && $0.name != original }) else {
            throw EngineError(message: "A brewing system called \u{201C}\(profile.name)\u{201D} already exists.")
        }
        if let original, let index = custom.firstIndex(where: { $0.name == original }) {
            custom[index] = profile
        } else {
            custom.append(profile)
        }
        try write()
    }

    /// Adds a copy of the custom system called `name`, named after it with " copy" added.
    func duplicate(_ name: String) throws {
        guard var copy = custom.first(where: { $0.name == name }) else { return }
        copy.name = uniqueName(for: "\(name) copy")
        custom.append(copy)
        try write()
    }

    func remove(_ name: String) throws {
        custom.removeAll { $0.name == name }
        try write()
    }

    /// `base`, or `base` with a number after it, whichever no system has yet.
    private func uniqueName(for base: String) -> String {
        let taken = Set(all.map(\.name))
        var candidate = base
        var number = 2
        while taken.contains(candidate) {
            candidate = "\(base) \(number)"
            number += 1
        }
        return candidate
    }

    private static var defaultFile: URL? {
        guard let support = try? FileManager.default.url(
            for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        else { return nil }
        let folder = support.appendingPathComponent("KettleLogic", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder.appendingPathComponent("custom-equipment.json")
    }

    private func load() -> [EquipmentProfile] {
        guard let file, let data = try? Data(contentsOf: file) else { return [] }
        return (try? JSONDecoder().decode([EquipmentProfile].self, from: data)) ?? []
    }

    private func write() throws {
        guard let file else {
            throw EngineError(message: "The equipment library has no folder to save to.")
        }
        try JSONEncoder().encode(custom).write(to: file, options: .atomic)
    }
}
