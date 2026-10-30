import SwiftUI

/// Starts a recipe from a style: pick a style, set the batch and brewing system, review, create. The
/// amounts come from `RecipeBuilder`, which works them out from the engine's results, so the review
/// shows the real gravity and bitterness before anything is added to the user's recipes.
struct NewRecipeWizard: View {
    let store: DocumentStore

    private enum Step: Int { case style, setup, review }

    @Environment(\.dismiss) private var dismiss
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric
    @State private var step = Step.style
    @State private var templateID = RecipeTemplate.all[0].id
    @State private var equipmentName = Preferences.defaultEquipmentName
    @State private var batchValue = ""
    @State private var preview: Preview?
    @State private var errorMessage: String?

    /// A recipe built in a scratch document, for the review step.
    private struct Preview {
        let recipe: RecipeSnapshot
        let comparison: StyleComparison?
    }

    private var template: RecipeTemplate {
        RecipeTemplate.all.first { $0.id == templateID } ?? RecipeTemplate.all[0]
    }

    private var profile: EquipmentProfile {
        store.equipment.profile(named: equipmentName)
    }

    private var request: RecipeBuildRequest {
        RecipeBuildRequest(template: template, batchValue: batchValue, profile: profile, units: units)
    }

    var body: some View {
        VStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 4) {
                Text("New Recipe from a Style").font(.title2.bold())
                Text(subtitle).foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding([.horizontal, .top], 20)
            .padding(.bottom, 12)

            Divider()
            Group {
                switch step {
                case .style: styleStep
                case .setup: setupStep
                case .review: reviewStep
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            Divider()

            if let errorMessage {
                Text(errorMessage).foregroundStyle(.red).padding(.horizontal, 20).padding(.top, 10)
            }
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Spacer()
                if step != .style {
                    Button("Back") { go(to: Step(rawValue: step.rawValue - 1) ?? .style) }
                }
                if step == .review {
                    Button("Create Recipe", action: create)
                        .keyboardShortcut(.defaultAction)
                        .disabled(preview == nil)
                } else {
                    Button("Next") { go(to: Step(rawValue: step.rawValue + 1) ?? .review) }
                        .keyboardShortcut(.defaultAction)
                        .disabled(step == .setup && request.batchLiters == nil)
                }
            }
            .padding(16)
        }
        .frame(width: 540, height: 600)
        .onAppear(perform: resetBatch)
        .onChange(of: equipmentName) { resetBatch() }
    }

    private var subtitle: String {
        switch step {
        case .style: "Step 1 of 3. What do you want to brew?"
        case .setup: "Step 2 of 3. How much, and on what equipment?"
        case .review: "Step 3 of 3. Check the recipe. You can change anything afterwards."
        }
    }

    // MARK: Steps

    private var styleStep: some View {
        List(RecipeTemplate.all, selection: Binding(get: { templateID }, set: { templateID = $0 ?? templateID })) { item in
            VStack(alignment: .leading, spacing: 2) {
                Text(item.name).font(.headline)
                Text(item.summary).font(.callout).foregroundStyle(.secondary)
                Text("OG \(item.targetOG.formatted(.number.precision(.fractionLength(3)))) · "
                    + "\(Int(item.targetIBU)) IBU · \(item.yeast.replacingOccurrences(of: "Fermentis SafAle ", with: ""))")
                    .font(.caption).foregroundStyle(.tertiary)
            }
            .padding(.vertical, 3)
            .tag(item.id)
        }
        .listStyle(.inset)
    }

    private var setupStep: some View {
        Form {
            Section {
                Picker("Brewing system", selection: $equipmentName) {
                    ForEach(store.equipment.all) { Text($0.name).tag($0.name) }
                }
                Text(profile.note).font(.caption).foregroundStyle(.secondary)
                LabeledContent("Batch size") {
                    HStack {
                        TextField("Batch size", text: $batchValue)
                            .multilineTextAlignment(.trailing)
                            .frame(width: 100)
                        Text(units.volumeSymbol)
                    }
                }
            } footer: {
                Text("The batch size is what you end up with in the fermenter. The amounts of grain and hops are "
                    + "worked out from it and from the efficiency of your system, to hit the style's strength and bitterness.")
            }
        }
        .formStyle(.grouped)
        .textFieldStyle(.roundedBorder)
    }

    private var reviewStep: some View {
        Group {
            if let preview {
                VStack(spacing: 0) {
                    ResultsStrip(recipe: preview.recipe, comparison: preview.comparison, showsVolumes: false) {}
                        .allowsHitTesting(false)
                    Divider()
                    Form {
                        Section("Grain") {
                            ForEach(preview.recipe.fermentables) { Text("\($0.amount.display)  \($0.name)") }
                        }
                        Section("Hops") {
                            ForEach(preview.recipe.hops) { Text("\($0.name): \($0.detail)") }
                        }
                        Section("Yeast") {
                            ForEach(preview.recipe.cultures) { Text($0.name) }
                        }
                        Section("Process") {
                            Text("Mash at \(units.temperatureText(celsius: template.mashTempC)) for 60 minutes. Boil \(template.boilMinutes) minutes.")
                            Text("Ferment at \(units.temperatureText(celsius: template.fermentTempC.lowerBound))–\(units.temperatureText(celsius: template.fermentTempC.upperBound)).")
                            ForEach(template.tips(in: units), id: \.self) { Text($0).foregroundStyle(.secondary) }
                        }
                    }
                    .formStyle(.grouped)
                }
            } else {
                ProgressView().frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }

    // MARK: Actions

    private func resetBatch() {
        batchValue = units.volumeText(liters: profile.batchLiters)
    }

    private func go(to next: Step) {
        errorMessage = nil
        step = next
        if next == .review { buildPreview() }
    }

    /// Builds the recipe in a throwaway document, so the review shows the engine's own numbers.
    private func buildPreview() {
        preview = nil
        do {
            let scratch = try RecipeDocument.makeNew()
            guard let first = scratch.snapshot.recipes.first else { return }
            try RecipeBuilder.populate(request, in: scratch, recipe: first.id)
            guard let built = scratch.snapshot.recipes.first else { return }
            preview = Preview(recipe: built, comparison: try? scratch.styleComparison(built.id))
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func create() {
        do {
            try store.newRecipe(building: request)
            dismiss()
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}
