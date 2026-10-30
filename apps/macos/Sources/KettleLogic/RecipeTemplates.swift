import Foundation

// Starter recipes for the new-recipe wizard, which `RecipeBuilder` turns into a real recipe. The recipes live
// in the engine (`core/src/catalog/recipe_templates.cpp`) so every front end offers the same ones, and this
// file reads them. See that file for how the amounts are worked out.
//
// DRAFT FOR REVIEW. These are typical, conventional recipes for each style, written to be forgiving
// for a first-time brewer, not tuned recipes. Every figure is open to correction.
//
// A template does not store amounts. The wizard works the amounts out from the batch size and the
// brewing system's efficiency, so one template fits any size of batch:
//   - grain: each fermentable is a share of the total fermentable weight, and the total is the weight
//     that reaches `targetOG`.
//   - boil hops: each addition is a share of `targetIBU`, and the weight that gives that share is
//     worked out from the hop's alpha acid and the boil time.
//   - aroma hops: a fixed weight per liter of finished beer, since aroma does not follow a formula.
//
// Names must match `Catalog`, so the add sheets and the wizard agree on alpha acid, yield, and
// color. `problems` checks that, and that each recipe sits inside its BJCP 2021 ranges.
//
// Left out on purpose: lagers (they need cold, steady fermentation most first-time brewers cannot
// give), sours, and anything that needs a yeast starter. Extract versions are not here: the engine applies
// brewhouse efficiency to extract and sugar too, so it would size them about 30 percent too heavy.

/// One fermentable in a template, as a share of the fermentable weight.
struct TemplateGrain: Decodable {
    let name: String
    let percent: Double
}

/// A hop added during the boil, as a share of the recipe's bitterness.
struct TemplateBoilHop: Decodable {
    let name: String
    /// Minutes before the end of the boil.
    let minutes: Int
    let ibuPercent: Double
}

/// A hop added after the boil for aroma, by weight per liter of finished beer.
struct TemplateAromaHop: Decodable {
    enum Timing: Decodable {
        /// Added to the boil with `minutes` left. Sized by weight, since a late addition adds little bitterness.
        case boil(minutes: Int)
        /// Steeped after the boil ends, for `minutes`.
        case flameout(minutes: Int)
        /// Added to the fermenter, for `days`.
        case dryHop(days: Int)

        private enum CodingKeys: String, CodingKey { case kind, value }

        init(from decoder: Decoder) throws {
            let container = try decoder.container(keyedBy: CodingKeys.self)
            let value = try container.decode(Int.self, forKey: .value)
            switch try container.decode(String.self, forKey: .kind) {
            case "boil": self = .boil(minutes: value)
            case "flameout": self = .flameout(minutes: value)
            case "dryHop": self = .dryHop(days: value)
            case let other:
                throw DecodingError.dataCorruptedError(
                    forKey: .kind, in: container, debugDescription: "Unknown aroma hop timing \(other).")
            }
        }
    }

    let name: String
    let gramsPerLiter: Double
    let timing: Timing
}

struct RecipeTemplate: Identifiable, Decodable {
    let id: String
    let name: String
    /// One plain sentence for the wizard's list.
    let summary: String
    /// The BJCP 2021 guideline id the recipe is compared against.
    let bjcpStyle: String
    let targetOG: Double
    let targetIBU: Double
    let boilMinutes: Int
    let grains: [TemplateGrain]
    let boilHops: [TemplateBoilHop]
    let aromaHops: [TemplateAromaHop]
    /// The yeast, by `Catalog.cultures` name.
    let yeast: String
    /// The attenuation to give the yeast when the catalog has none, so the recipe still shows a final
    /// gravity and alcohol. A typical figure for the style, to check against the packet.
    let yeastAttenuationPercent: String?
    let mashTempC: Double
    let fermentTempC: ClosedRange<Double>
    /// Whirlfloc tablets, added with 15 minutes left in the boil. Helps clarity, and costs nothing.
    let usesWhirlfloc: Bool
    /// What the brewer should know that the numbers do not say. A temperature is written `{24}` for 24 °C, and
    /// `tips(in:)` shows it in the user's unit.
    let tips: [String]

    private enum CodingKeys: String, CodingKey {
        case id, name, summary, bjcpStyle, targetOG, targetIBU, boilMinutes, grains, boilHops, aromaHops, yeast
        case yeastAttenuationPercent, mashTempC, fermentTempC, usesWhirlfloc, tips
    }

    private struct TempRange: Decodable {
        let lower: Double
        let upper: Double
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(String.self, forKey: .id)
        name = try c.decode(String.self, forKey: .name)
        summary = try c.decode(String.self, forKey: .summary)
        bjcpStyle = try c.decode(String.self, forKey: .bjcpStyle)
        targetOG = try c.decode(Double.self, forKey: .targetOG)
        targetIBU = try c.decode(Double.self, forKey: .targetIBU)
        boilMinutes = try c.decode(Int.self, forKey: .boilMinutes)
        grains = try c.decode([TemplateGrain].self, forKey: .grains)
        boilHops = try c.decode([TemplateBoilHop].self, forKey: .boilHops)
        aromaHops = try c.decode([TemplateAromaHop].self, forKey: .aromaHops)
        yeast = try c.decode(String.self, forKey: .yeast)
        yeastAttenuationPercent = try c.decodeIfPresent(String.self, forKey: .yeastAttenuationPercent)
        mashTempC = try c.decode(Double.self, forKey: .mashTempC)
        let range = try c.decode(TempRange.self, forKey: .fermentTempC)
        fermentTempC = range.lower...range.upper
        usesWhirlfloc = try c.decode(Bool.self, forKey: .usesWhirlfloc)
        tips = try c.decode([String].self, forKey: .tips)
    }
}

extension RecipeTemplate {
    /// The engine's starter recipes, in the order the wizard lists them.
    static let all: [RecipeTemplate] = BuiltinData.load("recipe_templates")
}

extension RecipeTemplate {
    /// Whatever is wrong with the template, as sentences. Empty when it is sound. A template is sound when its
    /// shares add up, its ingredients are in the `Catalog`, and its gravity and bitterness sit inside the
    /// BJCP range for the style it names.
    var problems: [String] {
        var found: [String] = []
        if abs(grains.reduce(0) { $0 + $1.percent } - 100) > 0.01 { found.append("Grain percentages do not add to 100.") }
        if abs(boilHops.reduce(0) { $0 + $1.ibuPercent } - 100) > 0.01 { found.append("Hop bitterness shares do not add to 100.") }
        for grain in grains where !Catalog.fermentables.contains(where: { $0.name == grain.name }) {
            found.append("\(grain.name) is not a fermentable in the catalog.")
        }
        for hop in boilHops.map(\.name) + aromaHops.map(\.name) where !Catalog.hops.contains(where: { $0.name == hop }) {
            found.append("\(hop) is not a hop in the catalog.")
        }
        if !Catalog.cultures.contains(where: { $0.name == yeast }) { found.append("\(yeast) is not a culture in the catalog.") }
        if let style = bjcpStyles.first(where: { $0.id == bjcpStyle }) {
            if !(style.og.low...style.og.high).contains(targetOG) { found.append("Target OG is outside the \(style.title) range.") }
            if !(style.ibu.low...style.ibu.high).contains(targetIBU) { found.append("Target IBU is outside the \(style.title) range.") }
        } else {
            found.append("\(bjcpStyle) is not a BJCP style the engine knows.")
        }
        return found
    }
}

extension RecipeTemplate {
    /// The tips, with each `{n}` temperature in °C written in the user's unit.
    func tips(in units: UnitSystem) -> [String] {
        tips.map { tip in
            var text = tip
            while let open = text.firstIndex(of: "{"), let close = text[open...].firstIndex(of: "}"),
                  let celsius = Double(text[text.index(after: open)..<close]) {
                text.replaceSubrange(open...close, with: units.temperatureText(celsius: celsius))
            }
            return text
        }
    }
}
