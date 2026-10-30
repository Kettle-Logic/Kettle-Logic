import SwiftUI

// One editor for every kind of ingredient. The caller decides what saving does: a
// recipe's ingredient is added or changed in the engine, and a library entry is
// saved to the user's library. A refused value is shown in the editor, which
// stays open so the value can be fixed.

/// A right-aligned field for a typed number, which the engine reads as exact decimal text.
struct NumberField: View {
    let title: String
    @Binding var text: String

    var body: some View {
        TextField(title, text: $text)
            .multilineTextAlignment(.trailing)
            .frame(width: 100)
    }
}

/// A unit picker beside a number field.
struct UnitPicker: View {
    let units: [String]
    @Binding var unit: String

    var body: some View {
        Picker("Unit", selection: $unit) {
            ForEach(units, id: \.self) { Text($0).tag($0) }
        }
        .labelsHidden()
        .frame(width: 90)
    }
}

/// The frame the editor shares with any other form: a form with a title, Cancel and
/// save buttons, and any error.
struct EditorFrame<Content: View>: View {
    let title: String
    let saveTitle: String
    let canSave: Bool
    let errorMessage: String?
    let save: () -> Void
    @ViewBuilder let content: Content

    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                content
                if let errorMessage {
                    Text(errorMessage).foregroundStyle(.red)
                }
            }
            .formStyle(.grouped)
            .textFieldStyle(.roundedBorder)
            .navigationTitle(title)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(saveTitle, action: save).disabled(!canSave)
                }
            }
        }
        .frame(minWidth: 440)
    }
}

/// Edits one ingredient. `seed` is the starting draft, and `menu` offers common
/// ingredients to start from. `showsQuantities` turns the amount and time fields on
/// or off, since a library entry is a template and has no amount.
struct IngredientEditor: View {
    let title: String
    let saveTitle: String
    let menu: [MenuEntry]
    let showsQuantities: Bool
    /// The currency a price is in, when the ingredient belongs to a recipe. A library entry has none.
    let currency: String?
    let save: (IngredientDraft) throws -> Void

    @State private var draft: IngredientDraft
    @State private var errorMessage: String?
    @State private var showsMoreDetails = false
    @State private var showsPicker = false
    /// Set when the user picks Custom for a hop's timing, so the custom fields stay even when the values
    /// happen to match a preset.
    @State private var customTiming = false
    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true
    @Environment(\.dismiss) private var dismiss

    init(
        title: String, saveTitle: String, seed: IngredientDraft, menu: [MenuEntry] = [],
        showsQuantities: Bool = true, currency: String? = nil, save: @escaping (IngredientDraft) throws -> Void
    ) {
        self.title = title
        self.saveTitle = saveTitle
        self.menu = menu
        self.showsQuantities = showsQuantities
        self.currency = currency
        self.save = save
        _draft = State(initialValue: seed)
    }

    var body: some View {
        EditorFrame(
            title: title,
            saveTitle: saveTitle,
            canSave: draft.isComplete(quantities: showsQuantities),
            errorMessage: errorMessage,
            save: saveDraft
        ) {
            if menu.count >= ingredientPickerThreshold {
                Button("Choose a Common Ingredient…", systemImage: "magnifyingglass") { showsPicker = true }
                    .popover(isPresented: $showsPicker, arrowEdge: .bottom) {
                        IngredientPicker(kind: draft.kind, entries: menu) { entry in
                            draft = draft.adopting(entry.draft)
                            showsPicker = false
                        }
                    }
            } else if !menu.isEmpty {
                Menu("Common ingredients") {
                    ForEach(menuGroups, id: \.self) { group in
                        Section(group) {
                            ForEach(menu.filter { $0.group == group }) { entry in
                                Button(entry.name) { draft = draft.adopting(entry.draft) }
                            }
                        }
                    }
                }
            }

            TextField("Name", text: $draft.name)

            switch draft.kind {
            case .fermentable: fermentableFields
            case .hop: hopFields
            case .culture: cultureFields
            case .water: waterFields
            case .miscellaneous: miscellaneousFields
            }

            if draft.kind != .water { priceFields }
        }
        // A price per kg fits an amount in lb, but not one in liters, so a new amount unit keeps the price unit sensible.
        .onChange(of: draft.unit) {
            if !priceUnits(forAmountUnit: draft.unit).contains(draft.priceUnit) {
                draft.priceUnit = draft.unit
            }
        }
    }

    @ViewBuilder private var priceFields: some View {
        LabeledContent {
            HStack {
                NumberField(title: "Price", text: $draft.price)
                UnitPicker(
                    units: options(priceUnits(forAmountUnit: draft.unit), including: draft.priceUnit),
                    unit: $draft.priceUnit)
            }
        } label: {
            Text("Price per" + (currency.map { " (\($0))" } ?? ""))
        }
        if !draft.price.isEmpty || !simpleMode {
            Text("Optional. With a price on its ingredients, the recipe shows what a batch costs.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private var menuGroups: [String] {
        var seen = Set<String>()
        return menu.map(\.group).filter { seen.insert($0).inserted }
    }

    @ViewBuilder private var fermentableFields: some View {
        Picker("Type", selection: $draft.type) {
            ForEach(options(fermentableTypes, including: draft.type), id: \.self) { Text($0).tag($0) }
        }
        if showsQuantities {
            LabeledContent("Amount") {
                HStack {
                    AmountField(text: $draft.amount, unit: draft.unit)
                    UnitPicker(units: options(fermentableUnits, including: draft.unit), unit: $draft.unit)
                }
            }
        }
        advanced {
            LabeledContent("Yield") {
                HStack {
                    NumberField(title: "Yield", text: $draft.yieldValue)
                    UnitPicker(units: options(["sg", "plato", "brix"], including: draft.yieldUnit), unit: $draft.yieldUnit)
                }
            }
            LabeledContent("Color") {
                HStack {
                    NumberField(title: "Color", text: $draft.colorValue)
                    UnitPicker(units: options(["SRM", "EBC", "Lovi"], including: draft.colorUnit), unit: $draft.colorUnit)
                }
            }
        }
    }

    @ViewBuilder private var hopFields: some View {
        LabeledContent {
            NumberField(title: "Alpha acid", text: $draft.alpha)
        } label: {
            HelpLabel("Alpha acid (%)", term: .alphaAcid)
        }
        advanced {
            LabeledContent {
                NumberField(title: "Beta acid", text: $draft.beta)
            } label: {
                HelpLabel("Beta acid (%)", term: .betaAcid)
            }
            Picker("Form", selection: $draft.form) {
                ForEach(options([""] + hopForms, including: draft.form), id: \.self) { Text(hopFormName($0)).tag($0) }
            }
        }
        if simpleMode && showsQuantities {
            Picker("When to add", selection: timingChoice) {
                ForEach(HopTiming.allCases) { Text($0.title).tag($0) }
            }
            if timing == .dryHop {
                LabeledContent("Days in the fermenter") { NumberField(title: "Days", text: $draft.time) }
            }
        }
        if !(simpleMode && showsQuantities) || timing == .custom {
            Picker("Added to", selection: $draft.use) {
                ForEach(options([""] + hopUses, including: draft.use), id: \.self) { Text(hopUseName($0)).tag($0) }
            }
        }
        if showsQuantities {
            LabeledContent("Amount") {
                HStack {
                    AmountField(text: $draft.amount, unit: draft.unit)
                    UnitPicker(units: options(hopUnits, including: draft.unit), unit: $draft.unit)
                }
            }
            if !simpleMode || timing == .custom {
                LabeledContent("Time") {
                    HStack {
                        NumberField(title: "Time", text: $draft.time)
                        UnitPicker(units: options(hopTimeUnits, including: draft.timeUnit), unit: $draft.timeUnit)
                    }
                }
            }
            Text(
                simpleMode && timing != .custom
                    ? "Hops added early make the beer bitter. Hops added late, or after the boil, add aroma instead."
                    : "A boil hop is timed in minutes before the end of the boil, and counts toward bitterness. "
                        + "Other additions are timed from the start of that step. Use a whole number, "
                        + "or leave the time empty."
            )
            .font(.caption)
            .foregroundStyle(.secondary)
        }
    }

    // MARK: Hop timing

    /// The timing a new brewer picks from, which sets the hop's use, time, and time unit together.
    private var timing: HopTiming {
        if customTiming { return .custom }
        let time = trimmed(draft.time)
        switch (draft.use, draft.timeUnit) {
        case ("add_to_boil", "min"):
            if time == "0" { return .flameout }
            if let minutes = Int(time), let preset = HopTiming.boilPresets.first(where: { $0.minutes == minutes }) {
                return preset
            }
        case ("add_to_fermentation", "day"):
            if Int(time) != nil { return .dryHop }
        default:
            break
        }
        return .custom
    }

    private var timingChoice: Binding<HopTiming> {
        Binding(
            get: { timing },
            set: { choice in
                customTiming = choice == .custom
                guard let setting = choice.setting else { return }
                // Moving to a dry hop from anything else starts at three days, not at the old boil minutes.
                let wasDryHop = draft.use == setting.use && Int(trimmed(draft.time)) != nil
                draft.use = setting.use
                draft.timeUnit = setting.unit
                if let time = setting.time {
                    draft.time = time
                } else if !wasDryHop {
                    draft.time = "3"
                }
            })
    }

    @ViewBuilder private var cultureFields: some View {
        Picker("Type", selection: $draft.type) {
            ForEach(options(cultureTypes, including: draft.type), id: \.self) { Text($0).tag($0) }
        }
        advanced {
            Picker("Form", selection: $draft.form) {
                ForEach(options(cultureForms, including: draft.form), id: \.self) { Text($0).tag($0) }
            }
            LabeledContent {
                NumberField(title: "Attenuation", text: $draft.attenuation)
            } label: {
                HelpLabel("Attenuation (%)", term: .attenuation)
            }
        }
        if showsQuantities {
            LabeledContent("Amount") {
                HStack {
                    AmountField(text: $draft.amount, unit: draft.unit)
                    UnitPicker(units: options(cultureUnits, including: draft.unit), unit: $draft.unit)
                }
            }
        }
        Text(
            "The first culture with an attenuation sets the final gravity. The figure is typical for "
                + "the strain, so check it against the packet. Leave it empty if you do not know it."
        )
        .font(.caption)
        .foregroundStyle(.secondary)
    }

    @ViewBuilder private var waterFields: some View {
        Section("Minerals (mg/L)") {
            LabeledContent("Calcium") { NumberField(title: "Calcium", text: $draft.calcium) }
            LabeledContent("Magnesium") { NumberField(title: "Magnesium", text: $draft.magnesium) }
            LabeledContent("Sodium") { NumberField(title: "Sodium", text: $draft.sodium) }
            LabeledContent("Chloride") { NumberField(title: "Chloride", text: $draft.chloride) }
            LabeledContent("Sulfate") { NumberField(title: "Sulfate", text: $draft.sulfate) }
            LabeledContent("Bicarbonate") { NumberField(title: "Bicarbonate", text: $draft.bicarbonate) }
        }
        if showsQuantities {
            LabeledContent("Volume (L)") { NumberField(title: "Volume", text: $draft.waterVolume) }
        }
        Text(
            "City profiles are commonly published figures. A real water changes with the season "
                + "and the source, so test your own."
        )
        .font(.caption)
        .foregroundStyle(.secondary)
    }

    @ViewBuilder private var miscellaneousFields: some View {
        Picker("Type", selection: $draft.type) {
            ForEach(options(miscellaneousTypes, including: draft.type), id: \.self) { Text($0).tag($0) }
        }
        if showsQuantities {
            LabeledContent("Amount") {
                HStack {
                    AmountField(text: $draft.amount, unit: draft.unit)
                    UnitPicker(units: options(miscellaneousUnits, including: draft.unit), unit: $draft.unit)
                }
            }
        }
    }

    /// The technical fields. Simple mode folds them into a closed group, since the common-ingredient
    /// menu fills them in; otherwise they show in place.
    @ViewBuilder private func advanced<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        let fields = content()
        if simpleMode {
            DisclosureGroup("More details", isExpanded: $showsMoreDetails) { fields }
        } else {
            fields
        }
    }

    private func saveDraft() {
        do {
            try save(draft)
            dismiss()
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}

/// The ways a new brewer thinks about when a hop goes in. Each sets the hop's use, time, and time unit.
enum HopTiming: Hashable, Identifiable, CaseIterable {
    case start, thirty, fifteen, five, flameout, dryHop, custom

    var id: Self { self }

    static let boilPresets: [HopTiming] = [.start, .thirty, .fifteen, .five]

    /// Minutes left in the boil, for the boil presets.
    var minutes: Int {
        switch self {
        case .start: 60
        case .thirty: 30
        case .fifteen: 15
        case .five: 5
        default: 0
        }
    }

    var title: String {
        switch self {
        case .start: "Start of boil (60 min left)"
        case .thirty: "30 minutes left"
        case .fifteen: "15 minutes left"
        case .five: "5 minutes left"
        case .flameout: "Flameout (end of boil)"
        case .dryHop: "Dry hop (in the fermenter)"
        case .custom: "Custom…"
        }
    }

    /// What picking this sets. A nil time keeps the time already there, which the dry hop does for its days.
    var setting: (use: String, time: String?, unit: String)? {
        switch self {
        case .start, .thirty, .fifteen, .five: ("add_to_boil", String(minutes), "min")
        case .flameout: ("add_to_boil", "0", "min")
        case .dryHop: ("add_to_fermentation", nil, "day")
        case .custom: nil
        }
    }
}

/// A number field for an amount. Simple mode adds a stepper beside it, so a brewer can nudge the amount
/// without typing. The step suits the unit: a few grams, a quarter ounce, or half a pound.
struct AmountField: View {
    @Binding var text: String
    let unit: String

    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true

    var body: some View {
        HStack(spacing: 4) {
            NumberField(title: "Amount", text: $text)
            if simpleMode {
                Stepper("Amount", onIncrement: { nudge(1) }, onDecrement: { nudge(-1) })
                    .labelsHidden()
            }
        }
    }

    private var step: Double {
        switch unit {
        case "g", "ml": 5
        case "oz", "kg": 0.25
        case "lb": 0.5
        default: 1
        }
    }

    private func nudge(_ direction: Double) {
        let current = Double(text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")) ?? 0
        let next = max(0, ((current + direction * step) * 1000).rounded() / 1000)
        text = next.formatted(.number.precision(.fractionLength(0...3)).grouping(.never))
    }
}
