import Foundation
import Observation

/// An ingredient the user made, kept in their library. Its draft holds the values an
/// editor shows, so it starts from the same place as a catalog entry.
struct CustomIngredient: Codable, Identifiable {
    let id: UUID
    var draft: IngredientDraft
}

/// The user's custom ingredients, saved as JSON in the app's Application Support
/// folder. A file that cannot be read starts the library empty, and the next save
/// replaces it.
@MainActor
@Observable
final class CustomIngredientStore {
    private(set) var ingredients: [CustomIngredient] = []
    private let file: URL?

    init() {
        file = Self.defaultFile
        ingredients = load()
    }

    /// Saves a draft as a new custom ingredient, or as a change to the one with `id`.
    func save(_ draft: IngredientDraft, replacing id: UUID?) throws {
        var draft = draft
        draft.name = trimmed(draft.name)
        if let id, let index = ingredients.firstIndex(where: { $0.id == id }) {
            ingredients[index].draft = draft
        } else {
            ingredients.append(CustomIngredient(id: UUID(), draft: draft))
        }
        try write()
    }

    /// Adds a copy of the ingredient with `id`, named after it with " copy" added.
    func duplicate(_ id: UUID) throws {
        guard let original = ingredients.first(where: { $0.id == id }) else { return }
        var draft = original.draft
        draft.name = trimmed(draft.name) + " copy"
        ingredients.append(CustomIngredient(id: UUID(), draft: draft))
        try write()
    }

    func remove(_ id: UUID) throws {
        ingredients.removeAll { $0.id == id }
        try write()
    }

    private static var defaultFile: URL? {
        guard let support = try? FileManager.default.url(
            for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        else { return nil }
        let folder = support.appendingPathComponent("KettleLogic", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder.appendingPathComponent("custom-ingredients.json")
    }

    private func load() -> [CustomIngredient] {
        guard let file, let data = try? Data(contentsOf: file) else { return [] }
        return (try? JSONDecoder().decode([CustomIngredient].self, from: data)) ?? []
    }

    private func write() throws {
        guard let file else {
            throw EngineError(message: "The ingredient library has no folder to save to.")
        }
        try JSONEncoder().encode(ingredients).write(to: file, options: .atomic)
    }
}
