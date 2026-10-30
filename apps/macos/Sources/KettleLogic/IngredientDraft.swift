import Foundation

/// The kinds of ingredient the app edits.
enum IngredientKind: String, Codable, CaseIterable, Identifiable {
    case fermentable, hop, culture, water, miscellaneous

    var id: Self { self }

    var title: String {
        switch self {
        case .fermentable: "Fermentable"
        case .hop: "Hop"
        case .culture: "Culture"
        case .water: "Water"
        case .miscellaneous: "Miscellaneous"
        }
    }
}

/// Every value an ingredient editor shows, as the text its fields hold. A catalog
/// entry, an ingredient in a recipe, and a custom ingredient all convert to a
/// draft, so each starts from the same values. A draft is saved to a recipe with
/// `commit`, or to the user's library as it is.
struct IngredientDraft: Codable {
    var kind: IngredientKind
    var name = ""
    var type = ""
    var form = ""
    var use = ""
    var amount = ""
    var unit = ""
    var alpha = ""
    var beta = ""
    /// What the ingredient costs for each one `priceUnit`, in the recipe's currency. Empty means no price.
    var price = ""
    var priceUnit = ""
    var time = ""
    var timeUnit = ""
    var attenuation = ""
    var yieldValue = ""
    var yieldUnit = "sg"
    var colorValue = ""
    var colorUnit = "SRM"
    var calcium = "0"
    var bicarbonate = "0"
    var sulfate = "0"
    var chloride = "0"
    var sodium = "0"
    var magnesium = "0"
    var waterVolume = ""

    /// A blank ingredient of `kind`, with the defaults a new one starts from.
    init(kind: IngredientKind) {
        self.kind = kind
        switch kind {
        case .fermentable:
            type = fermentableTypes[0]
            unit = UnitSystem.current.bulkMassUnit
            yieldValue = "1.036"
            colorValue = "2"
        case .hop:
            form = hopForms[0]
            use = hopUses[0]
            unit = UnitSystem.current.smallMassUnit
            time = "60"
            timeUnit = hopTimeUnits[0]
        case .culture:
            type = cultureTypes[0]
            form = cultureForms[0]
            amount = "1"
            unit = cultureUnits[0]
        case .water:
            break
        case .miscellaneous:
            type = miscellaneousTypes[0]
            unit = UnitSystem.current.smallMassUnit
        }
        priceUnit = unit
    }

    /// Reads a draft saved by any version of the app. A field a saved library entry predates, such as
    /// the beta acid or the price, keeps the value a new draft starts with.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        self.init(kind: try c.decode(IngredientKind.self, forKey: .kind))
        func text(_ key: CodingKeys, _ current: String) throws -> String {
            try c.decodeIfPresent(String.self, forKey: key) ?? current
        }
        name = try text(.name, name)
        type = try text(.type, type)
        form = try text(.form, form)
        use = try text(.use, use)
        amount = try text(.amount, amount)
        unit = try text(.unit, unit)
        alpha = try text(.alpha, alpha)
        beta = try text(.beta, beta)
        price = try text(.price, price)
        priceUnit = try c.decodeIfPresent(String.self, forKey: .priceUnit) ?? unit
        time = try text(.time, time)
        timeUnit = try text(.timeUnit, timeUnit)
        attenuation = try text(.attenuation, attenuation)
        yieldValue = try text(.yieldValue, yieldValue)
        yieldUnit = try text(.yieldUnit, yieldUnit)
        colorValue = try text(.colorValue, colorValue)
        colorUnit = try text(.colorUnit, colorUnit)
        calcium = try text(.calcium, calcium)
        bicarbonate = try text(.bicarbonate, bicarbonate)
        sulfate = try text(.sulfate, sulfate)
        chloride = try text(.chloride, chloride)
        sodium = try text(.sodium, sodium)
        magnesium = try text(.magnesium, magnesium)
        waterVolume = try text(.waterVolume, waterVolume)
    }

    // MARK: From the things it edits

    init(fermentable f: FermentableAddition) {
        self.init(kind: .fermentable)
        name = f.name
        type = f.type
        amount = f.amount.value
        unit = f.amount.unit
        yieldValue = f.potential?.value ?? ""
        yieldUnit = f.potential?.unit ?? "sg"
        colorValue = f.color.value
        colorUnit = f.color.unit
        adoptPrice(f.price)
    }

    init(hop h: HopAddition) {
        self.init(kind: .hop)
        name = h.name
        alpha = h.alphaAcid.value
        beta = h.betaAcid?.value ?? ""
        amount = h.amount.value
        unit = h.amount.unit
        form = h.form ?? ""
        use = h.use ?? ""
        time = h.time?.value ?? ""
        timeUnit = h.time?.unit ?? hopTimeUnits[0]
        adoptPrice(h.price)
    }

    init(culture c: CultureAddition) {
        self.init(kind: .culture)
        name = c.name
        type = c.type
        form = c.form
        amount = c.amount.value
        unit = c.amount.unit
        attenuation = c.attenuation?.value ?? ""
        adoptPrice(c.price)
    }

    init(water w: WaterAddition) {
        self.init(kind: .water)
        name = w.name
        calcium = w.calcium.value
        bicarbonate = w.bicarbonate.value
        sulfate = w.sulfate.value
        chloride = w.chloride.value
        sodium = w.sodium.value
        magnesium = w.magnesium.value
        waterVolume = w.amount?.value ?? ""
    }

    init(miscellaneous m: Ingredient) {
        self.init(kind: .miscellaneous)
        name = m.name
        type = m.type
        amount = m.amount.value
        unit = m.amount.unit
        adoptPrice(m.price)
    }

    /// Takes a price from an ingredient, or leaves the draft with none. With no price, the unit to
    /// price by is the amount's own.
    private mutating func adoptPrice(_ existing: Price?) {
        price = existing?.amount ?? ""
        priceUnit = existing?.per ?? unit
    }

    // MARK: From the catalog

    init(_ t: FermentableTemplate) {
        self.init(kind: .fermentable)
        name = t.name
        type = t.type
        yieldValue = t.yieldSG
        colorValue = t.colorSRM
    }

    init(_ t: HopTemplate) {
        self.init(kind: .hop)
        name = t.name
        alpha = t.alphaPercent
        beta = t.betaPercent ?? ""
    }

    init(_ t: CultureTemplate) {
        self.init(kind: .culture)
        name = t.name
        type = t.type
        form = t.form
        attenuation = t.attenuationPercent ?? ""
    }

    init(_ t: WaterTemplate) {
        self.init(kind: .water)
        name = t.name
        calcium = t.calcium
        bicarbonate = t.bicarbonate
        sulfate = t.sulfate
        chloride = t.chloride
        sodium = t.sodium
        magnesium = t.magnesium
    }

    init(_ t: MiscellaneousTemplate) {
        self.init(kind: .miscellaneous)
        name = t.name
        type = t.type
        unit = t.unit
    }

    /// This draft with the descriptive values of a catalog or library entry. The
    /// amounts and times already set here are kept, so picking an entry does not
    /// clear what the brewer entered.
    func adopting(_ entry: IngredientDraft) -> IngredientDraft {
        var copy = self
        copy.name = entry.name
        switch kind {
        case .fermentable:
            copy.type = entry.type
            copy.yieldValue = entry.yieldValue
            copy.yieldUnit = entry.yieldUnit
            copy.colorValue = entry.colorValue
            copy.colorUnit = entry.colorUnit
        case .hop:
            copy.alpha = entry.alpha
            copy.beta = entry.beta
        case .culture:
            copy.type = entry.type
            copy.form = entry.form
            copy.attenuation = entry.attenuation
        case .water:
            copy.calcium = entry.calcium
            copy.bicarbonate = entry.bicarbonate
            copy.sulfate = entry.sulfate
            copy.chloride = entry.chloride
            copy.sodium = entry.sodium
            copy.magnesium = entry.magnesium
        case .miscellaneous:
            copy.type = entry.type
            copy.unit = entry.unit
        }
        // A library entry's price is kept when it can price this amount. A catalog entry has none.
        if !trimmed(entry.price).isEmpty, priceUnits(forAmountUnit: unit).contains(entry.priceUnit) {
            copy.price = entry.price
            copy.priceUnit = entry.priceUnit
        }
        return copy
    }

    // MARK: Checks and text

    /// Whether the draft has what it needs. A recipe needs its amounts. A library
    /// entry is a template, so it does not need them.
    func isComplete(quantities: Bool) -> Bool {
        guard !trimmed(name).isEmpty else { return false }
        switch kind {
        case .fermentable:
            return !yieldValue.isEmpty && !colorValue.isEmpty && (!quantities || !amount.isEmpty)
        case .hop:
            return !alpha.isEmpty && (!quantities || !amount.isEmpty)
        case .culture, .miscellaneous:
            return !quantities || !amount.isEmpty
        case .water:
            return [calcium, bicarbonate, sulfate, chloride, sodium, magnesium].allSatisfy { !$0.isEmpty }
        }
    }

    /// The key figures, for a list of library entries.
    var summary: String {
        let parts: [String]
        switch kind {
        case .fermentable:
            parts = [type, "yield \(yieldValue) \(yieldUnit)", "\(colorValue) \(colorUnit)"]
        case .hop:
            parts = [
                "\(alpha)% alpha acid", beta.isEmpty ? nil : "\(beta)% beta acid", form,
                use.isEmpty ? nil : hopUseName(use),
            ].compactMap { $0 }
        case .culture:
            parts = ["\(type) · \(form)", attenuation.isEmpty ? nil : "\(attenuation)% attenuation"].compactMap { $0 }
        case .water:
            parts = ["Ca \(calcium) · Mg \(magnesium) · Na \(sodium) · Cl \(chloride) · SO4 \(sulfate) · HCO3 \(bicarbonate) mg/L"]
        case .miscellaneous:
            parts = [type, "measured in \(unit)"]
        }
        let priced = trimmed(price).isEmpty ? [] : ["\(trimmed(price)) / \(priceUnit)"]
        return (parts + priced).filter { !$0.isEmpty }.joined(separator: " · ")
    }

    // MARK: Saving to a recipe

    /// Adds the ingredient to a recipe, or replaces the recipe's ingredient `existing`
    /// with it. The engine checks every value, and throws with its message if one is refused.
    func commit(to document: RecipeDocument, recipe: UInt64, existing: UInt64?) throws {
        let cleanName = trimmed(name)
        let formValue = form.isEmpty ? nil : form
        let useValue = use.isEmpty ? nil : use
        let hasTime = !trimmed(time).isEmpty
        let timeValue = hasTime ? time : nil
        let timeUnitValue = hasTime ? timeUnit : nil
        let attenuationValue = trimmed(attenuation).isEmpty ? nil : attenuation
        let volumeValue = trimmed(waterVolume).isEmpty ? nil : waterVolume
        let betaValue = trimmed(beta).isEmpty ? nil : beta

        try saveIngredient(
            to: document, recipe: recipe, existing: existing, cleanName: cleanName, formValue: formValue,
            useValue: useValue, timeValue: timeValue, timeUnitValue: timeUnitValue,
            attenuationValue: attenuationValue, volumeValue: volumeValue, betaValue: betaValue)
        try savePrice(to: document, recipe: recipe, existing: existing)
    }

    /// Sets the price of the ingredient just saved: the recipe's ingredient `existing`, or the last one of
    /// this kind when it was added. Water has no price, and an unchanged price is left alone.
    private func savePrice(to document: RecipeDocument, recipe: UInt64, existing: UInt64?) throws {
        guard kind != .water, let snapshot = document.snapshot.recipes.first(where: { $0.id == recipe }) else { return }
        let items: [(id: UInt64, price: Price?)]
        switch kind {
        case .fermentable: items = snapshot.fermentables.map { ($0.id, $0.price) }
        case .hop: items = snapshot.hops.map { ($0.id, $0.price) }
        case .culture: items = snapshot.cultures.map { ($0.id, $0.price) }
        case .miscellaneous: items = snapshot.miscellaneous.map { ($0.id, $0.price) }
        case .water: items = []
        }
        guard let target = existing.flatMap({ id in items.first { $0.id == id } }) ?? items.last else { return }
        let amountText = trimmed(price)
        let wanted = amountText.isEmpty ? nil : Price(amount: amountText, per: priceUnit)
        guard wanted != target.price else { return }
        try document.setPrice(recipe, kind, target.id, amount: wanted?.amount, per: wanted?.per)
    }

    private func saveIngredient(
        to document: RecipeDocument, recipe: UInt64, existing: UInt64?, cleanName: String, formValue: String?,
        useValue: String?, timeValue: String?, timeUnitValue: String?, attenuationValue: String?,
        volumeValue: String?, betaValue: String?
    ) throws {
        switch kind {
        case .fermentable:
            if let existing {
                try document.setFermentable(
                    recipe, existing, name: cleanName, type: type, amount: amount, unit: unit,
                    yieldValue: yieldValue, yieldUnit: yieldUnit, colorValue: colorValue, colorUnit: colorUnit)
            } else {
                try document.addFermentable(
                    recipe, name: cleanName, type: type, amount: amount, unit: unit,
                    yieldValue: yieldValue, yieldUnit: yieldUnit, colorValue: colorValue, colorUnit: colorUnit)
            }
        case .hop:
            if let existing {
                try document.setHop(
                    recipe, existing, name: cleanName, alphaPercent: alpha, betaPercent: betaValue, amount: amount, unit: unit,
                    form: formValue, use: useValue, time: timeValue, timeUnit: timeUnitValue)
            } else {
                try document.addHop(
                    recipe, name: cleanName, alphaPercent: alpha, betaPercent: betaValue, amount: amount, unit: unit,
                    form: formValue, use: useValue, time: timeValue, timeUnit: timeUnitValue)
            }
        case .culture:
            if let existing {
                try document.setCulture(
                    recipe, existing, name: cleanName, type: type, form: form, amount: amount, unit: unit,
                    attenuationPercent: attenuationValue)
            } else {
                try document.addCulture(
                    recipe, name: cleanName, type: type, form: form, amount: amount, unit: unit,
                    attenuationPercent: attenuationValue)
            }
        case .water:
            if let existing {
                try document.setWater(
                    recipe, existing, name: cleanName, calcium: calcium, bicarbonate: bicarbonate, sulfate: sulfate,
                    chloride: chloride, sodium: sodium, magnesium: magnesium, amountLiters: volumeValue)
            } else {
                try document.addWater(
                    recipe, name: cleanName, calcium: calcium, bicarbonate: bicarbonate, sulfate: sulfate,
                    chloride: chloride, sodium: sodium, magnesium: magnesium, amountLiters: volumeValue)
            }
        case .miscellaneous:
            if let existing {
                try document.setMiscellaneous(recipe, existing, name: cleanName, type: type, amount: amount, unit: unit)
            } else {
                try document.addMiscellaneous(recipe, name: cleanName, type: type, amount: amount, unit: unit)
            }
        }
    }
}

/// An entry in the Common ingredients menu: a catalog or library ingredient, with
/// the group it sits under.
struct MenuEntry: Identifiable {
    let id: String
    let group: String
    let name: String
    let draft: IngredientDraft
}

extension IngredientDraft {
    /// The built-in catalog entries of one kind.
    static func catalogEntries(for kind: IngredientKind) -> [MenuEntry] {
        switch kind {
        case .fermentable: entries(Catalog.fermentables) { IngredientDraft($0) }
        case .hop: entries(Catalog.hops) { IngredientDraft($0) }
        case .culture: entries(Catalog.cultures) { IngredientDraft($0) }
        case .water: entries(Catalog.waters) { IngredientDraft($0) }
        case .miscellaneous: entries(Catalog.miscellaneous) { IngredientDraft($0) }
        }
    }

    /// The catalog entries of one kind, then the user's own, under Custom.
    static func menuEntries(for kind: IngredientKind, custom: [CustomIngredient]) -> [MenuEntry] {
        catalogEntries(for: kind) + custom.filter { $0.draft.kind == kind }.map {
            MenuEntry(id: "custom-\($0.id)", group: "Custom", name: $0.draft.name, draft: $0.draft)
        }
    }

    private static func entries<T: CatalogItem>(
        _ items: [T], _ make: (T) -> IngredientDraft
    ) -> [MenuEntry] {
        items.map { MenuEntry(id: $0.id, group: $0.group, name: $0.name, draft: make($0)) }
    }
}

/// Trims spaces from both ends of a name.
func trimmed(_ text: String) -> String {
    text.trimmingCharacters(in: .whitespaces)
}
