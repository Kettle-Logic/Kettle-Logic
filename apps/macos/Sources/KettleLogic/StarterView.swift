import SwiftUI

/// The yeast starter's inputs. Numbers are kept as the text the user typed, which the engine reads exactly.
struct StarterSettings: Codable, Equatable {
    var lager = false
    var packCells = "100"
    var growth = "15"
    var starterLiters = "2"
    var starterGravity = "1.040"
    var twoStage = false
    var firstStageLiters = "1"
    var addToBottling = false

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        lager = try c.decodeIfPresent(Bool.self, forKey: .lager) ?? lager
        packCells = try c.decodeIfPresent(String.self, forKey: .packCells) ?? packCells
        growth = try c.decodeIfPresent(String.self, forKey: .growth) ?? growth
        starterLiters = try c.decodeIfPresent(String.self, forKey: .starterLiters) ?? starterLiters
        starterGravity = try c.decodeIfPresent(String.self, forKey: .starterGravity) ?? starterGravity
        twoStage = try c.decodeIfPresent(Bool.self, forKey: .twoStage) ?? twoStage
        firstStageLiters = try c.decodeIfPresent(String.self, forKey: .firstStageLiters) ?? firstStageLiters
        addToBottling = try c.decodeIfPresent(Bool.self, forKey: .addToBottling) ?? addToBottling
    }
}

/// The yeast plan the engine works out from the inputs.
struct StarterPlan: Decodable {
    struct Starter: Decodable {
        let liters: Double
        let gravity: Double
        let dryMaltOunces: Double
        let dryMaltGrams: Double
        let cellsWithStarterBillion: Double
        let addToBottlingLiters: Double
    }

    struct TwoStage: Decodable {
        let firstLiters: Double
        let secondLiters: Double
        let use: Bool
    }

    let targetGravity: Double
    let targetPlato: Double
    let batchLiters: Double
    let pitchRate: Double
    let cellsNeededBillion: Double
    let packsWithoutStarter: Int
    let cellsWithoutStarterBillion: Double
    let starter: Starter
    let oneStageLiters: Double
    let twoStage: TwoStage
    let notes: [String]
}

/// The yeast sections of the Starter tab. The inputs are saved with the recipe, and the
/// readouts are the engine's, so they follow the recipe's batch and gravity.
struct StarterView: View {
    let document: RecipeDocument
    let recipe: RecipeSnapshot
    let store: DocumentStore

    @State private var settings = StarterSettings()
    /// The starter sizes as typed, in the user's unit. They become liters in `settings` when Return is pressed.
    @State private var starterDraft = ""
    @State private var firstStageDraft = ""
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric

    var body: some View {
        Group {
            if let plan = planOrError.plan {
                sections(for: plan)
            } else if let message = planOrError.message {
                Section {
                    Text(message).foregroundStyle(.secondary)
                }
            }
        }
        .onAppear(perform: load)
        .onChange(of: recipe) { load() }
        .onChange(of: units) { load() }
    }

    private func sections(for plan: StarterPlan) -> some View {
        Group {
            Section {
                LabeledContent("Pitch") {
                    Text("\(plan.packsWithoutStarter) pack\(plan.packsWithoutStarter == 1 ? "" : "s") without a starter")
                }
                LabeledContent("Plan in use") { Text(planInUse(plan)) }
                if settings.twoStage {
                    LabeledContent("Starters") {
                        Text("\(volume(plan.twoStage.firstLiters)), then \(volume(plan.twoStage.secondLiters))")
                            .monospacedDigit()
                    }
                } else {
                    LabeledContent("Starter") { Text(volume(plan.oneStageLiters)).monospacedDigit() }
                }
            } header: {
                Text("Yeast Plan")
            } footer: {
                Text("The summary of the sections below. The plan in use is the two-stage starter when it is switched on.")
            }

            Section {
                Toggle("Lager yeast (1.5 million cells per mL per °P, not 0.75)", isOn: $settings.lager)
                    .onChange(of: settings.lager) { save() }
                LabeledContent("Cells per pack (billion)") {
                    numberField($settings.packCells)
                }
                LabeledContent("Cells needed (billion)") { readout(plan.cellsNeededBillion) }
                LabeledContent("Cells without starter (billion)") { readout(plan.cellsWithoutStarterBillion) }
                LabeledContent("Yeast packs to use without a starter") {
                    Text("\(plan.packsWithoutStarter)").monospacedDigit()
                }
            } header: {
                Text("Yeast Cells Needed")
            } footer: {
                Text("Cells needed = pitch rate × batch × degrees Plato, from the recipe's original gravity of "
                    + "\(plan.targetGravity.formatted(.number.precision(.fractionLength(3)))). "
                    + "The packs are rounded up.")
            }

            Section {
                LabeledContent("Starter size (\(units.starterSymbol))") { volumeField($starterDraft) }
                LabeledContent("Starter gravity (SG)") { numberField($settings.starterGravity) }
                LabeledContent("Growth (billion cells per L per °P)") { numberField($settings.growth) }
                LabeledContent("Dry malt needed (oz)") { readout(plan.starter.dryMaltOunces) }
                LabeledContent("Dry malt needed (g)") { readout(plan.starter.dryMaltGrams) }
                LabeledContent("Yeast cells with starter (billion)") { readout(plan.starter.cellsWithStarterBillion) }
                Toggle("The starter is added to the bottling volume", isOn: $settings.addToBottling)
                    .onChange(of: settings.addToBottling) { save() }
                LabeledContent("Volume the starter adds to the batch (\(units.starterSymbol))") {
                    readout(plan.starter.addToBottlingLiters * units.starterFactor)
                }
            } header: {
                Text("Liquid Yeast Starter Size")
            } footer: {
                Text("Dry malt uses 44 points per pound per gallon. Growth is an estimate: measure your own "
                    + "yeast before relying on it.")
            }

            Section {
                LabeledContent("Recommended starter size (\(units.starterSymbol))") { readout(plan.oneStageLiters * units.starterFactor) }
            } header: {
                Text("Starter Volume for One Stage Starter")
            } footer: {
                Text("The size of one starter that grows the pack to the cells needed.")
            }

            Section {
                Toggle("Use a two-stage starter", isOn: $settings.twoStage)
                    .onChange(of: settings.twoStage) { save() }
                LabeledContent("First stage (\(units.starterSymbol))") { volumeField($firstStageDraft) }
                LabeledContent("Recommended first starter (\(units.starterSymbol))") {
                    readout(plan.twoStage.firstLiters * units.starterFactor)
                }
                LabeledContent("Recommended second starter (\(units.starterSymbol))") {
                    readout(plan.twoStage.secondLiters * units.starterFactor)
                }
            } header: {
                Text("Starter Volumes for Two Stage Starter")
            } footer: {
                Text("The first stage grows the pack. The second stage, made from the first, reaches the cells needed.")
            }

            if !plan.notes.isEmpty {
                Section("Notes") {
                    ForEach(plan.notes, id: \.self) { Text($0).foregroundStyle(.secondary) }
                }
            }
        }
    }

    // MARK: Pieces

    /// The plan the user has chosen: the two-stage starter when it is on, otherwise the one-stage.
    private func planInUse(_ plan: StarterPlan) -> String {
        if settings.twoStage { return "Two-stage starter" }
        return plan.oneStageLiters > 0 ? "One-stage starter" : "The pack alone"
    }

    /// A starter volume given in liters, with its unit in the user's system.
    private func volume(_ liters: Double) -> String {
        "\(number(liters * units.starterFactor)) \(units.starterSymbol)"
    }

    /// The liters to store for a field. A field left as it was shown keeps its stored text, so rounding for
    /// display never changes a figure the user did not touch.
    private func litersKeeping(_ stored: String, typed: String) -> String {
        typed == units.starterText(liters: stored) ? stored : units.litersText(starter: typed)
    }

    /// A starter size typed in the user's unit, saved as liters when Return is pressed.
    private func volumeField(_ text: Binding<String>) -> some View {
        TextField("", text: text)
            .multilineTextAlignment(.trailing)
            .frame(width: 100)
            .onSubmit {
                settings.starterLiters = litersKeeping(settings.starterLiters, typed: starterDraft)
                settings.firstStageLiters = litersKeeping(settings.firstStageLiters, typed: firstStageDraft)
                save()
            }
    }

    private func number(_ value: Double) -> String {
        value.formatted(.number.precision(.fractionLength(2)))
    }

    /// A typed number, saved when Return is pressed.
    private func numberField(_ text: Binding<String>) -> some View {
        TextField("", text: text)
            .multilineTextAlignment(.trailing)
            .frame(width: 100)
            .onSubmit { save() }
    }

    private func readout(_ value: Double) -> some View {
        Text(value.formatted(.number.precision(.fractionLength(2)))).monospacedDigit()
    }

    // MARK: Values

    /// The engine's plan for the current inputs, or the reason it could not be made.
    private var planOrError: (plan: StarterPlan?, message: String?) {
        do {
            return (try document.starterPlan(recipe.id, settings), nil)
        } catch {
            return (nil, error.localizedDescription)
        }
    }

    private func load() {
        settings = RecipePlanning.read(recipe.planning).starter
        starterDraft = units.starterText(liters: settings.starterLiters)
        firstStageDraft = units.starterText(liters: settings.firstStageLiters)
    }

    /// Saves the inputs into the recipe's planning, keeping what other tabs stored there.
    private func save() {
        var planning = RecipePlanning.read(recipe.planning)
        planning.starter = settings
        guard let text = planning.text else { return }
        store.attempt { try document.setPlanning(recipe.id, to: text) }
    }
}
