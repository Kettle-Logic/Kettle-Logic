import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// The brew journal: the values measured on each brew session, next to what the recipe expected.
/// A recipe can have several sessions. Each session keeps its own measurements and readings.
struct JournalView: View {
    let document: RecipeDocument
    let recipe: RecipeSnapshot
    let store: DocumentStore

    @State private var planning = RecipePlanning()
    @State private var selection: UUID?
    @State private var enteringSheet = false
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric

    var body: some View {
        Group {
            sessionChooser
            if let index = selectedIndex {
                brewSheetSection
                measuredSections(index)
            } else {
                Section {
                    Text("No brew session yet. A session records one brew day, and a recipe can have several.")
                        .foregroundStyle(.secondary)
                }
            }
        }
        .onAppear(perform: load)
        .onChange(of: recipe) { load() }
        .sheet(isPresented: $enteringSheet) {
            if let session = selectedIndex.map({ planning.sessions[$0].id }) {
                BrewSheetEntryView(document: document, recipe: recipe, imperial: units == .us) { reading in
                    guard let index = planning.sessions.firstIndex(where: { $0.id == session }) else { return }
                    planning.sessions[index].apply(reading)
                    commit()
                }
            }
        }
    }

    // MARK: Brew sheet

    private var brewSheetSection: some View {
        Section {
            HStack {
                Button("Print Brew Sheet…") { printBrewSheet() }
                Button("Enter Brew Sheet…") { enteringSheet = true }
            }
        } header: {
            Text("Brew Sheet")
        } footer: {
            Text("Print a sheet to fill in with a pencil on brew day. Each box has a code, and the entry screen has the same codes. "
                + "Enter the sheet's values here when the brew is done.")
        }
    }

    private func printBrewSheet() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.pdf]
        panel.nameFieldStringValue = "\(recipe.name.replacingOccurrences(of: "/", with: "-")) brew sheet.pdf"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        store.attempt { try document.writeBrewSheetPDF(recipe.id, imperial: units == .us, to: url) }
    }

    // MARK: Sessions

    private var sessionChooser: some View {
        Section {
            Picker("Session", selection: $selection) {
                ForEach(planning.sessions) { session in
                    Text(session.name).tag(Optional(session.id))
                }
            }
            HStack {
                Button("New Session") { newSession() }
                Button("Delete Session", role: .destructive) { deleteSession() }
                    .disabled(selectedIndex == nil)
            }
            if let index = selectedIndex {
                TextField("Session name", text: $planning.sessions[index].name)
                    .onSubmit { commit() }
                DatePicker("Brew day", selection: $planning.sessions[index].brewDate, displayedComponents: .date)
                    .onChange(of: planning.sessions[index].brewDate) { commit() }
            }
        } header: {
            Text("Brew Session")
        }
    }

    private var selectedIndex: Int? {
        planning.sessions.firstIndex { $0.id == selection } ?? (planning.sessions.isEmpty ? nil : 0)
    }

    // MARK: Measured sections

    @ViewBuilder
    private func measuredSections(_ index: Int) -> some View {
        let plan = journal(for: planning.sessions[index])
        mashSection(index, plan)
        boilerSection(index, plan)
        fermenterSection(index, plan)
        brewhouseSection(index, plan)
        packagingSection(index, plan)
        statisticsSection(index, plan)
        carbonationSection(index, plan)
        profileSection(index)
        readingsSection(index)
    }

    private func mashSection(_ index: Int, _ plan: JournalPlan?) -> some View {
        Section {
            optionalField("Measured mash pH", $planning.sessions[index].mashPh, places: 2)
            LabeledContent("Adjusted mash pH (estimated)") { readout(adjustedMashPh, places: 2) }
            optionalField("Sparge runoff pH", $planning.sessions[index].spargeRunoffPh, places: 2)
            optionalField("End of runnings gravity (SG)", $planning.sessions[index].endOfRunningsGravity, places: 3)
        } header: {
            Text("Mash pH and Runnings")
        } footer: {
            Text("The adjusted mash pH is the Mash tab's estimate, from the residual alkalinity after the acids. Measure the pH to check it.")
        }
    }

    private func boilerSection(_ index: Int, _ plan: JournalPlan?) -> some View {
        Section {
            LabeledContent("Estimated pre-boil volume (\(units.volumeUnit))") {
                readout(recipe.results.preBoilLiters.map { units.displayVolume(liters: $0) }, places: 2)
            }
            volumeField("Measured pre-boil volume", $planning.sessions[index].preBoilLiters)
            LabeledContent("Estimated pre-boil gravity (SG)") { readout(plan?.expectedPreBoilGravity, places: 3) }
            optionalField("Measured pre-boil gravity (SG)", $planning.sessions[index].preBoilGravity, places: 3)
        } header: {
            Text("Volume and Gravity in the Boiler")
        }
    }

    private func fermenterSection(_ index: Int, _ plan: JournalPlan?) -> some View {
        Section {
            LabeledContent("Calculated batch size (\(units.volumeUnit))") {
                readout(units.displayVolume(liters: litersOf(recipe.batchSize)))
            }
            volumeField("Measured batch size", $planning.sessions[index].batchLiters)
            LabeledContent("Estimated original gravity (SG)") { readout(recipe.results.originalGravity, places: 3) }
            optionalField("Measured original gravity (SG)", $planning.sessions[index].originalGravity, places: 3)
        } header: {
            Text("Into the Fermenter")
        }
    }

    private func brewhouseSection(_ index: Int, _ plan: JournalPlan?) -> some View {
        Section {
            LabeledContent("Expected brewhouse efficiency (%)") {
                readout(plan?.expectedBrewhouseEfficiency, places: 1)
            }
            LabeledContent("Measured mash efficiency (%)") { readout(plan?.measuredMashEfficiency, places: 1) }
            LabeledContent("Measured brewhouse efficiency (%)") { readout(plan?.measuredBrewhouseEfficiency, places: 1) }
            LabeledContent("Estimated calories (per 12 fl oz)") { readout(plan?.expectedCaloriesPer12Oz, places: 0) }
            LabeledContent("Measured calories (per 12 fl oz)") { readout(plan?.measuredCaloriesPer12Oz, places: 0) }
        } header: {
            Text("Brewhouse Efficiency")
        } footer: {
            Text("The expected brewhouse efficiency is the recipe's, shown on the Equipment section too. "
                + "The measured efficiencies are the grain's potential extract, scaled by the gravity and volume measured. "
                + "Calories are an estimate from the gravities and alcohol.")
        }
    }

    private func packagingSection(_ index: Int, _ plan: JournalPlan?) -> some View {
        Section {
            LabeledContent("Estimated final gravity (SG)") { readout(recipe.results.finalGravity, places: 3) }
            optionalField("Measured final gravity (SG)", $planning.sessions[index].finalGravity, places: 3)
            LabeledContent("Estimated bottling volume (\(units.volumeUnit))") {
                readout(units.displayVolume(liters: litersOf(recipe.batchSize)))
            }
            volumeField("Measured bottling volume", $planning.sessions[index].bottlingLiters)
        } header: {
            Text("At Packaging")
        }
    }

    private func statisticsSection(_ index: Int, _ plan: JournalPlan?) -> some View {
        Section {
            LabeledContent("Estimated ABV (%)") { readout(recipe.results.abv, places: 2) }
            LabeledContent("Measured ABV (%)") { readout(plan?.measuredAbv, places: 2) }
            LabeledContent("Estimated attenuation (%)") { readout(plan?.expectedAttenuation, places: 1) }
            LabeledContent("Measured attenuation (%)") { readout(plan?.measuredAttenuation, places: 1) }
        } header: {
            Text("Brew Statistics")
        } footer: {
            Text("ABV is the gravity drop times 131.25, and attenuation is the share of the original gravity that fermented.")
        }
    }

    private func carbonationSection(_ index: Int, _ plan: JournalPlan?) -> some View {
        Section {
            Picker("Method", selection: $planning.sessions[index].carbonationMethod) {
                ForEach(BrewSession.carbonationMethods, id: \.self) { Text($0).tag($0) }
            }
            .onChange(of: planning.sessions[index].carbonationMethod) { commit() }
            optionalField("Target carbonation (volumes of CO₂)", $planning.sessions[index].carbonationVolumes, places: 2)
            if planning.sessions[index].carbonationMethod.hasPrefix("Bottle") {
                LabeledContent("Corn sugar (g per L)") { readout(plan?.primingSugarGramsPerLiter, places: 1) }
                LabeledContent("Corn sugar for the bottling volume (g)") { readout(plan?.primingSugarGrams, places: 0) }
            } else if planning.sessions[index].carbonationMethod == "Force carbonation" {
                LabeledContent("Serving temperature (\(units.temperatureSymbol))") {
                    TextField("", text: optionalNumberText(units.temperature($planning.sessions[index].servingTempC), places: 1))
                        .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
                }
                LabeledContent("Regulator pressure (psi)") { readout(plan?.forceCarbonationPsi, places: 1) }
            }
        } header: {
            Text("Carbonation")
        }
    }

    private func profileSection(_ index: Int) -> some View {
        Section {
            Picker("Profile", selection: Binding(
                get: { planning.sessions[index].profileName ?? "None" },
                set: { choose($0, index) })) {
                Text("None").tag("None")
                ForEach(FermentationPreset.all) { Text($0.name).tag($0.name) }
            }
            ForEach(planning.sessions[index].profileSteps.indices, id: \.self) { step in
                HStack {
                    LabeledContent("Days") {
                        TextField("", text: numberText($planning.sessions[index].profileSteps[step].days, places: 0))
                            .multilineTextAlignment(.trailing).frame(width: 60)
                            .onSubmit { commit() }
                    }
                    LabeledContent("Temperature (\(units.temperatureSymbol))") {
                        TextField("", text: numberText(units.temperature($planning.sessions[index].profileSteps[step].tempC), places: 1))
                            .multilineTextAlignment(.trailing).frame(width: 80)
                            .onSubmit { commit() }
                    }
                }
            }
            LabeledContent("Total days") {
                Text("\(Int(planning.sessions[index].profileSteps.reduce(0) { $0 + $1.days }))").monospacedDigit()
            }
        } header: {
            Text("Fermentation Temperature Profile")
        } footer: {
            Text("Pick a profile to fill in its steps, then change them to suit the yeast.")
        }
    }

    private func readingsSection(_ index: Int) -> some View {
        let session = planning.sessions[index]
        return Section {
            ForEach(session.readings.indices, id: \.self) { row in
                HStack {
                    DatePicker("Date", selection: $planning.sessions[index].readings[row].date, displayedComponents: .date)
                        .labelsHidden()
                        .onChange(of: planning.sessions[index].readings[row].date) { commit() }
                    Text("Day \(ReadingsCSV.day(of: session.readings[row].date, brewDate: session.brewDate))")
                        .monospacedDigit()
                        .frame(width: 60, alignment: .leading)
                    TextField(units.temperatureSymbol, text: numberText(optionalBinding(units.temperature($planning.sessions[index].readings[row].temperatureC)), places: 1))
                        .multilineTextAlignment(.trailing).frame(width: 70)
                        .onSubmit { commit() }
                    TextField("SG", text: numberText(optionalBinding($planning.sessions[index].readings[row].gravity), places: 3))
                        .multilineTextAlignment(.trailing).frame(width: 90)
                        .onSubmit { commit() }
                    Button(role: .destructive) {
                        planning.sessions[index].readings.remove(at: row)
                        commit()
                    } label: {
                        Image(systemName: "minus.circle")
                    }
                    .buttonStyle(.borderless)
                    .help("Remove this reading")
                }
            }
            HStack {
                Button("Add Reading") {
                    planning.sessions[index].readings.append(FermentationReading())
                    commit()
                }
                Button("Import CSV…") { importCSV(index) }
                Button("Export CSV…") { exportCSV(index) }
                    .disabled(session.readings.isEmpty)
            }
        } header: {
            Text("Fermentation Readings")
        } footer: {
            Text("Each reading has a date, a temperature, and a gravity. The day counts from brew day. "
                + "The CSV file has the columns date, day, temperature_c, and gravity_sg.")
        }
    }

    // MARK: Fields

    /// A measured value in the units the user sees. The engine works in liters, so this converts.
    private func volumeField(_ title: String, _ value: Binding<Double?>) -> some View {
        let factor = units.displayVolume(liters: 1)
        return LabeledContent(title) {
            TextField("", text: optionalNumberText(Binding(
                get: { value.wrappedValue.map { $0 * factor } },
                set: { value.wrappedValue = $0.map { $0 / factor } }), places: 2))
                .multilineTextAlignment(.trailing)
                .frame(width: 100)
                .onSubmit { commit() }
        }
    }

    private func optionalField(_ title: String, _ value: Binding<Double?>, places: Int) -> some View {
        LabeledContent(title) {
            TextField("", text: optionalNumberText(value, places: places))
                .multilineTextAlignment(.trailing)
                .frame(width: 100)
                .onSubmit { commit() }
        }
    }

    private func readout(_ value: Double?, places: Int = 1) -> some View {
        Text(value.map { $0.formatted(.number.precision(.fractionLength(places))) } ?? "—")
            .monospacedDigit()
    }

    // MARK: Values and actions

    /// A quantity from the snapshot in liters. The snapshot keeps the unit the recipe was given in.
    private func litersOf(_ quantity: Quantity) -> Double {
        let value = Double(quantity.value) ?? 0
        return quantity.unit == "gal" ? value * 3.785411784 : value
    }

    /// The mash pH the Mash tab estimates after the acids, from the saved water and mash settings.
    private var adjustedMashPh: Double? {
        try? document.brewingPlan(recipe.id, BrewingInputs.from(planning.water, planning.mash)).estimatedMashPh
    }

    private func journal(for session: BrewSession) -> JournalPlan? {
        try? document.journalPlan(recipe.id, JournalRequest(session))
    }

    private func optionalBinding(_ value: Binding<Double?>) -> Binding<Double> {
        Binding(get: { value.wrappedValue ?? 0 }, set: { value.wrappedValue = $0 })
    }

    private func optionalNumberText(_ value: Binding<Double?>, places: Int) -> Binding<String> {
        Binding(
            get: { value.wrappedValue.map { $0.formatted(.number.precision(.fractionLength(0...places))) } ?? "" },
            set: { text in
                let trimmed = text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")
                value.wrappedValue = trimmed.isEmpty ? nil : Double(trimmed) ?? value.wrappedValue
            })
    }

    private func choose(_ name: String, _ index: Int) {
        planning.sessions[index].profileName = name == "None" ? nil : name
        planning.sessions[index].profileSteps = FermentationPreset.all.first { $0.name == name }?.steps ?? []
        commit()
    }

    private func newSession() {
        let session = BrewSession(name: "Session \(planning.sessions.count + 1)")
        planning.sessions.append(session)
        selection = session.id
        commit()
    }

    private func deleteSession() {
        planning.sessions.removeAll { $0.id == selection }
        selection = planning.sessions.first?.id
        commit()
    }

    private func exportCSV(_ index: Int) {
        let session = planning.sessions[index]
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.commaSeparatedText]
        panel.directoryURL = try? RecipeLibrary.ensureFolder()
        panel.nameFieldStringValue = "\(session.name) readings.csv"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let text = ReadingsCSV.export(session.readings, brewDate: session.brewDate)
        store.attempt { try text.write(to: url, atomically: true, encoding: .utf8) }
    }

    private func importCSV(_ index: Int) {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.commaSeparatedText]
        panel.allowsMultipleSelection = false
        panel.directoryURL = RecipeLibrary.folder
        guard panel.runModal() == .OK, let url = panel.url else { return }
        store.attempt {
            let text = try String(contentsOf: url, encoding: .utf8)
            let result = ReadingsCSV.parse(text)
            planning.sessions[index].readings.append(contentsOf: result.readings)
            if result.skipped > 0 {
                throw EngineError(message: "Imported \(result.readings.count) readings. \(result.skipped) rows could not be read.")
            }
        }
        commit()
    }

    private func load() {
        planning = RecipePlanning.read(recipe.planning)
        if selection == nil {
            selection = planning.sessions.first?.id
        }
    }

    private func commit() {
        let sessions = planning.sessions
        PlanningStore.update(recipe, document: document, store: store) { $0.sessions = sessions }
    }
}
