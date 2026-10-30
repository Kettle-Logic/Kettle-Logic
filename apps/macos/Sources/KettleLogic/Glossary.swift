import SwiftUI

/// A brewing term, with a plain-language explanation for someone new to it. The engine owns the wording.
enum Term: String {
    case originalGravity, finalGravity, abv, ibu, srm
    case brewhouseEfficiency, attenuation, alphaAcid, betaAcid, evaporation, trubLoss, fermentationEfficiency

    private struct Entry: Decodable {
        let term: String
        let explanation: String
    }

    private static let explanations: [String: String] = Dictionary(
        uniqueKeysWithValues: BuiltinData.load("glossary", as: [Entry].self).map { ($0.term, $0.explanation) })

    var explanation: String { Self.explanations[rawValue] ?? "" }
}

/// A label for a field, with its term explained. Simple mode shows the explanation under the label,
/// since a new brewer is the one who needs it. Otherwise an (i) button opens it in a popover.
struct HelpLabel: View {
    let title: String
    let term: Term

    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true
    @State private var showsPopover = false

    init(_ title: String, term: Term) {
        self.title = title
        self.term = term
    }

    var body: some View {
        if simpleMode {
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                Text(term.explanation)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        } else {
            HStack(spacing: 4) {
                Text(title)
                Button {
                    showsPopover.toggle()
                } label: {
                    Image(systemName: "info.circle")
                }
                .buttonStyle(.borderless)
                .foregroundStyle(.secondary)
                .help(term.explanation)
                .accessibilityLabel("About \(title)")
                .popover(isPresented: $showsPopover, arrowEdge: .bottom) {
                    Text(term.explanation)
                        .padding()
                        .frame(width: 260, alignment: .leading)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
    }
}
