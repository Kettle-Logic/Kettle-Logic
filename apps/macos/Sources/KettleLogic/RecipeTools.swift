import SwiftUI

/// A calculator opened from a recipe, with the recipe's own figures already in its fields.
struct ToolRequest: Identifiable {
    let kind: ToolKind
    let prefill: [String: String]
    let id = UUID()
}

/// A calculator in a sheet over the recipe. The tools are metric, so the figures are converted to match.
struct ToolSheet: View {
    let request: ToolRequest

    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ToolView(kind: request.kind, prefill: request.prefill)
                .toolbar {
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Done") { dismiss() }
                    }
                }
        }
        .frame(minWidth: 480, minHeight: 420)
    }
}

/// A button that opens a calculator for the recipe.
struct CalculatorButton: View {
    let title: String
    let systemImage: String
    let open: () -> Void

    var body: some View {
        Button(title, systemImage: systemImage, action: open)
            .help("Opens a calculator with this recipe's figures filled in")
    }
}

extension Quantity {
    /// The value in liters, for the volume units the app offers, or nil for any other unit or a non-number.
    var liters: Double? {
        guard let number = Double(value) else { return nil }
        switch unit.lowercased() {
        case "l": return number
        case "ml": return number / 1000
        case "gal": return number * UnitSystem.litersPerGallon
        case "qt": return number * UnitSystem.litersPerGallon / 4
        case "pt": return number * UnitSystem.litersPerGallon / 8
        case "floz": return number * UnitSystem.litersPerGallon / 128
        default: return nil
        }
    }

    /// The value in grams, for the mass units the app offers, or nil for any other unit or a non-number.
    var grams: Double? {
        guard let number = Double(value) else { return nil }
        switch unit.lowercased() {
        case "g": return number
        case "kg": return number * 1000
        case "oz": return number * 28.349523125
        case "lb": return number * 453.59237
        default: return nil
        }
    }
}

extension RecipeSnapshot {
    /// The yeast starter calculator's fields, from the batch, the gravity, and the first culture.
    var starterPrefill: [String: String] {
        var fields: [String: String] = [:]
        if let liters = batchSize.liters { fields["batchLiters"] = format(liters) }
        if let og = results.originalGravity { fields["originalGravity"] = format(og, places: 3) }
        if cultures.first?.type == "lager" { fields["lager"] = "1" }
        return fields
    }

    /// The hop bitterness calculator's fields for one hop, from the hop, the volume, and the gravity.
    func bitternessPrefill(for hop: HopAddition) -> [String: String] {
        var fields: [String: String] = ["alphaPercent": hop.alphaAcid.value]
        if let grams = hop.amount.grams { fields["grams"] = format(grams) }
        if let liters = results.postBoilLiters ?? batchSize.liters { fields["postBoilLiters"] = format(liters) }
        if let minutes = hop.time, minutes.unit == "min" { fields["boilMinutes"] = minutes.value }
        if let og = results.originalGravity { fields["gravity"] = format(og, places: 3) }
        return fields
    }

    private func format(_ value: Double, places: Int = 2) -> String {
        String(format: "%.\(places)f", value)
    }
}
