import SwiftUI

/// One calculated value, its range, and where the value falls in that range. The range is the BJCP guideline's, or
/// the user's own when `ownStyle` is set.
struct StyleCheckRow: View {
    let check: StyleCheck
    var ownStyle = false

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            LabeledContent(metricTitle) {
                Text(valueText).monospacedDigit()
            }
            if let value = check.value {
                RangeBar(value: value, low: check.low, high: check.high)
            }
            Text("\(ownStyle ? "Your" : "BJCP") range \(rangeText) · \(fitText)")
                .font(.caption)
                .foregroundStyle(.secondary)
                .monospacedDigit()
        }
    }

    private var metricTitle: String {
        switch check.metric {
        case "originalGravity": "Original gravity"
        case "finalGravity": "Final gravity"
        case "abv": "Alcohol (% by volume)"
        case "ibu": "Bitterness (IBU)"
        case "srm": "Color (SRM)"
        default: check.metric
        }
    }

    /// Gravities show three places, as the results do. The other values show one.
    private var places: Int {
        check.metric.hasSuffix("ravity") ? 3 : 1
    }

    private var rangePlaces: Int {
        check.metric == "ibu" ? 0 : places
    }

    private var valueText: String {
        check.value.map { $0.formatted(.number.precision(.fractionLength(places))) } ?? "—"
    }

    private var rangeText: String {
        let low = check.low.formatted(.number.precision(.fractionLength(rangePlaces)))
        let high = check.high.formatted(.number.precision(.fractionLength(rangePlaces)))
        return "\(low)–\(high)"
    }

    private var fitText: String {
        switch check.fit {
        case "below": "below the range"
        case "above": "above the range"
        case "within": "within the range"
        default: "no value yet"
        }
    }
}

extension BJCPStyle {
    /// The guideline category the style sits in: 21 for "21A".
    var category: Int {
        Int(id.prefix { $0.isNumber }) ?? 0
    }

    /// The figures a picker row shows under the name, such as "OG 1.056–1.070 · ABV 5.5–7.5% · IBU 40–70 · SRM 6–14".
    var summary: String {
        [
            "OG \(guidelineRange(og, places: 3))",
            "ABV \(guidelineRange(abv, places: 1))%",
            "IBU \(guidelineRange(ibu, places: 0))",
            "SRM \(guidelineRange(srm, places: 1))",
        ].joined(separator: " · ")
    }
}

/// A guideline range as text, such as "1.056–1.070", with the places the figure is printed to.
func guidelineRange(_ range: BJCPStyle.Range, places: Int) -> String {
    "\(range.low.formatted(.number.precision(.fractionLength(places))))–\(range.high.formatted(.number.precision(.fractionLength(places))))"
}

/// The BJCP guideline's details for a style: its number, its category, and the range each vital statistic
/// is held to. Shown whether or not the recipe has values yet, so the brewer can read the style first.
struct BJCPStyleNotes: View {
    let style: BJCPStyle

    private static let guidelines = URL(string: "https://www.bjcp.org/style/2021/beer/")!

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            LabeledContent("BJCP style") {
                Text("\(style.id) \(style.name)")
            }
            LabeledContent("Category") {
                Text("\(style.category)").monospacedDigit()
            }
            LabeledContent("Original gravity") {
                Text(guidelineRange(style.og, places: 3)).monospacedDigit()
            }
            LabeledContent("Final gravity") {
                Text(guidelineRange(style.fg, places: 3)).monospacedDigit()
            }
            LabeledContent("Alcohol (% by volume)") {
                Text(guidelineRange(style.abv, places: 1)).monospacedDigit()
            }
            LabeledContent("Bitterness (IBU)") {
                Text(guidelineRange(style.ibu, places: 0)).monospacedDigit()
            }
            LabeledContent("Color (SRM)") {
                Text(guidelineRange(style.srm, places: 1)).monospacedDigit()
            }
            Link("BJCP 2021 Beer Style Guidelines", destination: Self.guidelines)
                .font(.caption)
        }
    }
}

/// A scale with the style's range shaded and a marker at the recipe's value. The scale
/// stretches to include the value, so a value outside the range still shows.
struct RangeBar: View {
    let value: Double
    let low: Double
    let high: Double

    var body: some View {
        GeometryReader { proxy in
            let scale = axis
            let width = proxy.size.width
            let x = { (point: Double) -> CGFloat in
                CGFloat((point - scale.lower) / (scale.upper - scale.lower)) * width
            }
            ZStack(alignment: .leading) {
                Capsule()
                    .fill(.quaternary)
                    .frame(height: 6)
                Capsule()
                    .fill(Color.accentColor.opacity(0.6))
                    .frame(width: max(x(high) - x(low), 2), height: 6)
                    .offset(x: x(low))
                Circle()
                    .fill(.primary)
                    .frame(width: 10, height: 10)
                    .offset(x: min(max(x(value) - 5, 0), width - 10))
            }
            .frame(maxHeight: .infinity)
        }
        .frame(height: 12)
        .accessibilityHidden(true)
    }

    /// The scale: the range and the value, with a tenth of the span added on each side.
    private var axis: (lower: Double, upper: Double) {
        let lower = min(low, value)
        let upper = max(high, value)
        let span = upper - lower
        if span == 0 {
            return (lower - 0.5, upper + 0.5)
        }
        return (lower - span * 0.1, upper + span * 0.1)
    }
}
