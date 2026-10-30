import SwiftUI

/// The recipe's headline numbers, pinned above the tabs so they stay in view while ingredients change.
/// With a style chosen, each number is marked by whether it falls inside the style's range. Clicking
/// the strip opens the Results tab.
struct ResultsStrip: View {
    let recipe: RecipeSnapshot
    let comparison: StyleComparison?
    let showsVolumes: Bool
    let open: () -> Void

    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric

    var body: some View {
        Button(action: open) {
            HStack(alignment: .top, spacing: 0) {
                tile("OG", term: .originalGravity, value: recipe.results.originalGravity, places: 3, metric: "originalGravity")
                tile("FG", term: .finalGravity, value: recipe.results.finalGravity, places: 3, metric: "finalGravity")
                tile("ABV", term: .abv, value: recipe.results.abv, places: 1, suffix: "%", metric: "abv")
                if !recipe.isFermentedDrink {
                    tile("IBU", term: .ibu, value: recipe.results.ibu, places: 0, metric: "ibu")
                    tile("Color", term: .srm, value: recipe.results.srm, places: 1, suffix: " SRM", metric: "srm",
                         swatch: recipe.results.srm.map(SRMColor.color(for:)))
                    if showsVolumes {
                        tile("Post-boil", term: nil, value: recipe.results.postBoilLiters.map { units.displayVolume(liters: $0) },
                             places: 1, suffix: " \(units.volumeSymbol)")
                    }
                }
                if let cost = recipe.results.cost {
                    costTile(cost)
                }
            }
            .padding(.vertical, 8)
            .padding(.horizontal, 12)
            .frame(maxWidth: .infinity)
            .background(.bar)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help("Show the full results")
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Results")
        .accessibilityHint("Opens the Results tab")
    }

    private func tile(
        _ title: String, term: Term?, value: Double?, places: Int, suffix: String = "", metric: String? = nil,
        swatch: Color? = nil
    ) -> some View {
        let fit = metric.flatMap { key in comparison?.checks.first { $0.metric == key }?.fit }
        return VStack(spacing: 2) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
            HStack(spacing: 4) {
                if let swatch {
                    Circle().fill(swatch).frame(width: 12, height: 12)
                        .overlay(Circle().stroke(.secondary.opacity(0.4), lineWidth: 0.5))
                }
                Text(value.map { $0.formatted(.number.precision(.fractionLength(places))) + suffix } ?? "—")
                    .font(.title3.weight(.semibold))
                    .monospacedDigit()
                if let fit, let mark = Self.mark(for: fit) {
                    Image(systemName: mark.symbol)
                        .foregroundStyle(mark.color)
                        .accessibilityLabel(mark.label)
                }
            }
        }
        .frame(maxWidth: .infinity)
        .help(helpText(term: term, value: value))
    }

    /// The term's explanation, plus where to find the reason when the value is missing.
    private func helpText(term: Term?, value: Double?) -> String {
        let explanation = term?.explanation ?? ""
        guard value == nil else { return explanation }
        return explanation + "\n\nNot calculated yet. See Calculation Notes under Results > Notes for what is missing."
    }

    /// The cost of the priced ingredients. Only a recipe with a price shows it, and the help says when
    /// some ingredients are not counted.
    private func costTile(_ cost: RecipeCost) -> some View {
        VStack(spacing: 2) {
            Text("Cost")
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(money(cost.total, currency: cost.currency))
                .font(.title3.weight(.semibold))
                .monospacedDigit()
        }
        .frame(maxWidth: .infinity)
        .help(
            cost.unpriced == 0
                ? "What the ingredients cost"
                : "What the priced ingredients cost. \(cost.unpriced) without a price are not counted.")
    }

    /// A symbol as well as a color, so the fit does not depend on seeing color.
    private static func mark(for fit: String) -> (symbol: String, color: Color, label: String)? {
        switch fit {
        case "within": ("checkmark.circle.fill", .green, "within the style range")
        case "below": ("arrow.down.circle.fill", .orange, "below the style range")
        case "above": ("arrow.up.circle.fill", .orange, "above the style range")
        default: nil
        }
    }
}

/// The color of a beer for an SRM value, by interpolating between published reference swatches.
enum SRMColor {
    private static let stops: [(srm: Double, rgb: (Double, Double, Double))] = [
        (1, (255, 230, 153)), (3, (255, 216, 120)), (6, (255, 191, 66)), (10, (248, 166, 0)),
        (15, (239, 148, 0)), (20, (229, 133, 0)), (25, (194, 107, 0)), (30, (143, 68, 0)),
        (35, (90, 42, 6)), (40, (46, 21, 7)), (50, (18, 10, 5)),
    ]

    static func color(for srm: Double) -> Color {
        let value = min(max(srm, stops[0].srm), stops[stops.count - 1].srm)
        let upperIndex = stops.firstIndex { $0.srm >= value } ?? stops.count - 1
        let upper = stops[upperIndex]
        let lower = stops[max(upperIndex - 1, 0)]
        let t = upper.srm == lower.srm ? 0 : (value - lower.srm) / (upper.srm - lower.srm)
        func mix(_ a: Double, _ b: Double) -> Double { (a + (b - a) * t) / 255 }
        return Color(red: mix(lower.rgb.0, upper.rgb.0), green: mix(lower.rgb.1, upper.rgb.1),
                     blue: mix(lower.rgb.2, upper.rgb.2))
    }
}
