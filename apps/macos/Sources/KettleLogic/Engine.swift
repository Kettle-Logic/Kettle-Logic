import CKettleLogic
import Foundation

// The Swift side of the engine. Everything here goes through the C facade. The
// views read `DocumentSnapshot` and never touch the C functions themselves.

/// An exact quantity: the value is decimal text, as the engine writes it, so no
/// value is rounded on its way to the screen.
struct Quantity: Decodable, Hashable {
    let value: String
    let unit: String

    var display: String { "\(value) \(unit)" }
}

/// What an ingredient costs: `amount` in the recipe's currency for each one `per`, such as 4.5 per "kg".
/// The amount is decimal text, as the engine writes it.
struct Price: Decodable, Hashable {
    let amount: String
    let per: String

    var display: String { "\(amount) / \(per)" }
}

/// An ingredient, as the snapshot lists it.
struct Ingredient: Decodable, Identifiable, Hashable {
    let id: UInt64
    /// The identity the cost breakdown refers to.
    let uuid: String
    let name: String
    let type: String
    let amount: Quantity
    let price: Price?
}

/// A fermentable, with its potential and color, which the edit forms show.
struct FermentableAddition: Decodable, Identifiable, Hashable {
    let id: UInt64
    let uuid: String
    let price: Price?
    let name: String
    let type: String
    let amount: Quantity
    let potential: Quantity?
    let color: Quantity
}

/// A hop addition. `use` is a BeerJSON timing use, and `time` is the time in the
/// unit it was entered in.
struct HopAddition: Decodable, Identifiable, Hashable {
    let id: UInt64
    let uuid: String
    let price: Price?
    let name: String
    let alphaAcid: Quantity
    let betaAcid: Quantity?
    let amount: Quantity
    let form: String?
    let use: String?
    let time: Quantity?
}

/// A culture: a yeast, a bacterium, and so on.
struct CultureAddition: Decodable, Identifiable, Hashable {
    let id: UInt64
    let uuid: String
    let price: Price?
    let name: String
    let type: String
    let form: String
    let amount: Quantity
    let attenuation: Quantity?
}

/// A water and its minerals, which are all in mg/L.
struct WaterAddition: Decodable, Identifiable, Hashable {
    let id: UInt64
    let name: String
    let calcium: Quantity
    let bicarbonate: Quantity
    let sulfate: Quantity
    let chloride: Quantity
    let sodium: Quantity
    let magnesium: Quantity
    let amount: Quantity?
}

struct BoilOff: Decodable, Hashable {
    let evaporationRate: Quantity
    let trubLoss: Quantity
}

/// The sugar a cider, mead, or wine needs to reach its own targets.
struct DrinkPlan: Decodable, Hashable {
    let gravityGrams: Double?
    let alcoholGrams: Double?
    let notes: [String]
}

/// What the priced ingredients cost, in the recipe's currency. An ingredient with no price is left
/// out, and `unpriced` counts them.
struct RecipeCost: Decodable, Hashable {
    struct Line: Decodable, Hashable {
        let ingredient: String
        let cost: Double
    }

    let currency: String
    let total: Double
    let perLiter: Double?
    let perGallon: Double?
    let unpriced: Int
    let byIngredient: [Line]

    /// The cost of one ingredient, by its identity, or nil if it has no price.
    func cost(of uuid: String) -> Double? {
        byIngredient.first { $0.ingredient == uuid }?.cost
    }
}

/// The calculated values. A value the engine could not compute is nil, and the
/// reason is in `notes`.
struct Results: Decodable, Hashable {
    let originalGravity: Double?
    let finalGravity: Double?
    let abv: Double?
    let ibu: Double?
    let srm: Double?
    let preBoilLiters: Double?
    let postBoilLiters: Double?
    let sulfiteSo2Ppm: Double?
    let tartaricAcidGramsPerLiter: Double?
    let drink: DrinkPlan?
    /// Present only when at least one ingredient has a price.
    let cost: RecipeCost?
    let notes: [String]
}

struct RecipeSnapshot: Decodable, Identifiable, Hashable {
    let id: UInt64
    let name: String
    let type: String
    let batchSize: Quantity
    let brewhouseEfficiency: Quantity
    let fermentationEfficiency: Quantity
    /// The currency of every ingredient price, as a three-letter code such as "USD".
    let currency: String
    let boilOff: BoilOff
    let fermentables: [FermentableAddition]
    let miscellaneous: [Ingredient]
    let hops: [HopAddition]
    let cultures: [CultureAddition]
    let waters: [WaterAddition]
    /// The folder the recipe is filed in, or nil for the top level.
    let folderId: UInt64?
    /// The BJCP guideline id of the style the recipe is compared against, such as "21A".
    let bjcpStyle: String?
    /// The app's planning inputs for this recipe, as JSON text. See `RecipePlanning`.
    let planning: String
    let results: Results

    /// Cider, mead, and wine are planned from their sugar and are not brewed.
    var isFermentedDrink: Bool { ["cider", "mead", "wine"].contains(type) }
}

/// A folder that groups recipes in the recipe list. The Recently Deleted folder is `trash`.
struct RecipeFolder: Decodable, Identifiable, Hashable {
    let id: UInt64
    let name: String
    let trash: Bool
}

struct DocumentSnapshot: Decodable {
    let source: String
    let recipes: [RecipeSnapshot]
    let folders: [RecipeFolder]
}

/// One step of a brew-day procedure. `minute` is the minute into the boil, for a boil step.
struct ProcedureStep: Codable, Hashable {
    let phase: String
    let minute: Int?
    let text: String
    let warning: Bool
    /// How long the step runs, for a step that needs a timer.
    let timerMinutes: Int?
}

/// A failure reported by the engine, with its message.
struct EngineError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

/// One open recipe. It owns the engine's document, and every change goes through
/// the engine's command stack, so undo and redo cover all edits.
@Observable
final class RecipeDocument {
    private let handle: OpaquePointer

    /// Identifies the document in the sidebar while the app runs. Recipe ids are only unique within one document.
    let id = UUID()
    /// Where the document lives on disk, or nil for a new, unsaved document.
    private(set) var fileURL: URL?
    private(set) var snapshot = DocumentSnapshot(source: "current", recipes: [], folders: [])
    /// The recipes as they were when the document was last saved. A new document has none.
    private(set) var savedRecipes: [RecipeSnapshot] = []
    private(set) var canUndo = false
    private(set) var canRedo = false
    private(set) var undoLabel = ""
    private(set) var redoLabel = ""

    /// The file name without its extension, as the sidebar shows it.
    var displayName: String {
        fileURL?.deletingPathExtension().lastPathComponent ?? "New Recipe"
    }

    private init(handle: OpaquePointer, fileURL: URL?) {
        self.handle = handle
        self.fileURL = fileURL
        refresh()
        if fileURL != nil {
            savedRecipes = snapshot.recipes
        }
    }

    deinit {
        kl_document_free(handle)
    }

    static func makeNew() throws -> RecipeDocument {
        var error: UnsafeMutablePointer<CChar>?
        guard let handle = kl_document_new(&error) else {
            throw Self.takeError(error)
        }
        return RecipeDocument(handle: handle, fileURL: nil)
    }

    static func open(_ url: URL) throws -> RecipeDocument {
        var error: UnsafeMutablePointer<CChar>?
        guard let handle = kl_document_open(url.path, &error) else {
            throw Self.takeError(error)
        }
        return RecipeDocument(handle: handle, fileURL: url)
    }

    func save(to url: URL) throws {
        var error: UnsafeMutablePointer<CChar>?
        let status = kl_document_save(handle, url.path, &error)
        try check(status, error)
        fileURL = url
        refresh()
        savedRecipes = snapshot.recipes
    }

    /// Whether the recipe differs from its last saved copy, or has never been saved.
    func isUnsaved(_ recipe: RecipeSnapshot) -> Bool {
        savedRecipes.first { $0.id == recipe.id } != recipe
    }

    /// Whether a recipe is in Recently Deleted.
    func isDeleted(_ recipe: UInt64) -> Bool {
        guard let folder = snapshot.recipes.first(where: { $0.id == recipe })?.folderId else { return false }
        return snapshot.folders.contains { $0.id == folder && $0.trash }
    }

    // MARK: Commands

    func rename(_ recipe: UInt64, to name: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_recipe_name(handle, recipe, name, &error), error)
        refresh()
    }

    func setType(_ recipe: UInt64, to type: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_recipe_type(handle, recipe, type, &error), error)
        refresh()
    }

    /// `value` is exact decimal text, such as "5.25".
    func setBatchSize(_ recipe: UInt64, value: String, unit: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_batch_size(handle, recipe, value, unit, &error), error)
        refresh()
    }

    func setFermentationEfficiency(_ recipe: UInt64, percent: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_fermentation_efficiency(handle, recipe, percent, &error), error)
        refresh()
    }

    func setBoilOff(_ recipe: UInt64, evaporationPercent: String, trubLossLiters: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_boil_off(handle, recipe, evaporationPercent, trubLossLiters, &error), error)
        refresh()
    }

    /// Runs `work` and records everything it does as one undo step called `label`. If `work` throws,
    /// whatever it changed is taken back and the error is rethrown, so a half-built result never remains.
    func grouped(_ label: String, _ work: () throws -> Void) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_group_begin(handle, &error), error)
        do {
            try work()
        } catch {
            var ignored: UnsafeMutablePointer<CChar>?
            _ = kl_group_cancel(handle, &ignored)
            if let ignored { kl_string_free(ignored) }
            refresh()
            throw error
        }
        try check(kl_group_end(handle, label, &error), error)
        refresh()
    }

    /// Adds a recipe with a default name, and returns its id.
    func addRecipe() throws -> UInt64 {
        var error: UnsafeMutablePointer<CChar>?
        var created: kl_recipe_id = 0
        try check(kl_add_recipe(handle, &created, &error), error)
        refresh()
        return created
    }

    /// The amounts and the yield and color are exact decimal text, as typed.
    func addFermentable(_ recipe: UInt64, name: String, type: String, amount: String, unit: String,
                        yieldValue: String, yieldUnit: String, colorValue: String, colorUnit: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_add_fermentable(handle, recipe, name, type, amount, unit, yieldValue, yieldUnit, colorValue, colorUnit,
                               &error),
            error)
        refresh()
    }

    func setFermentable(_ recipe: UInt64, _ fermentable: UInt64, name: String, type: String, amount: String,
                        unit: String, yieldValue: String, yieldUnit: String, colorValue: String,
                        colorUnit: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_set_fermentable(handle, recipe, fermentable, name, type, amount, unit, yieldValue, yieldUnit,
                               colorValue, colorUnit, &error),
            error)
        refresh()
    }

    func setHop(_ recipe: UInt64, _ hop: UInt64, name: String, alphaPercent: String, betaPercent: String?,
                amount: String, unit: String, form: String?, use: String?, time: String?, timeUnit: String?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_set_hop(
                handle, recipe, hop, name, alphaPercent, betaPercent, amount, unit, form, use, time, timeUnit, &error),
            error)
        refresh()
    }

    func setCulture(_ recipe: UInt64, _ culture: UInt64, name: String, type: String, form: String, amount: String,
                    unit: String, attenuationPercent: String?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_set_culture(handle, recipe, culture, name, type, form, amount, unit, attenuationPercent, &error),
            error)
        refresh()
    }

    func setWater(_ recipe: UInt64, _ water: UInt64, name: String, calcium: String, bicarbonate: String,
                  sulfate: String, chloride: String, sodium: String, magnesium: String,
                  amountLiters: String?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_set_water(handle, recipe, water, name, calcium, bicarbonate, sulfate, chloride, sodium, magnesium,
                         amountLiters, &error),
            error)
        refresh()
    }

    func setMiscellaneous(_ recipe: UInt64, _ miscellaneous: UInt64, name: String, type: String, amount: String,
                          unit: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_miscellaneous(handle, recipe, miscellaneous, name, type, amount, unit, &error), error)
        refresh()
    }

    /// Sets the batch size, brewhouse efficiency, and boil-off, as one undo step. The
    /// evaporation is a percentage per hour, and the trub loss is in liters.
    func applyEquipment(_ recipe: UInt64, batchValue: String, batchUnit: String, efficiencyPercent: String,
                        evaporationPercent: String, trubLossLiters: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_apply_equipment(handle, recipe, batchValue, batchUnit, efficiencyPercent, evaporationPercent,
                               trubLossLiters, &error),
            error)
        refresh()
    }

    /// A brew-day procedure for the recipe, given its boil length in minutes and its
    /// mash system, "separate tun" or "all in one".
    func procedure(_ recipe: UInt64, boilMinutes: Int, system: String) throws -> [ProcedureStep] {
        var error: UnsafeMutablePointer<CChar>?
        guard let json = kl_procedure_json(handle, recipe, Int32(boilMinutes), system, &error) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(json) }
        return try JSONDecoder().decode([ProcedureStep].self, from: Data(String(cString: json).utf8))
    }

    /// Deletes a recipe for good. Undo puts it back.
    func removeRecipe(_ recipe: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_remove_recipe(handle, recipe, &error), error)
        refresh()
    }

    /// Moves a recipe to Recently Deleted. Undo files it back where it was.
    func trashRecipe(_ recipe: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_trash_recipe(handle, recipe, &error), error)
        refresh()
    }

    /// Copies a recipe after the original, named "<name> copy", and returns the copy's id.
    func duplicateRecipe(_ recipe: UInt64) throws -> UInt64 {
        var error: UnsafeMutablePointer<CChar>?
        var created: kl_recipe_id = 0
        try check(kl_duplicate_recipe(handle, recipe, &created, &error), error)
        refresh()
        return created
    }

    /// Copies an ingredient within its recipe, after the original.
    func duplicateIngredient(_ kind: IngredientKind, _ item: UInt64, in recipe: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        let status: Int32
        switch kind {
        case .fermentable: status = kl_duplicate_fermentable(handle, recipe, item, &error)
        case .hop: status = kl_duplicate_hop(handle, recipe, item, &error)
        case .culture: status = kl_duplicate_culture(handle, recipe, item, &error)
        case .water: status = kl_duplicate_water(handle, recipe, item, &error)
        case .miscellaneous: status = kl_duplicate_miscellaneous(handle, recipe, item, &error)
        }
        try check(status, error)
        refresh()
    }

    func removeFermentable(_ recipe: UInt64, _ fermentable: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_remove_fermentable(handle, recipe, fermentable, &error), error)
        refresh()
    }

    func addMiscellaneous(_ recipe: UInt64, name: String, type: String, amount: String, unit: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_add_miscellaneous(handle, recipe, name, type, amount, unit, &error), error)
        refresh()
    }

    func removeMiscellaneous(_ recipe: UInt64, _ miscellaneous: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_remove_miscellaneous(handle, recipe, miscellaneous, &error), error)
        refresh()
    }

    /// `time` and `timeUnit` are both given or both nil. `form` and `use` may be nil.
    func addHop(_ recipe: UInt64, name: String, alphaPercent: String, betaPercent: String?, amount: String,
                unit: String, form: String?, use: String?, time: String?, timeUnit: String?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_add_hop(
                handle, recipe, name, alphaPercent, betaPercent, amount, unit, form, use, time, timeUnit, &error),
            error)
        refresh()
    }

    /// Sets what an ingredient costs: `amount` for each one `per`. Both nil clears the price. `per` must
    /// measure the same thing as the ingredient's amount, or the engine refuses.
    func setPrice(_ recipe: UInt64, _ kind: IngredientKind, _ ingredient: UInt64, amount: String?, per: String?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_price(handle, recipe, kind.rawValue, ingredient, amount, per, &error), error)
        refresh()
    }

    /// Sets the currency the recipe's prices are in, a three-letter code.
    func setRecipeCurrency(_ recipe: UInt64, _ currency: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_recipe_currency(handle, recipe, currency, &error), error)
        refresh()
    }

    func removeHop(_ recipe: UInt64, _ hop: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_remove_hop(handle, recipe, hop, &error), error)
        refresh()
    }

    func addCulture(_ recipe: UInt64, name: String, type: String, form: String, amount: String, unit: String,
                    attenuationPercent: String?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_add_culture(handle, recipe, name, type, form, amount, unit, attenuationPercent, &error), error)
        refresh()
    }

    func removeCulture(_ recipe: UInt64, _ culture: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_remove_culture(handle, recipe, culture, &error), error)
        refresh()
    }

    /// Each mineral is exact decimal text in mg/L. `amountLiters` may be nil.
    func addWater(_ recipe: UInt64, name: String, calcium: String, bicarbonate: String, sulfate: String,
                  chloride: String, sodium: String, magnesium: String, amountLiters: String?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(
            kl_add_water(handle, recipe, name, calcium, bicarbonate, sulfate, chloride, sodium, magnesium,
                         amountLiters, &error),
            error)
        refresh()
    }

    func removeWater(_ recipe: UInt64, _ water: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_remove_water(handle, recipe, water, &error), error)
        refresh()
    }

    func undo() throws {
        var error: UnsafeMutablePointer<CChar>?
        let status = kl_undo(handle, &error)
        if status < 0 { throw Self.takeError(error) }
        kl_string_free(error)
        refresh()
    }

    func redo() throws {
        var error: UnsafeMutablePointer<CChar>?
        let status = kl_redo(handle, &error)
        if status < 0 { throw Self.takeError(error) }
        kl_string_free(error)
        refresh()
    }

    // MARK: Planning

    /// Stores the app's planning JSON for the recipe. The engine keeps it as it is.
    func setPlanning(_ recipe: UInt64, to planning: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_recipe_planning(handle, recipe, planning, &error), error)
        refresh()
    }

    /// The brewing water, mash, and sparge plan for the recipe.
    func brewingPlan(_ recipe: UInt64, _ inputs: BrewingInputs) throws -> BrewingPlan {
        var error: UnsafeMutablePointer<CChar>?
        let data = try JSONEncoder().encode(inputs)
        guard let json = kl_brewing_plan_json(handle, recipe, String(decoding: data, as: UTF8.self), &error) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(json) }
        return try JSONDecoder().decode(BrewingPlan.self, from: Data(String(cString: json).utf8))
    }

    /// The brew sheet for the recipe: its boxes, in order, with the figures the recipe expects.
    func brewSheet(_ recipe: UInt64, imperial: Bool) throws -> BrewSheetLayout {
        var error: UnsafeMutablePointer<CChar>?
        guard let json = kl_brew_sheet_json(handle, recipe, imperial ? 1 : 0, &error) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(json) }
        return try JSONDecoder().decode(BrewSheetLayout.self, from: Data(String(cString: json).utf8))
    }

    /// Writes the recipe's brew sheet to a PDF file.
    func writeBrewSheetPDF(_ recipe: UInt64, imperial: Bool, to url: URL) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_brew_sheet_write_pdf(handle, recipe, imperial ? 1 : 0, url.path, &error), error)
    }

    /// Reads the text typed into a brew sheet's boxes, keyed by box code.
    func readBrewSheet(_ recipe: UInt64, imperial: Bool, entries: [String: String]) throws -> BrewSheetReading {
        var error: UnsafeMutablePointer<CChar>?
        let data = try JSONEncoder().encode(entries)
        guard let json = kl_brew_sheet_read_json(handle, recipe, imperial ? 1 : 0, String(decoding: data, as: UTF8.self), &error) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(json) }
        return try JSONDecoder().decode(BrewSheetReading.self, from: Data(String(cString: json).utf8))
    }

    /// The brew journal's expected and measured figures for the recipe.
    func journalPlan(_ recipe: UInt64, _ measured: JournalRequest) throws -> JournalPlan {
        var error: UnsafeMutablePointer<CChar>?
        let data = try JSONEncoder().encode(measured)
        guard let json = kl_journal_plan_json(handle, recipe, String(decoding: data, as: UTF8.self), &error) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(json) }
        return try JSONDecoder().decode(JournalPlan.self, from: Data(String(cString: json).utf8))
    }

    /// The yeast plan for the recipe, sized from its batch and original gravity.
    func starterPlan(_ recipe: UInt64, _ settings: StarterSettings) throws -> StarterPlan {
        var error: UnsafeMutablePointer<CChar>?
        guard let json = kl_starter_plan_json(
            handle, recipe, settings.lager ? 1 : 0,
            settings.packCells, settings.growth, settings.starterLiters, settings.starterGravity,
            settings.twoStage ? 1 : 0, settings.firstStageLiters, settings.addToBottling ? 1 : 0,
            &error
        ) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(json) }
        return try JSONDecoder().decode(StarterPlan.self, from: Data(String(cString: json).utf8))
    }

    // MARK: Styles and folders

    /// Sets the BJCP style a recipe is compared against, by guideline id, or nil for none.
    /// The recipe as text in an export format: "beerxml", "beerjson", or "text".
    func export(_ recipe: UInt64, format: String) throws -> String {
        var error: UnsafeMutablePointer<CChar>?
        guard let text = kl_recipe_export(handle, recipe, format, &error) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(text) }
        return String(cString: text)
    }

    /// Writes the recipe to a PDF file: its estimates, ingredients, mash, fermentation, and notes. It is not the brew sheet.
    func writeRecipePDF(_ recipe: UInt64, to url: URL) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_recipe_write_pdf(handle, recipe, url.path, &error), error)
    }

    func setStyle(_ recipe: UInt64, to style: String?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_recipe_style(handle, recipe, style, &error), error)
        refresh()
    }

    /// The styles the user defined, in the order they were made.
    func userStyles() throws -> [UserStyle] {
        var error: UnsafeMutablePointer<CChar>?
        guard let json = kl_user_styles_json(handle, &error) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(json) }
        return try JSONDecoder().decode([UserStyle].self, from: Data(String(cString: json).utf8))
    }

    /// Adds a style the user defined, and returns its new id.
    func addUserStyle(_ style: UserStyle) throws -> String {
        var error: UnsafeMutablePointer<CChar>?
        var created: UnsafeMutablePointer<CChar>?
        try check(kl_add_user_style(handle, try style.engineJSON(), &created, &error), error)
        refresh()
        guard let created else { throw EngineError(message: "The engine did not return the new style.") }
        defer { kl_string_free(created) }
        return String(cString: created)
    }

    /// Replaces the user style with the same id.
    func updateUserStyle(_ style: UserStyle) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_set_user_style(handle, style.id, try style.engineJSON(), &error), error)
        refresh()
    }

    /// Removes a user style. Recipes compared against it are left with no style.
    func removeUserStyle(_ id: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_remove_user_style(handle, id, &error), error)
        refresh()
    }

    /// Adds a folder, and returns its id.
    func addFolder(named name: String) throws -> UInt64 {
        var error: UnsafeMutablePointer<CChar>?
        var created: kl_folder_id = 0
        try check(kl_add_folder(handle, name, &created, &error), error)
        refresh()
        return created
    }

    func renameFolder(_ folder: UInt64, to name: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_rename_folder(handle, folder, name, &error), error)
        refresh()
    }

    /// Removes a folder. Its recipes move to the top level. Undo puts them back.
    func removeFolder(_ folder: UInt64) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_remove_folder(handle, folder, &error), error)
        refresh()
    }

    /// Files a recipe in a folder, or at the top level when `folder` is nil.
    func moveRecipe(_ recipe: UInt64, toFolder folder: UInt64?) throws {
        var error: UnsafeMutablePointer<CChar>?
        try check(kl_move_recipe_to_folder(handle, recipe, folder ?? 0, &error), error)
        refresh()
    }

    /// The recipe's values placed against its style, or nil when it has no style.
    func styleComparison(_ recipe: UInt64) throws -> StyleComparison? {
        var error: UnsafeMutablePointer<CChar>?
        guard let json = kl_style_comparison_json(handle, recipe, &error) else {
            throw Self.takeError(error)
        }
        defer { kl_string_free(json) }
        let text = String(cString: json)
        if text == "null" { return nil }
        return try JSONDecoder().decode(StyleComparison.self, from: Data(text.utf8))
    }

    // MARK: Reading

    /// Reloads the snapshot and the undo state after every change.
    private func refresh() {
        if let json = kl_document_snapshot_json(handle) {
            defer { kl_string_free(json) }
            do {
                snapshot = try JSONDecoder().decode(DocumentSnapshot.self, from: Data(String(cString: json).utf8))
            } catch {
                assertionFailure("the engine returned a snapshot the app cannot read: \(error)")
            }
        }
        canUndo = kl_can_undo(handle) == 1
        canRedo = kl_can_redo(handle) == 1
        undoLabel = takeString(kl_undo_label(handle))
        redoLabel = takeString(kl_redo_label(handle))
    }

    // MARK: Helpers

    private func check(_ status: Int32, _ error: UnsafeMutablePointer<CChar>?) throws {
        if status != 0 {
            throw Self.takeError(error)
        }
        kl_string_free(error)
    }

    private func takeString(_ pointer: UnsafeMutablePointer<CChar>?) -> String {
        guard let pointer else { return "" }
        defer { kl_string_free(pointer) }
        return String(cString: pointer)
    }

    private static func takeError(_ error: UnsafeMutablePointer<CChar>?) -> EngineError {
        guard let error else { return EngineError(message: "The engine failed without a message.") }
        defer { kl_string_free(error) }
        return EngineError(message: String(cString: error))
    }
}

/// A BJCP 2021 beer style, with the range each vital statistic falls in.
struct BJCPStyle: Decodable, Identifiable, Hashable {
    struct Range: Decodable, Hashable {
        let low: Double
        let high: Double
    }

    let id: String
    let name: String
    let og: Range
    let fg: Range
    let ibu: Range
    let srm: Range
    let abv: Range

    /// The name shown in the style picker, such as "21A American IPA".
    var title: String { "\(id) \(name)" }
}

/// A range of one vital statistic, as the engine stores it.
struct StyleRange: Codable, Hashable {
    var low: Double
    var high: Double
}

/// A beer style the user defined. Its `id` begins with "custom-", and it is empty until the engine assigns one.
struct UserStyle: Codable, Identifiable, Hashable {
    var id: String
    var name: String
    var og: StyleRange
    var fg: StyleRange
    var ibu: StyleRange
    var srm: StyleRange
    var abv: StyleRange

    /// A style with no name and empty ranges, for a new style.
    static let blank = UserStyle(id: "", name: "", og: StyleRange(low: 0, high: 0), fg: StyleRange(low: 0, high: 0),
                                 ibu: StyleRange(low: 0, high: 0), srm: StyleRange(low: 0, high: 0),
                                 abv: StyleRange(low: 0, high: 0))

    /// The JSON the engine reads for a style: the name and the five ranges, as the facade expects them.
    func engineJSON() throws -> String {
        let data = try JSONEncoder().encode(self)
        return String(decoding: data, as: UTF8.self)
    }
}

/// Every BJCP style the engine knows, in guideline order.
let bjcpStyles: [BJCPStyle] = {
    guard let json = kl_styles_json() else { return [] }
    defer { kl_string_free(json) }
    return (try? JSONDecoder().decode([BJCPStyle].self, from: Data(String(cString: json).utf8))) ?? []
}()

/// A recipe's values placed against its style. `checks` are in the order original gravity,
/// final gravity, alcohol, bitterness, and color.
struct StyleComparison: Decodable, Hashable {
    struct Reference: Decodable, Hashable {
        let id: String
        let name: String
    }

    let style: Reference
    let checks: [StyleCheck]
}

/// One calculated value against its style's range. `value` is nil when the engine has none,
/// and `fit` is "below", "within", "above", or "unknown".
struct StyleCheck: Decodable, Identifiable, Hashable {
    let metric: String
    let value: Double?
    let low: Double
    let high: Double
    let fit: String

    var id: String { metric }
}

/// The choices the recipe and ingredient pickers offer, by BeerJSON name, from the engine. The first entry of a list
/// is the default. A recipe keeps any other value it was given, so a picker adds the current one.
struct BuiltinOptions: Decodable {
    let mashStepKinds: [String]
    let carbonationMethods: [String]
    let currencyCodes: [String]
    let recipeTypes: [String]
    let fermentableTypes: [String]
    let fermentableUnits: [String]
    let miscellaneousTypes: [String]
    let miscellaneousUnits: [String]
    let hopForms: [String]
    let hopUses: [String]
    let hopUnits: [String]
    let hopTimeUnits: [String]
    let cultureTypes: [String]
    let cultureForms: [String]
    let cultureUnits: [String]
}

let builtinOptions = BuiltinData.load("options", as: BuiltinOptions.self)

/// The currencies the recipe's currency picker offers. A recipe keeps any other code it was given.
let currencyCodes = builtinOptions.currencyCodes

/// The recipe types a user can pick.
let recipeTypes = builtinOptions.recipeTypes

/// The fermentable types and the units a fermentable amount can use.
let fermentableTypes = builtinOptions.fermentableTypes
let fermentableUnits = builtinOptions.fermentableUnits

/// The miscellaneous types and the units a miscellaneous amount can use.
let miscellaneousTypes = builtinOptions.miscellaneousTypes
let miscellaneousUnits = builtinOptions.miscellaneousUnits

/// The hop forms, uses, and units. A use also has a name to show.
let hopForms = builtinOptions.hopForms
let hopUnits = builtinOptions.hopUnits
let hopTimeUnits = builtinOptions.hopTimeUnits
let hopUses = builtinOptions.hopUses

func hopUseName(_ use: String) -> String {
    switch use {
    case "add_to_boil": "Boil"
    case "add_to_mash": "Mash"
    case "add_to_fermentation": "Fermentation"
    case "add_to_package": "Package"
    case "": "Not set"
    default: use
    }
}

/// The culture types, forms, and amount units.
let cultureTypes = builtinOptions.cultureTypes
let cultureForms = builtinOptions.cultureForms
let cultureUnits = builtinOptions.cultureUnits

/// The units a price can be per, for an ingredient measured in `amountUnit`. A price has to measure the
/// same thing as the amount, so a price per kg suits an amount in lb, and a price per package suits a count.
/// The engine owns the rule (`catalog::priceUnits`).
func priceUnits(forAmountUnit amountUnit: String) -> [String] {
    guard let json = kl_price_units_json(amountUnit) else { return [amountUnit] }
    defer { kl_string_free(json) }
    return (try? JSONDecoder().decode([String].self, from: Data(String(cString: json).utf8))) ?? [amountUnit]
}
