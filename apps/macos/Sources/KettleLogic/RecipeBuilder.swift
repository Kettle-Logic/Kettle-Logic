import Foundation

/// What the new-recipe wizard has asked for.
struct RecipeBuildRequest {
    var template: RecipeTemplate
    /// The batch size as typed, in `units.volumeUnit`.
    var batchValue: String
    var profile: EquipmentProfile
    var units: UnitSystem

    /// The batch in liters, or nil when the text is not a positive number.
    var batchLiters: Double? {
        guard let value = Double(batchValue.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")),
              value > 0 else { return nil }
        return units == .metric ? value : value * UnitSystem.litersPerGallon
    }
}

enum RecipeBuildError: LocalizedError {
    case badBatchSize
    case unknownIngredient(String)
    case noGravity
    case noBitterness(String)

    var errorDescription: String? {
        switch self {
        case .badBatchSize: "Enter a batch size greater than zero."
        case .unknownIngredient(let name): "\(name) is not in the ingredient catalog."
        case .noGravity: "The engine could not work out a gravity for these fermentables."
        case .noBitterness(let name): "\(name) adds no bitterness, so it cannot be sized."
        }
    }
}

/// Builds a recipe from a template. The amounts are not stored in the template. They are worked out
/// from the engine's own results: the grain is scaled until the original gravity reaches the target,
/// and each boil hop until its share of the target bitterness is reached. Everything is one undo step.
enum RecipeBuilder {
    /// Adds a new recipe to `document`, built from the request, and returns its id.
    static func add(_ request: RecipeBuildRequest, to document: RecipeDocument) throws -> UInt64 {
        var created: UInt64 = 0
        try document.grouped("New Recipe from Style") {
            created = try document.addRecipe()
            try fill(request, in: document, recipe: created)
        }
        return created
    }

    /// Builds the request into a recipe that already exists, such as a new document's first recipe.
    static func populate(_ request: RecipeBuildRequest, in document: RecipeDocument, recipe: UInt64) throws {
        try document.grouped("New Recipe from Style") {
            try fill(request, in: document, recipe: recipe)
        }
    }

    // MARK: Building

    private static func fill(_ request: RecipeBuildRequest, in document: RecipeDocument, recipe: UInt64) throws {
        guard let batchLiters = request.batchLiters else { throw RecipeBuildError.badBatchSize }
        let template = request.template
        let units = request.units
        let profile = request.profile

        try document.rename(recipe, to: template.name)
        try document.setType(recipe, to: "all grain")
        try document.applyEquipment(
            recipe, batchValue: request.batchValue, batchUnit: units.volumeUnit,
            efficiencyPercent: profile.efficiencyPercent, evaporationPercent: profile.evaporationPercent,
            trubLossLiters: profile.trubLossLiters)
        try document.setStyle(recipe, to: template.bjcpStyle)

        func current() -> RecipeSnapshot? { document.snapshot.recipes.first { $0.id == recipe } }

        try addGrains(template, units: units, document: document, recipe: recipe, current: current)
        try addYeast(template, document: document, recipe: recipe)
        try addHops(template, batchLiters: batchLiters, units: units, document: document, recipe: recipe, current: current)

        if template.usesWhirlfloc, let tablet = Catalog.miscellaneous.first(where: { $0.name == "Whirlfloc tablet" }) {
            var draft = IngredientDraft(tablet)
            draft.amount = "1"
            try draft.commit(to: document, recipe: recipe, existing: nil)
        }

        if let snapshot = current() {
            var planning = RecipePlanning.read(snapshot.planning)
            if !planning.mash.steps.isEmpty {
                planning.mash.steps[0].tempC = template.mashTempC
            }
            planning.notes = notes(for: template, units: units)
            if let text = planning.text {
                try document.setPlanning(recipe, to: text)
            }
        }
    }

    /// The process and tips that go with the style, as the recipe's starting notes.
    private static func notes(for template: RecipeTemplate, units: UnitSystem) -> String {
        let process = "Mash at \(units.temperatureText(celsius: template.mashTempC)) for 60 minutes. "
            + "Boil \(template.boilMinutes) minutes. "
            + "Ferment at \(units.temperatureText(celsius: template.fermentTempC.lowerBound)) to "
            + "\(units.temperatureText(celsius: template.fermentTempC.upperBound))."
        return ([process] + template.tips(in: units)).joined(separator: "\n\n")
    }

    /// Adds each grain at a trial weight, reads the original gravity, then rescales every grain by the
    /// same factor. The gravity is proportional to the weight, so one pass is exact before rounding.
    private static func addGrains(
        _ template: RecipeTemplate, units: UnitSystem, document: RecipeDocument, recipe: UInt64,
        current: () -> RecipeSnapshot?
    ) throws {
        let trialTotal = 3.0
        var added: [(draft: IngredientDraft, id: UInt64, percent: Double)] = []
        for grain in template.grains {
            guard let entry = Catalog.fermentables.first(where: { $0.name == grain.name }) else {
                throw RecipeBuildError.unknownIngredient(grain.name)
            }
            var draft = IngredientDraft(entry)
            draft.unit = units.bulkMassUnit
            draft.amount = decimal(trialTotal * grain.percent / 100, places: 3)
            try draft.commit(to: document, recipe: recipe, existing: nil)
            guard let id = current()?.fermentables.last?.id else { throw RecipeBuildError.noGravity }
            added.append((draft, id, grain.percent))
        }
        guard let og = current()?.results.originalGravity, og > 1 else { throw RecipeBuildError.noGravity }
        let scale = (template.targetOG - 1) / (og - 1)
        for var item in added {
            item.draft.amount = decimal(trialTotal * scale * item.percent / 100, places: 2)
            try item.draft.commit(to: document, recipe: recipe, existing: item.id)
        }
    }

    private static func addYeast(_ template: RecipeTemplate, document: RecipeDocument, recipe: UInt64) throws {
        guard let entry = Catalog.cultures.first(where: { $0.name == template.yeast }) else {
            throw RecipeBuildError.unknownIngredient(template.yeast)
        }
        var draft = IngredientDraft(entry)
        draft.amount = "1"
        if draft.attenuation.isEmpty, let fallback = template.yeastAttenuationPercent {
            draft.attenuation = fallback
        }
        try draft.commit(to: document, recipe: recipe, existing: nil)
    }

    /// Boil hops go in first, in template order, then the aroma hops, so the list reads in the order of the
    /// brew day. Each addition is added at a trial weight and its bitterness read from the change in the
    /// total. That bitterness depends only on the gravity, the time, and the volume, not on the other hops,
    /// so it can be read in any order. Aroma hops are then set to their weight per liter, and each boil hop
    /// to the weight that gives its share of the bitterness the aroma hops leave to reach the target.
    private static func addHops(
        _ template: RecipeTemplate, batchLiters: Double, units: UnitSystem, document: RecipeDocument,
        recipe: UInt64, current: () -> RecipeSnapshot?
    ) throws {
        func hopDraft(_ name: String) throws -> IngredientDraft {
            guard let entry = Catalog.hops.first(where: { $0.name == name }) else {
                throw RecipeBuildError.unknownIngredient(name)
            }
            var draft = IngredientDraft(entry)
            draft.unit = units.smallMassUnit
            return draft
        }

        let trialGrams = 10.0
        /// Adds `draft` at the trial weight, and returns its id and the bitterness it adds per trial weight.
        func addTrial(_ draft: IngredientDraft) throws -> (id: UInt64, gained: Double) {
            var draft = draft
            draft.amount = hopAmount(grams: trialGrams, units: units, rounded: false)
            let before = current()?.results.ibu ?? 0
            try draft.commit(to: document, recipe: recipe, existing: nil)
            guard let id = current()?.hops.last?.id else { throw RecipeBuildError.noBitterness(draft.name) }
            return (id, (current()?.results.ibu ?? 0) - before)
        }

        var boil: [(draft: IngredientDraft, id: UInt64, gained: Double, percent: Double)] = []
        for hop in template.boilHops {
            var draft = try hopDraft(hop.name)
            draft.use = "add_to_boil"
            draft.time = String(hop.minutes)
            draft.timeUnit = "min"
            let trial = try addTrial(draft)
            guard trial.gained > 0.0001 else { throw RecipeBuildError.noBitterness(hop.name) }
            boil.append((draft, trial.id, trial.gained, hop.ibuPercent))
        }

        var aromaIBU = 0.0
        for aroma in template.aromaHops {
            var draft = try hopDraft(aroma.name)
            switch aroma.timing {
            case .boil(let minutes):
                draft.use = "add_to_boil"
                draft.time = String(minutes)
                draft.timeUnit = "min"
            case .flameout:
                draft.use = "add_to_boil"
                draft.time = "0"
                draft.timeUnit = "min"
            case .dryHop(let days):
                draft.use = "add_to_fermentation"
                draft.time = String(days)
                draft.timeUnit = "day"
            }
            let trial = try addTrial(draft)
            let grams = aroma.gramsPerLiter * batchLiters
            draft.amount = hopAmount(grams: grams, units: units)
            try draft.commit(to: document, recipe: recipe, existing: trial.id)
            aromaIBU += trial.gained * grams / trialGrams
        }

        let remaining = template.targetIBU - aromaIBU
        for var hop in boil {
            let wanted = remaining * hop.percent / 100
            hop.draft.amount = hopAmount(grams: trialGrams * wanted / hop.gained, units: units)
            try hop.draft.commit(to: document, recipe: recipe, existing: hop.id)
        }
    }

    // MARK: Amounts

    private static func decimal(_ value: Double, places: Int) -> String {
        String(format: "%.\(places)f", value)
    }

    /// A hop weight in the user's small mass unit: whole grams, or ounces to the nearest 0.05.
    /// `rounded` is false for the trial weight, which only has to be exact enough to measure with.
    private static func hopAmount(grams: Double, units: UnitSystem, rounded: Bool = true) -> String {
        switch units {
        case .metric:
            return rounded ? String(format: "%.0f", max(grams.rounded(), 1)) : decimal(grams, places: 1)
        case .us:
            let ounces = grams / 28.349523125
            return rounded ? decimal(max((ounces * 20).rounded() / 20, 0.05), places: 2) : decimal(ounces, places: 3)
        }
    }
}
