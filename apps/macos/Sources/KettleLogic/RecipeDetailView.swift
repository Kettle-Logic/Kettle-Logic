import SwiftUI

/// Each section of a recipe, as a tab. Which tabs show depends on the recipe: a drink has
/// fermentation and plan tabs, a beer has boil-off and style tabs, and notes show when there are any.
enum RecipeTab: String, CaseIterable, Identifiable {
    case recipe, equipment, boilOff, fermentation, procedure, starter, waterBuilder, mash, journal, results, plan, style
    case fermentables, hops, cultures, water, miscellaneous, notes

    var id: Self { self }

    var title: String { title(simple: false) }

    /// Simple mode says "Yeast" for cultures, which is the only culture a new brewer uses. The starter tab
    /// is "Yeast Starter" in both, so the two never read as the same thing.
    func title(simple: Bool) -> String {
        return switch self {
        case .recipe: "Recipe"
        case .equipment: "Equipment"
        case .boilOff: "Boil-Off"
        case .fermentation: "Fermentation"
        case .procedure: "Procedure"
        case .starter: "Yeast Starter"
        case .waterBuilder: "Water Builder"
        case .mash: "Mash"
        case .journal: "Brew Journal"
        case .results: "Results"
        case .plan: "Plan"
        case .style: "Style"
        case .fermentables: "Fermentables"
        case .hops: "Hops"
        case .cultures: simple ? "Yeast" : "Yeast & Cultures"
        case .water: "Water"
        case .miscellaneous: "Miscellaneous"
        case .notes: "Notes"
        }
    }
}

/// The groups the tabs fall into when `Preferences.groupedTabsKey` is on. Each group is a top-level
/// choice, with its own tabs under it, so a recipe shows four choices instead of up to sixteen.
enum RecipeGroup: Int, CaseIterable, Identifiable {
    case recipe, ingredients, process, results

    var id: Self { self }

    var title: String {
        switch self {
        case .recipe: "Recipe"
        case .ingredients: "Ingredients"
        case .process: "Process"
        case .results: "Results"
        }
    }

    /// The group's tabs, in the order they show under it. A tab the recipe does not have is left out.
    var tabs: [RecipeTab] {
        switch self {
        case .recipe: [.recipe]
        case .ingredients: [.fermentables, .hops, .cultures, .water, .miscellaneous]
        case .process: [.equipment, .mash, .boilOff, .fermentation, .waterBuilder, .starter, .procedure, .journal]
        case .results: [.results, .style, .plan, .notes]
        }
    }
}

extension RecipeTab {
    var group: RecipeGroup {
        RecipeGroup.allCases.first { $0.tabs.contains(self) } ?? .recipe
    }
}

/// Edits one recipe and shows its results. The fields keep their own drafts, so
/// a half-typed value is not lost, and each is committed with Return. Each section
/// of the recipe is a tab, and only the selected tab's section is shown.
struct RecipeDetailView: View {
    let document: RecipeDocument
    let recipe: RecipeSnapshot
    let store: DocumentStore

    @State private var name = ""
    @State private var batchValue = ""
    @State private var efficiency = ""
    @State private var evaporation = ""
    @State private var trubLoss = ""
    @State private var request: RecipeIngredientRequest?
    @State private var toolRequest: ToolRequest?
    @State private var styleRequest: StyleRequest?
    @State private var showsStylePicker = false
    @State private var notesDraft = ""
    @FocusState private var notesFocused: Bool
    @State private var equipmentName = Preferences.defaultEquipmentName
    @State private var selectedTab: RecipeTab = .recipe
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric
    /// The last tab shown in each group, so going back to a group returns to where the user was.
    @State private var lastTab: [RecipeGroup: RecipeTab] = [:]
    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true
    @AppStorage(Preferences.groupedTabsKey) private var groupedTabs = true

    var body: some View {
        VStack(spacing: 0) {
            if groupedTabs {
                groupBar
                if tabsInActiveGroup.count > 1 {
                    Divider()
                    tabBar(tabsInActiveGroup)
                }
            } else {
                tabBar(visibleTabs)
            }
            Divider()
            if activeTab != .results {
                ResultsStrip(recipe: recipe, comparison: styleComparison, showsVolumes: !simpleMode) {
                    selectedTab = .results
                }
                Divider()
            }
            Form {
                sectionView(for: activeTab)
            }
            .formStyle(.grouped)
            .textFieldStyle(.roundedBorder)
        }
        .navigationTitle(recipe.name)
        .onAppear(perform: loadDrafts)
        .onChange(of: recipe) { loadDrafts() }
        .onChange(of: units) { loadDrafts() }
        .onChange(of: selectedTab) { lastTab[selectedTab.group] = selectedTab }
        .background(groupShortcuts)
        .toolbar {
            ToolbarItem {
                Text(store.saveStatus(of: document, recipe: recipe))
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .accessibilityLabel("Save status")
            }
            .sharedBackgroundVisibility(.hidden)
            ToolbarItem {
                Button {
                    store.save()
                } label: {
                    Label { Text("Save") } icon: { FloppyDiskIcon() }
                        .labelStyle(.titleAndIcon)
                }
                .help("Save this document. Command-S also saves.")
            }
            ToolbarItem {
                RecipeShareMenu(document: document, recipe: recipe, store: store)
            }
            ToolbarItem {
                Button("Duplicate Recipe", systemImage: "plus.square.on.square") {
                    store.duplicateRecipe(ItemKey(document: document.id, id: recipe.id))
                }
                .help("Make a copy of this recipe, after it in the list")
            }
            ToolbarItem {
                Button("Delete Recipe", systemImage: "trash", role: .destructive) {
                    store.requestDelete(ItemKey(document: document.id, id: recipe.id))
                }
                .help("Delete this recipe. It moves to Recently Deleted.")
            }
        }
        .sheet(item: $toolRequest) { ToolSheet(request: $0) }
        .sheet(item: $styleRequest) { request in
            StyleEditor(
                request: request,
                save: { style in try saveStyle(style, isNew: request.style == nil) },
                remove: request.style.map { style in { try removeStyle(style.id) } })
        }
        .sheet(item: $request) { request in
            IngredientEditor(
                title: request.itemID == nil ? "Add \(request.draft.kind.title)" : "Edit \(request.draft.kind.title)",
                saveTitle: request.itemID == nil ? "Add" : "Save",
                seed: request.draft,
                menu: request.itemID == nil
                    ? IngredientDraft.menuEntries(for: request.draft.kind, custom: store.customIngredients.ingredients)
                    : [],
                currency: recipe.currency
            ) { draft in
                // The ingredient and its price are one edit, so one undo takes back both.
                try document.grouped(request.itemID == nil ? "Add \(draft.kind.title)" : "Edit \(draft.kind.title)") {
                    try draft.commit(to: document, recipe: recipe.id, existing: request.itemID)
                }
            }
        }
    }

    // MARK: Tabs

    /// The tabs that apply to this recipe, in the order they show.
    private var visibleTabs: [RecipeTab] {
        simpleMode ? simpleTabs : allTabs
    }

    /// Simple mode: ingredients, results, style, the procedure, and the journal. Water and
    /// miscellaneous show only when the recipe already has some, so no data is ever hidden.
    private var simpleTabs: [RecipeTab] {
        var tabs: [RecipeTab] = [.recipe, .fermentables, .hops, .cultures]
        if !recipe.waters.isEmpty { tabs.append(.water) }
        if !recipe.miscellaneous.isEmpty { tabs.append(.miscellaneous) }
        tabs += [.results]
        if !recipe.isFermentedDrink { tabs.append(.style) }
        tabs += [.procedure, .journal, .equipment]
        tabs.append(.notes)
        return tabs
    }

    private var allTabs: [RecipeTab] {
        var tabs: [RecipeTab] = [.recipe, .equipment]
        if recipe.isFermentedDrink {
            tabs.append(.fermentation)
        } else {
            tabs.append(.boilOff)
        }
        tabs.append(.procedure)
        tabs.append(.starter)
        tabs += [.waterBuilder, .mash, .journal]
        tabs.append(.results)
        if recipe.results.drink != nil {
            tabs.append(.plan)
        }
        if !recipe.isFermentedDrink {
            tabs.append(.style)
        }
        tabs += [.fermentables, .hops, .cultures, .water, .miscellaneous]
        tabs.append(.notes)
        return tabs
    }

    /// The selected tab, or the recipe tab when the selected one no longer applies (after a change of type).
    private var activeTab: RecipeTab {
        visibleTabs.contains(selectedTab) ? selectedTab : .recipe
    }

    // MARK: Grouped tabs

    /// The tabs of a group that this recipe has, in the group's order.
    private func tabs(in group: RecipeGroup) -> [RecipeTab] {
        group.tabs.filter { visibleTabs.contains($0) }
    }

    private var tabsInActiveGroup: [RecipeTab] { tabs(in: activeTab.group) }

    /// The groups that have at least one tab for this recipe.
    private var visibleGroups: [RecipeGroup] {
        RecipeGroup.allCases.filter { !tabs(in: $0).isEmpty }
    }

    private func select(_ group: RecipeGroup) {
        let available = tabs(in: group)
        selectedTab = lastTab[group].flatMap { available.contains($0) ? $0 : nil } ?? available.first ?? .recipe
    }

    private var groupBar: some View {
        Picker("Section", selection: Binding(get: { activeTab.group }, set: { select($0) })) {
            ForEach(visibleGroups) { Text($0.title).tag($0) }
        }
        .pickerStyle(.segmented)
        .labelsHidden()
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(.bar)
    }

    /// Command-1 to Command-4 pick a group. They are hidden buttons, since a menu cannot reach this view's state.
    @ViewBuilder
    private var groupShortcuts: some View {
        if groupedTabs {
            ZStack {
                ForEach(Array(visibleGroups.enumerated()), id: \.element) { index, group in
                    Button(group.title) { select(group) }
                        .keyboardShortcut(KeyEquivalent(Character(String(index + 1))), modifiers: .command)
                }
            }
            .opacity(0)
            .allowsHitTesting(false)
            .accessibilityHidden(true)
        }
    }

    private func tabBar(_ tabs: [RecipeTab]) -> some View {
        ScrollView(.horizontal) {
            HStack(spacing: 4) {
                ForEach(tabs) { tab in
                    let isSelected = tab == activeTab
                    Button {
                        selectedTab = tab
                    } label: {
                        Text(tab.title(simple: simpleMode))
                            .font(.callout.weight(isSelected ? .semibold : .regular))
                            .foregroundStyle(isSelected ? Color.primary : Color.secondary)
                            .padding(.horizontal, 10)
                            .padding(.vertical, 8)
                            .overlay(alignment: .bottom) {
                                Rectangle()
                                    .fill(isSelected ? Color.accentColor : Color.clear)
                                    .frame(height: 2)
                            }
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityAddTraits(isSelected ? .isSelected : [])
                }
            }
            .padding(.horizontal, 12)
        }
        .scrollIndicators(.hidden)
        .background(.bar)
    }

    @ViewBuilder
    private func sectionView(for tab: RecipeTab) -> some View {
        switch tab {
        case .recipe:
            Group {
                recipeSection
                overviewSection
            }
        case .equipment: equipmentSection
        case .boilOff: boilOffSection
        case .fermentation: fermentationSection
        case .procedure: ProcedureSection(document: document, recipe: recipe.id, profile: equipment)
        case .starter: StarterView(document: document, recipe: recipe, store: store)
        case .waterBuilder: WaterView(document: document, recipe: recipe, store: store)
        case .mash: MashView(document: document, recipe: recipe, store: store)
        case .journal:
            Group {
                JournalView(document: document, recipe: recipe, store: store)
                journalCalculators
            }
        case .results: resultsSection
        case .plan: planSection
        case .style:
            Group {
                styleSection
                ownStylesSection
            }
        case .fermentables: fermentablesSection
        case .hops:
            Group {
                hopsSection
                hopCalculators
            }
        case .cultures:
            Group {
                culturesSection
                yeastCalculators
            }
        case .water: waterSection
        case .miscellaneous: miscellaneousSection
        case .notes: notesSection
        }
    }

    // MARK: Sections

    private var recipeSection: some View {
        Section {
            TextField("Name", text: $name)
                .onSubmit { store.attempt { try document.rename(recipe.id, to: name) } }

            Picker("Type", selection: typeBinding) {
                ForEach(recipeTypes, id: \.self) { Text($0).tag($0) }
            }

            Picker("Currency", selection: currencyBinding) {
                ForEach(options(currencyCodes, including: recipe.currency), id: \.self) { Text($0).tag($0) }
            }
            .help("The currency of the ingredient prices")

            LabeledContent("Batch size") {
                HStack {
                    TextField("Batch size", text: $batchValue)
                        .multilineTextAlignment(.trailing)
                        .frame(width: 100)
                        .onSubmit {
                            store.attempt {
                                try document.setBatchSize(recipe.id, value: batchValue, unit: recipe.batchSize.unit)
                            }
                        }
                    Text(recipe.batchSize.unit)
                }
            }
        } header: {
            Text("Recipe")
        } footer: {
            if simpleMode {
                Text("Simple mode is on. Mash, water chemistry, yeast starter, and boil-off use typical values "
                    + "for your brewing system. Turn it off in the View menu or Settings for full control.")
            }
        }
    }

    /// The recipe at a glance: its style and the bill of ingredients with their amounts.
    @ViewBuilder private var overviewSection: some View {
        Section {
            LabeledContent("Style") {
                Text(styleName(recipe.bjcpStyle))
                    .foregroundStyle(recipe.bjcpStyle == nil ? .secondary : .primary)
            }
            let bill = overviewRows
            if bill.isEmpty {
                Text("No ingredients yet.").foregroundStyle(.secondary)
            }
            ForEach(bill) { row in
                LabeledContent(row.name) { Text(row.amount).monospacedDigit() }
            }
        } header: {
            Text("Overview")
        }
    }

    private struct OverviewRow: Identifiable {
        let id: String
        let name: String
        let amount: String
    }

    /// Every ingredient with its amount, in the order fermentables, hops, cultures, water, miscellaneous.
    private var overviewRows: [OverviewRow] {
        let fermentables = recipe.fermentables.map { ($0.uuid, $0.name, $0.amount.display) }
        let hops = recipe.hops.map { ($0.uuid, $0.name, $0.amount.display) }
        let cultures = recipe.cultures.map { ($0.uuid, $0.name, $0.amount.display) }
        let waters = recipe.waters.map { ("water-\($0.id)", $0.name, $0.amount?.display ?? "") }
        let misc = recipe.miscellaneous.map { ($0.uuid, $0.name, $0.amount.display) }
        return (fermentables + hops + cultures + waters + misc).map { OverviewRow(id: $0.0, name: $0.1, amount: $0.2) }
    }

    private var equipmentSection: some View {
        Section {
            Picker("Brewing system", selection: equipmentChoice) {
                ForEach(store.equipment.all) { Text($0.name).tag($0.name) }
            }
            Text(equipment.note).font(.caption).foregroundStyle(.secondary)
            LabeledContent {
                Text(recipe.brewhouseEfficiency.display).monospacedDigit()
            } label: {
                HelpLabel("Brewhouse efficiency", term: .brewhouseEfficiency)
            }
            Button("Apply to Recipe", action: applyEquipment)
        } header: {
            Text("Equipment")
        } footer: {
            Text("Applying a system sets the batch size, brewhouse efficiency, and boil-off in one undo step. "
                + "The figures are starting points, so measure your own system.")
        }
    }

    private var boilOffSection: some View {
        Section("Boil-off") {
            LabeledContent {
                TextField("Evaporation", text: $evaporation)
                    .multilineTextAlignment(.trailing)
                    .frame(width: 100)
                    .onSubmit { commitBoilOff() }
            } label: {
                HelpLabel("Evaporation (% per hour)", term: .evaporation)
            }
            LabeledContent {
                TextField("Trub loss", text: $trubLoss)
                    .multilineTextAlignment(.trailing)
                    .frame(width: 100)
                    .onSubmit { commitBoilOff() }
            } label: {
                HelpLabel("Trub and chiller loss (\(units.volumeSymbol))", term: .trubLoss)
            }
        }
    }

    private var fermentationSection: some View {
        Section {
            LabeledContent {
                TextField("Efficiency", text: $efficiency)
                    .multilineTextAlignment(.trailing)
                    .frame(width: 100)
                    .onSubmit {
                        store.attempt { try document.setFermentationEfficiency(recipe.id, percent: efficiency) }
                    }
            } label: {
                HelpLabel("Fermentation efficiency (%)", term: .fermentationEfficiency)
            }
        } header: {
            Text("Fermentation")
        } footer: {
            Text("The share of sugar that yeast turns into alcohol. Used to plan the sugar a drink needs.")
        }
    }

    @ViewBuilder private var resultsSection: some View {
        Section("Results") {
            ResultRow("Original gravity", value: recipe.results.originalGravity, places: 3, term: .originalGravity)
            ResultRow("Final gravity", value: recipe.results.finalGravity, places: 3, term: .finalGravity)
            ResultRow("Alcohol (% by volume)", value: recipe.results.abv, places: 2, term: .abv)
            if !recipe.isFermentedDrink {
                ResultRow("Bitterness (IBU)", value: recipe.results.ibu, places: 1, term: .ibu)
                ResultRow("Color (SRM)", value: recipe.results.srm, places: 1, term: .srm)
            }
            if !recipe.isFermentedDrink && !simpleMode {
                ResultRow(
                    "Post-boil volume (\(units == .metric ? "L" : "gal"))",
                    value: recipe.results.postBoilLiters.map { units.displayVolume(liters: $0) },
                    places: 2)
            }
            if !simpleMode, let so2 = recipe.results.sulfiteSo2Ppm {
                ResultRow("Sulfite SO2 added (mg/L)", value: so2, places: 1)
            }
        }
        if let cost = recipe.results.cost {
            costSection(cost)
        }
    }

    /// What the priced ingredients cost. Only a recipe with at least one price has this.
    private func costSection(_ cost: RecipeCost) -> some View {
        Section {
            LabeledContent("Ingredients") {
                Text(money(cost.total, currency: cost.currency)).monospacedDigit()
            }
            let perVolume = units == .metric ? cost.perLiter : cost.perGallon
            if let perVolume {
                LabeledContent("Per \(units == .metric ? "liter" : "gallon")") {
                    Text(money(perVolume, currency: cost.currency)).monospacedDigit()
                }
            }
            if cost.unpriced > 0 {
                Text(
                    "\(cost.unpriced) \(cost.unpriced == 1 ? "ingredient has" : "ingredients have") no price, "
                        + "so \(cost.unpriced == 1 ? "it is" : "they are") not counted. Water is never priced."
                )
                .font(.caption)
                .foregroundStyle(.secondary)
            }
        } header: {
            Text("Cost")
        } footer: {
            Text("Prices are the ones you entered, for the amounts in this recipe. They are not looked up.")
        }
    }

    private var planSection: some View {
        Section {
            if let plan = recipe.results.drink {
                ResultRow("Sugar to reach target gravity (g)", value: plan.gravityGrams, places: 1)
                ResultRow("Sugar to reach target alcohol (g)", value: plan.alcoholGrams, places: 1)
                ForEach(plan.notes, id: \.self) { Text($0).foregroundStyle(.secondary) }
            }
        } header: {
            Text("Plan")
        }
    }

    private var styleSection: some View {
        Section {
            LabeledContent("Style", value: styleName(recipe.bjcpStyle))
            Button("Choose a Style…", systemImage: "magnifyingglass") { showsStylePicker = true }
                .popover(isPresented: $showsStylePicker, arrowEdge: .bottom) {
                    StylePicker(current: recipe.bjcpStyle, userStyles: userStyles) { style in
                        showsStylePicker = false
                        store.attempt { try document.setStyle(recipe.id, to: style) }
                    }
                }
            if let style = bjcpStyles.first(where: { $0.id == recipe.bjcpStyle }) {
                BJCPStyleNotes(style: style)
            }
            if let comparison = styleComparison {
                ForEach(comparison.checks) { check in
                    StyleCheckRow(check: check, ownStyle: comparison.style.id.hasPrefix("custom-"))
                }
            }
        } header: {
            Text("Style")
        } footer: {
            Text("Compares the results with the style's range. The marker shows where the recipe falls in that range.")
        }
    }

    /// The styles the user defined, which the recipe can be compared against, with the means to change them.
    private var ownStylesSection: some View {
        Section {
            ForEach(userStyles) { style in
                HStack {
                    Button(style.name) { styleRequest = StyleRequest(style: style) }
                        .buttonStyle(.link)
                    Spacer()
                    Button("Remove", role: .destructive) {
                        store.attempt { try document.removeUserStyle(style.id) }
                    }
                }
            }
            Button("New Style…") { styleRequest = StyleRequest(style: nil) }
        } header: {
            Text("Your Styles")
        } footer: {
            Text("Styles you define are saved in this recipe's file, so they travel with it.")
        }
    }

    private var fermentablesSection: some View {
        Section {
            if recipe.fermentables.isEmpty {
                Text("No fermentables yet.").foregroundStyle(.secondary)
            }
            ForEach(recipe.fermentables) { item in
                IngredientRow(name: item.name, detail: item.detail + recipe.priceDetail(item.price, uuid: item.uuid)) {
                    open(IngredientDraft(fermentable: item), item.id)
                } duplicate: {
                    store.attempt { try document.duplicateIngredient(.fermentable, item.id, in: recipe.id) }
                } remove: {
                    store.attempt { try document.removeFermentable(recipe.id, item.id) }
                }
            }
        } header: {
            IngredientHeader(title: "Fermentables") { add(.fermentable) }
        }
    }

    private var hopsSection: some View {
        Section {
            if recipe.hops.isEmpty {
                Text("No hops yet.").foregroundStyle(.secondary)
            }
            ForEach(recipe.hops) { item in
                IngredientRow(name: item.name, detail: item.detail + recipe.priceDetail(item.price, uuid: item.uuid)) {
                    open(IngredientDraft(hop: item), item.id)
                } duplicate: {
                    store.attempt { try document.duplicateIngredient(.hop, item.id, in: recipe.id) }
                } remove: {
                    store.attempt { try document.removeHop(recipe.id, item.id) }
                }
            }
        } header: {
            IngredientHeader(title: "Hops") { add(.hop) }
        }
    }

    private var culturesSection: some View {
        Section {
            if recipe.cultures.isEmpty {
                Text("No yeast or other cultures yet.").foregroundStyle(.secondary)
            }
            ForEach(recipe.cultures) { item in
                IngredientRow(name: item.name, detail: item.detail + recipe.priceDetail(item.price, uuid: item.uuid)) {
                    open(IngredientDraft(culture: item), item.id)
                } duplicate: {
                    store.attempt { try document.duplicateIngredient(.culture, item.id, in: recipe.id) }
                } remove: {
                    store.attempt { try document.removeCulture(recipe.id, item.id) }
                }
            }
        } header: {
            IngredientHeader(title: simpleMode ? "Yeast" : "Yeast & Cultures") { add(.culture) }
        }
    }

    // MARK: Calculators

    /// Checks the bitterness of a boil hop with the Tinseth calculator, filled in from the recipe.
    @ViewBuilder
    private var hopCalculators: some View {
        let boilHops = recipe.hops.filter { $0.use == "add_to_boil" && ($0.time?.value ?? "0") != "0" }
        if !boilHops.isEmpty {
            Section("Calculators") {
                Menu("Check a Hop's Bitterness…", systemImage: "function") {
                    ForEach(boilHops) { hop in
                        Button("\(hop.name), \(hop.time?.value ?? "") min") {
                            toolRequest = ToolRequest(kind: .hopBitterness, prefill: recipe.bitternessPrefill(for: hop))
                        }
                    }
                }
                .help("Opens the bitterness calculator with this hop's figures filled in")
            }
        }
    }

    /// The yeast starter calculator, which Simple Mode otherwise hides along with the Yeast Starter tab.
    private var yeastCalculators: some View {
        Section("Calculators") {
            CalculatorButton(title: "Yeast Starter Calculator…", systemImage: "function") {
                toolRequest = ToolRequest(kind: .yeastStarter, prefill: recipe.starterPrefill)
            }
        }
    }

    /// Corrections for the gravity readings a brewer takes on brew day.
    private var journalCalculators: some View {
        Section("Calculators") {
            CalculatorButton(title: "Correct a Hydrometer Reading for Temperature…", systemImage: "thermometer.medium") {
                toolRequest = ToolRequest(kind: .hydrometer, prefill: [:])
            }
            CalculatorButton(title: "Convert a Refractometer Reading…", systemImage: "eyedropper") {
                toolRequest = ToolRequest(kind: .refractometer, prefill: [:])
            }
        }
    }

    private var waterSection: some View {
        Section {
            if recipe.waters.isEmpty {
                Text("No water yet.").foregroundStyle(.secondary)
            }
            ForEach(recipe.waters) { item in
                IngredientRow(name: item.name, detail: item.detail) {
                    open(IngredientDraft(water: item), item.id)
                } duplicate: {
                    store.attempt { try document.duplicateIngredient(.water, item.id, in: recipe.id) }
                } remove: {
                    store.attempt { try document.removeWater(recipe.id, item.id) }
                }
            }
        } header: {
            IngredientHeader(title: "Water") { add(.water) }
        }
    }

    private var miscellaneousSection: some View {
        Section {
            if recipe.miscellaneous.isEmpty {
                Text("No miscellaneous ingredients yet.").foregroundStyle(.secondary)
            }
            ForEach(recipe.miscellaneous) { item in
                IngredientRow(name: item.name, detail: item.amount.display + recipe.priceDetail(item.price, uuid: item.uuid)) {
                    open(IngredientDraft(miscellaneous: item), item.id)
                } duplicate: {
                    store.attempt { try document.duplicateIngredient(.miscellaneous, item.id, in: recipe.id) }
                } remove: {
                    store.attempt { try document.removeMiscellaneous(recipe.id, item.id) }
                }
            }
        } header: {
            IngredientHeader(title: "Miscellaneous") { add(.miscellaneous) }
        }
    }

    private var notesSection: some View {
        Group {
            Section {
                TextEditor(text: $notesDraft)
                    .frame(minHeight: 160)
                    .focused($notesFocused)
                    .overlay {
                        RoundedRectangle(cornerRadius: 6).strokeBorder(.separator)
                    }
            } header: {
                Text("Brewing Notes")
            } footer: {
                Text("Your own notes for this recipe. They are saved when you click away.")
            }
            if !recipe.results.notes.isEmpty {
                Section("Calculation Notes") {
                    ForEach(recipe.results.notes, id: \.self) { Text($0).foregroundStyle(.secondary) }
                }
            }
        }
        .onChange(of: notesFocused) { if !notesFocused { commitNotes() } }
        .onDisappear(perform: commitNotes)
    }

    /// Saves the notes if they changed, so clicking away or leaving the tab never loses them.
    private func commitNotes() {
        guard notesDraft != RecipePlanning.read(recipe.planning).notes else { return }
        PlanningStore.update(recipe, document: document, store: store) { $0.notes = notesDraft }
    }

    // MARK: Values and actions

    /// The recipe's values against its style. Nil when there is no style, or the engine cannot compare.
    private var styleComparison: StyleComparison? {
        try? document.styleComparison(recipe.id)
    }

    /// The styles the user defined.
    private var userStyles: [UserStyle] {
        (try? document.userStyles()) ?? []
    }

    /// The name of a style for the overview: its guideline title, or the user's own name. "None" for no style.
    private func styleName(_ id: String?) -> String {
        guard let id else { return "None" }
        if let style = bjcpStyles.first(where: { $0.id == id }) { return style.title }
        return userStyles.first(where: { $0.id == id })?.name ?? "None"
    }

    /// Saves a new style, and chooses it for the recipe, or changes one of the user's styles. One undo step.
    private func saveStyle(_ style: UserStyle, isNew: Bool) throws {
        try store.perform {
            try document.grouped(isNew ? "New Style" : "Edit Style") {
                if isNew {
                    let created = try document.addUserStyle(style)
                    try document.setStyle(recipe.id, to: created)
                } else {
                    try document.updateUserStyle(style)
                }
            }
        }
    }

    /// Removes one of the user's styles. Recipes compared against it are left with no style.
    private func removeStyle(_ id: String) throws {
        try store.perform { try document.removeUserStyle(id) }
    }

    private var equipment: EquipmentProfile {
        store.equipment.profile(named: equipmentName)
    }

    /// The brewing system by name. A name that no system has shows the system the recipe falls back to.
    private var equipmentChoice: Binding<String> {
        Binding(
            get: { equipment.name },
            set: { equipmentName = $0 })
    }

    private var typeBinding: Binding<String> {
        Binding(
            get: { recipe.type },
            set: { newType in store.attempt { try document.setType(recipe.id, to: newType) } }
        )
    }

    private var currencyBinding: Binding<String> {
        Binding(
            get: { recipe.currency },
            set: { code in store.attempt { try document.setRecipeCurrency(recipe.id, code) } }
        )
    }

    private func add(_ kind: IngredientKind) {
        request = RecipeIngredientRequest(draft: IngredientDraft(kind: kind), itemID: nil)
    }

    private func open(_ draft: IngredientDraft, _ itemID: UInt64) {
        request = RecipeIngredientRequest(draft: draft, itemID: itemID)
    }

    private func applyEquipment() {
        let profile = equipment
        store.attempt {
            try document.applyEquipment(
                recipe.id,
                batchValue: units.volumeText(liters: profile.batchLiters),
                batchUnit: units.volumeUnit,
                efficiencyPercent: profile.efficiencyPercent,
                evaporationPercent: profile.evaporationPercent,
                trubLossLiters: profile.trubLossLiters)
        }
    }

    private func commitBoilOff() {
        store.attempt {
            // An untouched field keeps the recipe's own figure, so rounding the display never changes it.
            let untouched = trubLoss == units.volumeText(liters: recipe.boilOff.trubLoss.value)
            try document.setBoilOff(
                recipe.id, evaporationPercent: evaporation,
                trubLossLiters: untouched ? recipe.boilOff.trubLoss.value : units.litersText(volume: trubLoss))
        }
    }

    /// Copies the recipe's values into the drafts, so an undo or redo shows at once.
    private func loadDrafts() {
        name = recipe.name
        batchValue = recipe.batchSize.value
        efficiency = recipe.fermentationEfficiency.value
        evaporation = recipe.boilOff.evaporationRate.value
        trubLoss = units.volumeText(liters: recipe.boilOff.trubLoss.value)
        notesDraft = RecipePlanning.read(recipe.planning).notes
    }
}

/// A calculated value, or a dash when the engine could not compute it.
struct ResultRow: View {
    let title: String
    let value: Double?
    let places: Int
    let term: Term?

    init(_ title: String, value: Double?, places: Int, term: Term? = nil) {
        self.title = title
        self.value = value
        self.places = places
        self.term = term
    }

    var body: some View {
        LabeledContent {
            Text(value.map { $0.formatted(.number.precision(.fractionLength(places))) } ?? "—")
                .monospacedDigit()
        } label: {
            if let term {
                HelpLabel(title, term: term)
            } else {
                Text(title)
            }
        }
    }
}
