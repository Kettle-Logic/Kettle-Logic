import SwiftUI

/// A number field bound to a value, showing it with a fixed number of places. Text that is not a
/// number leaves the value as it was. `factor` converts the value to the unit the field shows,
/// and the typed number is converted back.
func numberText(_ value: Binding<Double>, places: Int = 2, factor: Double = 1) -> Binding<String> {
    Binding(
        get: { (value.wrappedValue * factor).formatted(.number.precision(.fractionLength(0...places))) },
        set: { text in
            if let number = Double(text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")) {
                value.wrappedValue = number / factor
            }
        })
}

/// A readout for a value the engine worked out, or a dash when it has none.
func readout(_ value: Double?, places: Int = 1) -> some View {
    Text(value.map { $0.formatted(.number.precision(.fractionLength(places))) } ?? "—")
        .monospacedDigit()
}

/// The water tab: saved water profiles to load, the base water, the salts added to it, and the water
/// profiles and analysis that follow. The mash and sparge volumes come from the mash tab's settings.
struct WaterView: View {
    let document: RecipeDocument
    let recipe: RecipeSnapshot
    let store: DocumentStore

    @State private var planning = RecipePlanning()
    @State private var chosenProfile: UUID?
    @State private var libraryMessage: String?
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric
    @Environment(WaterProfileLibrary.self) private var waterProfiles

    var body: some View {
        Group {
            savedSection
            baseSection
            saltSection
            targetSection
            switch result {
            case .success(let plan):
                profileSection(plan)
                analysisSection(plan)
                if !plan.notes.isEmpty {
                    Section("Notes") {
                        ForEach(plan.notes, id: \.self) { Text($0).foregroundStyle(.secondary) }
                    }
                }
            case .failure(let error):
                Section {
                    Text(error.localizedDescription).foregroundStyle(.red)
                }
            }
        }
        .onAppear(perform: load)
        .onChange(of: recipe) { load() }
    }

    // MARK: Sections

    private var savedSection: some View {
        Section {
            if waterProfiles.profiles.isEmpty {
                Text("No saved profiles yet. Save one from the Water Profile tool in the Tools menu.")
                    .foregroundStyle(.secondary)
            } else {
                Picker("Profile", selection: $chosenProfile) {
                    Text("None").tag(UUID?.none)
                    ForEach(waterProfiles.profiles) { Text($0.name).tag(Optional($0.id)) }
                }
                HStack {
                    Button("Load Profile") { loadChosenProfile() }
                        .disabled(chosenProfile == nil || strikeLiters == nil)
                    Button("Remove Profile", role: .destructive) { removeChosenProfile() }
                        .disabled(chosenProfile == nil)
                }
            }
            if let libraryMessage {
                Text(libraryMessage).foregroundStyle(.secondary)
            }
        } header: {
            Text("Saved Water Profiles")
        } footer: {
            Text("Loading a profile replaces the base water, the reverse osmosis share, and the salts. The salts are "
                + "scaled from grams per liter to this recipe's mash water. A profile with a target sets the target; "
                + "one without keeps this recipe's target.")
        }
    }

    private var baseSection: some View {
        Section {
            Picker("Start from", selection: Binding(
                get: { planning.water.baseName },
                set: { chooseBase($0) })) {
                Text("Custom").tag("Custom")
                ForEach(Catalog.waters, id: \.name) { water in
                    Text(water.name).tag(water.name)
                }
            }
            ionRow("Calcium (ppm)", $planning.water.base.calcium)
            ionRow("Magnesium (ppm)", $planning.water.base.magnesium)
            ionRow("Sodium (ppm)", $planning.water.base.sodium)
            ionRow("Sulfate (ppm)", $planning.water.base.sulfate)
            ionRow("Chloride (ppm)", $planning.water.base.chloride)
            ionRow("Bicarbonate (ppm)", $planning.water.base.bicarbonate)
            LabeledContent("Reverse osmosis share (%)") {
                TextField("", text: numberText($planning.water.roPercent, places: 0))
                    .multilineTextAlignment(.trailing)
                    .frame(width: 100)
                    .onSubmit { commit() }
            }
        } header: {
            Text("Base Water")
        } footer: {
            Text("The brewing water before any salts. The reverse osmosis share mixes distilled water in first."
                + "Pick a start point or type your own water report.")
        }
    }

    private var targetSection: some View {
        Section {
            Toggle("Compare with a target water", isOn: Binding(
                get: { planning.water.useTarget },
                set: { planning.water.useTarget = $0; commit() }))
            if planning.water.useTarget {
                ionRow("Target calcium (ppm)", $planning.water.target.calcium)
                ionRow("Target magnesium (ppm)", $planning.water.target.magnesium)
                ionRow("Target sodium (ppm)", $planning.water.target.sodium)
                ionRow("Target sulfate (ppm)", $planning.water.target.sulfate)
                ionRow("Target chloride (ppm)", $planning.water.target.chloride)
                ionRow("Target bicarbonate (ppm)", $planning.water.target.bicarbonate)
            }
        } header: {
            Text("Target Water")
        } footer: {
            Text("The desired mash water. The adjusted profile shows how far the salts leave it from this."
                + "The default is the Pale Ale profile.")
        }
    }

    private var saltSection: some View {
        Section {
            Toggle("Leave out water salts", isOn: Binding(
                get: { planning.water.leaveOutSalts },
                set: { planning.water.leaveOutSalts = $0; commit() }))
            Toggle("Hold sparge salts until the boil", isOn: Binding(
                get: { planning.water.holdSpargeSaltsUntilBoil },
                set: { planning.water.holdSpargeSaltsUntilBoil = $0; commit() }))
            ForEach(planning.water.salts.indices, id: \.self) { index in
                HStack {
                    Picker("Salt", selection: Binding(
                        get: { planning.water.salts[index].kind },
                        set: { planning.water.salts[index].kind = $0; commit() })) {
                        ForEach(Self.saltKinds, id: \.self) { Text($0).tag($0) }
                    }
                    .labelsHidden()
                    TextField("g", text: numberText($planning.water.salts[index].grams))
                        .multilineTextAlignment(.trailing)
                        .frame(width: 70)
                        .onSubmit { commit() }
                    Picker("Added to", selection: Binding(
                        get: { planning.water.salts[index].sparge },
                        set: { planning.water.salts[index].sparge = $0; commit() })) {
                        Text("Mash").tag(false)
                        Text("Sparge").tag(true)
                    }
                    .labelsHidden()
                    .frame(width: 110)
                    Button(role: .destructive) {
                        planning.water.salts.remove(at: index)
                        commit()
                    } label: {
                        Image(systemName: "minus.circle")
                    }
                    .buttonStyle(.borderless)
                    .help("Remove this salt")
                }
            }
            Button("Add Salt") {
                planning.water.salts.append(SaltInput())
                commit()
            }
        } header: {
            Text("Water Salts")
        } footer: {
            Text("Grams of each salt, added to the mash or the sparge water. Salts are shown in the profile below.")
        }
    }

    private func profileSection(_ plan: BrewingPlan) -> some View {
        Section {
            LabeledContent("Total mash water (\(units.volumeSymbol))") {
                readout(units.displayVolume(liters: plan.strikeLiters))
            }
            LabeledContent("Calcium (ppm)") { readout(plan.mashWater.ions.calcium, places: 0) }
            LabeledContent("Magnesium (ppm)") { readout(plan.mashWater.ions.magnesium, places: 0) }
            LabeledContent("Sodium (ppm)") { readout(plan.mashWater.ions.sodium, places: 0) }
            LabeledContent("Sulfate (ppm)") { readout(plan.mashWater.ions.sulfate, places: 0) }
            LabeledContent("Chloride (ppm)") { readout(plan.mashWater.ions.chloride, places: 0) }
            LabeledContent("Bicarbonate (ppm)") { readout(plan.mashWater.ions.bicarbonate, places: 0) }
            LabeledContent("Sparge volume (\(units.volumeSymbol))") {
                readout(plan.spargeLiters.map { units.displayVolume(liters: $0) })
            }
            if let difference = plan.mashDifference {
                LabeledContent("Target less mash, calcium (ppm)") { readout(difference.calcium, places: 0) }
                LabeledContent("Target less mash, magnesium (ppm)") { readout(difference.magnesium, places: 0) }
                LabeledContent("Target less mash, sodium (ppm)") { readout(difference.sodium, places: 0) }
                LabeledContent("Target less mash, sulfate (ppm)") { readout(difference.sulfate, places: 0) }
                LabeledContent("Target less mash, chloride (ppm)") { readout(difference.chloride, places: 0) }
                LabeledContent("Target less mash, bicarbonate (ppm)") { readout(difference.bicarbonate, places: 0) }
            }
        } header: {
            Text("Adjusted Water Profile")
        } footer: {
            Text("The mash water after its salts. The sparge makes up the pre-boil volume the recipe asks for.")
        }
    }

    private func analysisSection(_ plan: BrewingPlan) -> some View {
        Section {
            LabeledContent("Alkalinity (ppm as CaCO₃)") { readout(plan.mashWater.alkalinity, places: 0) }
            LabeledContent("Effective hardness (ppm as CaCO₃)") { readout(plan.mashWater.effectiveHardness, places: 0) }
            LabeledContent("Residual alkalinity (ppm as CaCO₃)") {
                readout(plan.mashWater.residualAlkalinity, places: 0)
            }
            LabeledContent("Sulfate to chloride ratio") { readout(plan.mashWater.sulfateToChloride, places: 2) }
            LabeledContent("Sulfate and chloride balance") { Text(plan.mashWater.balance) }
            LabeledContent("Water color range (SRM)") {
                Text(colorRange(plan)).monospacedDigit()
            }
        } header: {
            Text("Water Analysis")
        } footer: {
            Text("Effective hardness is calcium and magnesium as CaCO₃. The balance uses sulfate to chloride "
                + "below 0.5 (malt-forward), 0.5 to 1.5 (balanced), and above 1.5 (hop-forward). "
                + "The color range is Palmer's rough mapping from the residual alkalinity after the acids.")
        }
    }

    // MARK: Pieces

    private func ionRow(_ title: String, _ value: Binding<Double>) -> some View {
        LabeledContent(title) {
            TextField("", text: numberText(value, places: 1))
                .multilineTextAlignment(.trailing)
                .frame(width: 100)
                .onSubmit { commit() }
        }
    }

    private func colorRange(_ plan: BrewingPlan) -> String {
        guard let low = plan.colorLowSrm, let high = plan.colorHighSrm else { return "—" }
        return "\(low.formatted(.number.precision(.fractionLength(1))))–\(high.formatted(.number.precision(.fractionLength(1))))"
    }

    static let saltKinds = ["gypsum", "calcium chloride", "epsom salt", "magnesium chloride", "baking soda",
                            "table salt", "chalk", "pickling lime"]

    // MARK: Values

    private var result: Result<BrewingPlan, Error> {
        Result { try document.brewingPlan(recipe.id, BrewingInputs.from(planning.water, planning.mash)) }
    }

    /// The recipe's mash water, which a loaded profile's salts are scaled to. Nil when the plan does not work out.
    private var strikeLiters: Double? {
        if case .success(let plan) = result { return plan.strikeLiters }
        return nil
    }

    private func loadChosenProfile() {
        guard let profile = waterProfiles.profiles.first(where: { $0.id == chosenProfile }),
              let strikeLiters else { return }
        var water = planning.water
        profile.apply(to: &water, mashLiters: strikeLiters)
        planning.water = water
        commit()
        libraryMessage = "Loaded “\(profile.name)”."
    }

    private func removeChosenProfile() {
        guard let id = chosenProfile else { return }
        do {
            try waterProfiles.remove(id)
            chosenProfile = nil
            libraryMessage = nil
        } catch {
            libraryMessage = error.localizedDescription
        }
    }

    /// Starts from a catalog water, filling the base water from its figures.
    private func chooseBase(_ name: String) {
        planning.water.baseName = name
        if let water = Catalog.waters.first(where: { $0.name == name }) {
            planning.water.base = IonsInput(
                calcium: Double(water.calcium) ?? 0, magnesium: Double(water.magnesium) ?? 0,
                sodium: Double(water.sodium) ?? 0, sulfate: Double(water.sulfate) ?? 0,
                chloride: Double(water.chloride) ?? 0, bicarbonate: Double(water.bicarbonate) ?? 0)
        }
        commit()
    }

    private func load() {
        planning = RecipePlanning.read(recipe.planning)
    }

    private func commit() {
        let water = planning.water
        PlanningStore.update(recipe, document: document, store: store) { $0.water = water }
    }
}

/// The mash tab: the mash steps, the sparge, the starting conditions, the mash volume, and the
/// acids that bring the mash alkalinity down.
struct MashView: View {
    let document: RecipeDocument
    let recipe: RecipeSnapshot
    let store: DocumentStore

    @State private var planning = RecipePlanning()
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric

    var body: some View {
        Group {
            stepsSection
            spargeSection
            conditionsSection
            switch result {
            case .success(let plan):
                volumeSection(plan)
                acidSection(plan)
                spargeAcidSection(plan)
            case .failure(let error):
                Section {
                    Text(error.localizedDescription).foregroundStyle(.red)
                }
            }
        }
        .onAppear(perform: load)
        .onChange(of: recipe) { load() }
    }

    // MARK: Sections

    private var stepsSection: some View {
        Section {
            ForEach(planning.mash.steps.indices, id: \.self) { index in
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        TextField("Step name", text: $planning.mash.steps[index].name)
                            .onSubmit { commit() }
                        Picker("Kind", selection: Binding(
                            get: { planning.mash.steps[index].kind },
                            set: { planning.mash.steps[index].kind = $0; commit() })) {
                            ForEach(MashStepSetting.kinds, id: \.self) { Text($0).tag($0) }
                        }
                        .labelsHidden()
                        .frame(width: 130)
                        Button(role: .destructive) {
                            planning.mash.steps.remove(at: index)
                            commit()
                        } label: {
                            Image(systemName: "minus.circle")
                        }
                        .buttonStyle(.borderless)
                        .help("Remove this step")
                    }
                    HStack {
                        LabeledContent("Temperature (\(units.temperatureSymbol))") {
                            TextField("", text: numberText(units.temperature($planning.mash.steps[index].tempC), places: 1))
                                .multilineTextAlignment(.trailing).frame(width: 80)
                                .onSubmit { commit() }
                        }
                        LabeledContent("Time (min)") {
                            TextField("", text: numberText($planning.mash.steps[index].minutes, places: 0))
                                .multilineTextAlignment(.trailing).frame(width: 80)
                                .onSubmit { commit() }
                        }
                    }
                    TextField("Addition during this step, such as boiling water", text: $planning.mash.steps[index].addition)
                        .onSubmit { commit() }
                }
                .padding(.vertical, 2)
            }
            Button("Add Mash Step") {
                planning.mash.steps.append(MashStepSetting(name: "Rest", kind: "Temperature", tempC: 72, minutes: 15))
                commit()
            }
        } header: {
            Text("Mash Process")
        } footer: {
            Text("Total mash time: \(Int(planning.mash.steps.reduce(0) { $0 + $1.minutes })) minutes. "
                + "Infusion adds hot water, temperature changes only the heat, and decoction moves part of the mash to boil.")
        }
    }

    private var spargeSection: some View {
        Section {
            Picker("Sparge type", selection: Binding(
                get: { planning.mash.spargeType },
                set: { planning.mash.spargeType = $0; commit() })) {
                Text("Batch").tag("Batch")
                Text("Fly").tag("Fly")
                Text("No sparge").tag("No sparge")
            }
            LabeledContent("Sparge temperature (\(units.temperatureSymbol))") {
                TextField("", text: numberText(units.temperature($planning.mash.spargeTempC), places: 1))
                    .multilineTextAlignment(.trailing).frame(width: 100)
                    .onSubmit { commit() }
            }
            LabeledContent("Sparge water (\(units.volumeSymbol))") {
                readout(plan?.spargeLiters.map { units.displayVolume(liters: $0) })
            }
        } header: {
            Text("Sparge")
        } footer: {
            Text("The sparge water is worked out from the pre-boil volume and the first runnings.")
        }
    }

    private var conditionsSection: some View {
        Section {
            LabeledContent("Grain temperature (\(units.temperatureSymbol))") {
                TextField("", text: numberText(units.temperature($planning.mash.grainTempC), places: 1))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            LabeledContent("Mash tun temperature (\(units.temperatureSymbol))") {
                TextField("", text: numberText(units.temperature($planning.mash.tunTempC), places: 1))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            LabeledContent("Decoction boil temperature (\(units.temperatureSymbol))") {
                TextField("", text: numberText(units.temperature($planning.mash.decoctionTempC), places: 1))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            LabeledContent("Mash water (\(units.grainRatioSymbol) of grain)") {
                TextField("", text: numberText($planning.mash.mashRatioLPerKg, places: 2, factor: units.grainRatioFactor))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            LabeledContent("Grain weight (kg)") { readout(plan?.grainKg, places: 2) }
            LabeledContent("Grain absorption (\(units.grainRatioSymbol))") {
                TextField("", text: numberText($planning.mash.absorptionLPerKg, places: 2, factor: units.grainRatioFactor))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            LabeledContent("Water the grain absorbs (\(units.volumeSymbol))") {
                readout(plan.map { units.displayVolume(liters: $0.absorptionLiters) })
            }
        } header: {
            Text("Initial Mash Conditions")
        }
    }

    private func volumeSection(_ plan: BrewingPlan) -> some View {
        Section {
            LabeledContent("Mash tun addition (\(units.volumeSymbol))") {
                readout(units.displayVolume(liters: plan.strikeLiters))
            }
            LabeledContent("Tun deadspace (\(units.volumeSymbol))") {
                TextField("", text: numberText($planning.mash.deadspaceLiters, places: 2, factor: units.volumeFactor))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            Toggle("Adjust mash volume for deadspace", isOn: Binding(
                get: { planning.mash.adjustForDeadspace },
                set: { planning.mash.adjustForDeadspace = $0; commit() }))
            LabeledContent("Mash volume needed (\(units.volumeSymbol))") {
                readout(units.displayVolume(liters: plan.mashVolumeLiters))
            }
            LabeledContent("Mash tun volume (\(units.volumeSymbol))") {
                readout(units.displayVolume(liters: plan.mashTunVolumeLiters))
            }
            LabeledContent("Strike water temperature (\(units.temperatureSymbol))") {
                readout(plan.strikeTempC.map { units.displayTemperature(celsius: $0) })
            }
        } header: {
            Text("Mash Volume Needed")
        } footer: {
            Text("The mash tun volume adds the grain's displacement, about 0.7 L per kg, to the mash volume.")
        }
    }

    private func acidSection(_ plan: BrewingPlan) -> some View {
        Section {
            ForEach(planning.mash.acids.indices, id: \.self) { index in
                HStack {
                    Picker("Acid", selection: Binding(
                        get: { planning.mash.acids[index].kind },
                        set: { planning.mash.acids[index].kind = $0; commit() })) {
                        ForEach(Self.acidKinds, id: \.self) { Text($0).tag($0) }
                    }
                    .labelsHidden()
                    TextField("g", text: numberText($planning.mash.acids[index].grams))
                        .multilineTextAlignment(.trailing).frame(width: 80)
                        .onSubmit { commit() }
                    Button(role: .destructive) {
                        planning.mash.acids.remove(at: index)
                        commit()
                    } label: {
                        Image(systemName: "minus.circle")
                    }
                    .buttonStyle(.borderless)
                    .help("Remove this acid")
                }
            }
            Button("Add Acid") {
                planning.mash.acids.append(AcidInput())
                commit()
            }
            LabeledContent("Target mash pH") {
                TextField("", text: numberText($planning.mash.targetMashPh, places: 2))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            Picker("Acid for the target", selection: Binding(
                get: { planning.mash.acidForTarget },
                set: { planning.mash.acidForTarget = $0; commit() })) {
                ForEach(Self.acidKinds, id: \.self) { Text($0).tag($0) }
            }
            LabeledContent("Milliequivalents from the acids") { readout(plan.acidMilliequivalents, places: 1) }
            LabeledContent("Alkalinity after the acids (ppm as CaCO₃)") {
                readout(plan.alkalinityAfterAcid, places: 0)
            }
            LabeledContent("Grams of the target acid to reach the target pH") {
                readout(plan.acidGramsForTarget, places: 2)
            }
            LabeledContent("Estimated mash pH before the acids") { readout(plan.estimatedMashPhBeforeAcid, places: 2) }
            LabeledContent("Estimated mash pH after the acids") { readout(plan.estimatedMashPh, places: 2) }
        } header: {
            Text("Mash pH Acid Additions")
        } footer: {
            Text("The malt's acidity and the mash water's residual alkalinity, "
                + "with baking soda's bicarbonate scaled by the pH. It is an estimate, so measure the pH. The acid "
                + "blend is 45 % malic, 45 % citric, and 10 % tartaric, a wine blend, since no brewing blend is published.")
        }
    }

    private func spargeAcidSection(_ plan: BrewingPlan) -> some View {
        Section {
            Picker("Sparge acid", selection: Binding(
                get: { planning.mash.spargeAcid },
                set: { planning.mash.spargeAcid = $0; commit() })) {
                ForEach(Self.spargeAcidKinds, id: \.self) { Text($0).tag($0) }
            }
            LabeledContent("Strength (%)") {
                TextField("", text: numberText($planning.mash.spargeAcidStrength, places: 0))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            LabeledContent("Sparge water pH before the acid") {
                TextField("", text: numberText($planning.mash.spargeStartPh, places: 2))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            LabeledContent("Target sparge pH") {
                TextField("", text: numberText($planning.mash.spargeTargetPh, places: 2))
                    .multilineTextAlignment(.trailing).frame(width: 100).onSubmit { commit() }
            }
            LabeledContent("Sparge acid (mL)") { readout(plan.spargeAcidMilliliters, places: 2) }
            LabeledContent("Sparge acid, as sold (g)") { readout(plan.spargeAcidGrams, places: 2) }
        } header: {
            Text("Sparge Acidification")
        } footer: {
            Text("The acid that takes the sparge water to its target pH, from its alkalinity using carbonate "
                + "titration. Phosphoric and lactic acid are the usual choices.")
        }
    }

    static let spargeAcidKinds = ["phosphoric acid", "lactic acid", "sulfuric acid", "hydrochloric acid", "acetic acid",
                                  "citric acid", "tartaric acid", "malic acid"]
    static let acidKinds = ["lactic acid", "phosphoric acid", "acid blend", "malic acid", "ascorbic acid"]

    // MARK: Values

    private var result: Result<BrewingPlan, Error> {
        Result { try document.brewingPlan(recipe.id, BrewingInputs.from(planning.water, planning.mash)) }
    }

    private var plan: BrewingPlan? {
        try? result.get()
    }

    private func load() {
        planning = RecipePlanning.read(recipe.planning)
    }

    private func commit() {
        let mash = planning.mash
        PlanningStore.update(recipe, document: document, store: store) { $0.mash = mash }
    }
}
