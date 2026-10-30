import SwiftUI

/// A section header with the list's title and a button that adds an item.
struct IngredientHeader: View {
    let title: String
    let add: () -> Void

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            Button("Add", systemImage: "plus", action: add)
                .buttonStyle(.borderless)
                .help("Add \(title.lowercased())")
        }
    }
}

/// An ingredient's name, which opens its editor, and a line of detail, with a
/// button and a context menu to duplicate or remove it.
struct IngredientRow: View {
    let name: String
    let detail: String
    let edit: () -> Void
    let duplicate: () -> Void
    let remove: () -> Void

    var body: some View {
        LabeledContent {
            HStack {
                Text(detail).monospacedDigit().foregroundStyle(.secondary)
                Button(role: .destructive, action: remove) {
                    Label("Remove \(name)", systemImage: "minus.circle")
                }
                .labelStyle(.iconOnly)
                .buttonStyle(.borderless)
                .help("Remove \(name)")
            }
        } label: {
            Button(name, action: edit)
                .buttonStyle(.plain)
                .help("Edit \(name)")
        }
        .contextMenu {
            Button("Edit", action: edit)
            Button("Duplicate", action: duplicate)
            Button("Remove", role: .destructive, action: remove)
        }
    }
}

/// The choices for a picker: the usual list, with the current value added when
/// the list does not have it, so an existing item keeps its own unit or type.
func options(_ base: [String], including current: String) -> [String] {
    base.contains(current) ? base : [current] + base
}

/// The name of a hop form, where an empty value means it is not set.
func hopFormName(_ form: String) -> String {
    form.isEmpty ? "Not set" : form
}

/// An ingredient editor to open on a recipe. A nil `itemID` adds an ingredient, and
/// an ID edits the recipe's ingredient with that ID.
struct RecipeIngredientRequest: Identifiable {
    let id: String
    let draft: IngredientDraft
    let itemID: UInt64?

    init(draft: IngredientDraft, itemID: UInt64?) {
        self.draft = draft
        self.itemID = itemID
        self.id = itemID.map { "\(draft.kind.rawValue)-\($0)" } ?? UUID().uuidString
    }
}

extension HopAddition {
    /// The alpha acid, the amount, and when it goes in, such as "Boil 60 min".
    var detail: String {
        var parts = ["\(alphaAcid.display) AA"]
        if let betaAcid { parts.append("\(betaAcid.display) BA") }
        parts.append(amount.display)
        let when = [use.map(hopUseName), time?.display].compactMap { $0 }
        if !when.isEmpty {
            parts.append(when.joined(separator: " "))
        }
        return parts.joined(separator: " · ")
    }
}

extension FermentableAddition {
    /// The amount, and the potential and color when they are set.
    var detail: String {
        var parts = [amount.display]
        if let potential { parts.append("\(potential.value) \(potential.unit)") }
        parts.append(color.display)
        return parts.joined(separator: " · ")
    }
}

extension CultureAddition {
    var detail: String {
        var parts = ["\(type) · \(form)", amount.display]
        if let attenuation { parts.append("\(attenuation.display) attenuation") }
        return parts.joined(separator: " · ")
    }
}

extension WaterAddition {
    /// The minerals in mg/L, and the volume if there is one.
    var detail: String {
        let minerals = [
            "Ca \(calcium.value)", "Mg \(magnesium.value)", "Na \(sodium.value)",
            "Cl \(chloride.value)", "SO4 \(sulfate.value)", "HCO3 \(bicarbonate.value)",
        ].joined(separator: " ")
        return [minerals + " mg/L", amount?.display].compactMap { $0 }.joined(separator: " · ")
    }
}

/// A price or cost shown in a recipe's currency, such as "$4.50".
func money(_ value: Double, currency: String) -> String {
    value.formatted(.currency(code: currency))
}

extension RecipeSnapshot {
    /// What an ingredient row adds when the ingredient has a price: the price, and what the amount costs,
    /// such as " · $4.50 / kg · $2.05". Empty without a price.
    func priceDetail(_ price: Price?, uuid: String) -> String {
        guard let price, let amount = Double(price.amount) else { return "" }
        var text = " · \(money(amount, currency: currency)) / \(price.per)"
        if let line = results.cost?.cost(of: uuid) {
            text += " · \(money(line, currency: currency))"
        }
        return text
    }
}
