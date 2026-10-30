import CKettleLogic
import Foundation

// Searching, filtering, and sorting a list of ingredients. The engine does the work
// (`core/src/catalog/ingredient_query.cpp`), so every front end finds and orders alike. This file
// describes a list to the engine through the C facade and reads the answer back.

/// One ingredient as the engine's search sees it. `primary` and `secondary` are the numbers the engine
/// names for the kind (see `IngredientSearchResult.Metrics`), in its units.
struct IngredientSearchEntry: Encodable {
    let id: String
    let name: String
    let group: String
    let type: String
    let form: String
    let custom: Bool
    let primary: Double?
    let secondary: Double?
}

enum IngredientSortKey: String, Encodable, CaseIterable {
    case catalog, name, group, primary, secondary
}

enum IngredientSourceFilter: String, Encodable, CaseIterable {
    case any, builtIn, custom
}

/// What the user asked for: words to find, filters, and an order.
struct IngredientSearch: Encodable, Equatable {
    var text = ""
    var groups: Set<String> = []
    var types: Set<String> = []
    var forms: Set<String> = []
    var source = IngredientSourceFilter.any
    var minPrimary: Double?
    var maxPrimary: Double?
    var sort = IngredientSortKey.catalog
    var descending = false

    /// Whether any filter is set. The words and the order are not filters.
    var isFiltering: Bool {
        !groups.isEmpty || !types.isEmpty || !forms.isEmpty || source != .any || minPrimary != nil || maxPrimary != nil
    }

    mutating func clearFilters() {
        groups = []
        types = []
        forms = []
        source = .any
        minPrimary = nil
        maxPrimary = nil
    }
}

struct IngredientSearchResult: Decodable {
    /// Positions in the entries that were searched, in the order to show them.
    let indices: [Int]
    /// What all the entries offer to filter on, not only those that passed.
    let facets: Facets
    let metrics: Metrics

    struct Facets: Decodable {
        let groups: [String]
        let types: [String]
        let forms: [String]
        let primaryMin: Double?
        let primaryMax: Double?
    }

    struct Metric: Decodable {
        let label: String
        let unit: String

        /// "Color (SRM)", or just the label when the number has no unit.
        var title: String { unit.isEmpty ? label : "\(label) (\(unit))" }
    }

    struct Metrics: Decodable {
        let primary: Metric?
        let secondary: Metric?
    }
}

enum IngredientSearchEngine {
    private struct Request: Encodable {
        let kind: String
        let entries: [IngredientSearchEntry]
        let search: IngredientSearch

        func encode(to encoder: Encoder) throws {
            try search.encode(to: encoder)
            var c = encoder.container(keyedBy: CodingKeys.self)
            try c.encode(kind, forKey: .kind)
            try c.encode(entries, forKey: .entries)
        }

        private enum CodingKeys: String, CodingKey { case kind, entries }
    }

    /// Runs a search over `entries`. The engine refuses only a malformed request, which would be a mismatch
    /// between the engine and the app, so a failure shows every entry in its own order.
    static func run(kind: IngredientKind, entries: [IngredientSearchEntry], search: IngredientSearch)
        -> IngredientSearchResult?
    {
        guard let body = try? JSONEncoder().encode(Request(kind: kind.rawValue, entries: entries, search: search)),
            let raw = String(data: body, encoding: .utf8),
            let json = kl_ingredient_query_json(raw)
        else { return nil }
        defer { kl_string_free(json) }
        return try? JSONDecoder().decode(IngredientSearchResult.self, from: Data(String(cString: json).utf8))
    }
}

extension MenuEntry {
    /// Whether the user made this entry, as opposed to the built-in catalog.
    var isCustom: Bool { id.hasPrefix("custom-") }

    /// The entry as the engine's search sees it. The numbers are in the units `ingredientMetrics` names:
    /// a fermentable's color in SRM, which an EBC or Lovibond figure is converted to, and its yield as a
    /// gravity. A figure in another unit, or one that is not a number, is left out, so a bound or a sort
    /// skips it.
    var searchEntry: IngredientSearchEntry {
        let primary: Double?
        let secondary: Double?
        switch draft.kind {
        case .fermentable:
            let color = Double(trimmed(draft.colorValue))
            switch draft.colorUnit {
            case "SRM": primary = color
            case "EBC": primary = color.map { $0 / 1.97 }
            case "Lovi": primary = color.map { 1.3 * $0 - 0.6 }
            default: primary = nil
            }
            secondary = draft.yieldUnit == "sg" ? Double(trimmed(draft.yieldValue)) : nil
        case .hop:
            primary = Double(trimmed(draft.alpha))
            secondary = Double(trimmed(draft.beta))
        case .culture:
            primary = Double(trimmed(draft.attenuation))
            secondary = nil
        case .water, .miscellaneous:
            primary = nil
            secondary = nil
        }
        return IngredientSearchEntry(
            id: id, name: name, group: group, type: draft.kind == .hop ? "" : draft.type,
            form: draft.kind == .fermentable || draft.kind == .miscellaneous ? "" : draft.form,
            custom: isCustom, primary: primary, secondary: secondary)
    }
}
