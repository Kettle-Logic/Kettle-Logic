import CKettleLogic
import Foundation

/// Reads one of the engine's built-in datasets (`core/src/catalog`): the ingredient catalog, brewing systems,
/// starter recipes, fermentation profiles, glossary, tool forms, and option lists. The engine owns the data, so
/// every front end offers the same entries, and this app only decodes it. The data is compiled into the engine,
/// so a failure here is a mismatch between the engine and the app, not something a user can fix.
enum BuiltinData {
    static func load<T: Decodable>(_ dataset: String, as type: T.Type = T.self) -> T {
        guard let json = dataset == "ingredient_catalog" ? kl_ingredient_catalog_json() : kl_builtin_data_json(dataset)
        else { fatalError("The engine has no built-in dataset named \(dataset).") }
        defer { kl_string_free(json) }
        do {
            return try JSONDecoder().decode(T.self, from: Data(String(cString: json).utf8))
        } catch {
            fatalError("The engine's \(dataset) data does not match the app: \(error)")
        }
    }
}
