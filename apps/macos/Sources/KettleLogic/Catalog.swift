import Foundation

// Common ingredients that prefill the add sheets. The entries live in the engine
// (`core/src/catalog/ingredients.cpp`), so every front end offers the same ones, and
// this file only reads them through the C facade. The figures are typical, not
// exact: hop alpha acid changes with every crop, malt color and yield change with
// the maltster's lot, and yeast attenuation depends on the wort. Each sheet shows
// the values before they are added, so check them against the packet or the lab
// sheet. An attenuation is left blank where there is no widely agreed figure,
// because the first culture with an attenuation sets the final gravity.
//
// Water salts, sulfite, and tartaric acid are not here. The engine only counts
// them when they are tagged as a salt or an additive, and the app cannot set
// those tags yet.

/// An entry in a catalog menu, grouped under a heading.
protocol CatalogItem: Identifiable {
    var name: String { get }
    var group: String { get }
}

extension CatalogItem {
    var id: String { "\(group)/\(name)" }
}

struct FermentableTemplate: CatalogItem, Decodable {
    let name: String
    let group: String
    let type: String
    /// The potential, in specific gravity.
    let yieldSG: String
    let colorSRM: String

    private enum CodingKeys: String, CodingKey {
        case name, group, type
        case yieldSG = "yield_sg"
        case colorSRM = "color_srm"
    }
}

struct HopTemplate: CatalogItem, Decodable {
    let name: String
    let group: String
    let alphaPercent: String
    /// A typical mid-range figure, like the alpha acid: it changes with every crop, so check the packet.
    let betaPercent: String?

    private enum CodingKeys: String, CodingKey {
        case name, group
        case alphaPercent = "alpha_percent"
        case betaPercent = "beta_percent"
    }
}

struct CultureTemplate: CatalogItem, Decodable {
    let name: String
    let group: String
    let type: String
    let form: String
    let attenuationPercent: String?

    private enum CodingKeys: String, CodingKey {
        case name, group, type, form
        case attenuationPercent = "attenuation_percent"
    }
}

struct WaterTemplate: CatalogItem, Decodable {
    let name: String
    let group: String
    /// Calcium, bicarbonate, sulfate, chloride, sodium, and magnesium, in mg/L.
    let calcium: String
    let bicarbonate: String
    let sulfate: String
    let chloride: String
    let sodium: String
    let magnesium: String
}

struct MiscellaneousTemplate: CatalogItem, Decodable {
    let name: String
    let group: String
    let type: String
    let unit: String
}

enum Catalog {
    static var fermentables: [FermentableTemplate] { contents.fermentables }
    static var hops: [HopTemplate] { contents.hops }
    static var cultures: [CultureTemplate] { contents.cultures }
    /// Well-known brewing waters, as commonly published. A real water changes
    /// with the season and the source, so test your own.
    static var waters: [WaterTemplate] { contents.waters }
    static var miscellaneous: [MiscellaneousTemplate] { contents.miscellaneous }

    private struct Contents: Decodable {
        var fermentables: [FermentableTemplate] = []
        var hops: [HopTemplate] = []
        var cultures: [CultureTemplate] = []
        var waters: [WaterTemplate] = []
        var miscellaneous: [MiscellaneousTemplate] = []
    }

    /// Every entry, read from the engine once.
    private static let contents = BuiltinData.load("ingredient_catalog", as: Contents.self)
}
