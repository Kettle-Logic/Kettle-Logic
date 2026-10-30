import SwiftUI

/// The style editor's request: a style to change, or nil for a new one.
struct StyleRequest: Identifiable {
    let id = UUID()
    let style: UserStyle?
}

/// Makes a beer style of the user's own, or changes one. Each range is typed as two plain numbers, low end first,
/// and each is checked before the style is saved, so the engine only refuses what the user can see.
struct StyleEditor: View {
    let request: StyleRequest
    let save: (UserStyle) throws -> Void
    /// Removes the style being edited. Nil for a new style, which has nothing to remove.
    let remove: (() throws -> Void)?

    /// The ranges in the order the editor shows them, with the title of each.
    static let figures: [(title: String, path: WritableKeyPath<UserStyle, StyleRange>)] = [
        ("Original gravity", \.og),
        ("Final gravity", \.fg),
        ("Bitterness (IBU)", \.ibu),
        ("Color (SRM)", \.srm),
        ("Alcohol (% by volume)", \.abv),
    ]

    @Environment(\.dismiss) private var dismiss
    @State private var name: String
    @State private var lows: [String]
    @State private var highs: [String]
    @State private var errorMessage: String?

    init(request: StyleRequest, save: @escaping (UserStyle) throws -> Void, remove: (() throws -> Void)?) {
        self.request = request
        self.save = save
        self.remove = remove
        let style = request.style ?? .blank
        _name = State(initialValue: style.name)
        _lows = State(initialValue: Self.figures.map { Self.text(style[keyPath: $0.path].low) })
        _highs = State(initialValue: Self.figures.map { Self.text(style[keyPath: $0.path].high) })
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(request.style == nil ? "New Style" : "Edit Style").font(.headline)
            Form {
                Section {
                    TextField("Name", text: $name)
                } footer: {
                    Text("Your style is compared with a recipe the same way as a BJCP style. Enter each range "
                        + "as a plain number, such as 1.040.")
                }

                Section("Ranges") {
                    ForEach(Self.figures.indices, id: \.self) { index in
                        LabeledContent(Self.figures[index].title) {
                            HStack(spacing: 6) {
                                TextField("Low", text: $lows[index])
                                    .frame(width: 90)
                                Text("–").foregroundStyle(.secondary)
                                TextField("High", text: $highs[index])
                                    .frame(width: 90)
                            }
                        }
                    }
                }
            }
            .formStyle(.grouped)
            .textFieldStyle(.roundedBorder)

            if let errorMessage {
                Text(errorMessage).foregroundStyle(.red)
            }

            HStack {
                if let remove {
                    Button("Delete", role: .destructive) { delete(remove) }
                }
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Save", action: saveStyle)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 460)
    }

    private func saveStyle() {
        let trimmedName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedName.isEmpty else {
            errorMessage = "A style needs a name."
            return
        }
        var style = request.style ?? .blank
        style.name = trimmedName
        for (index, figure) in Self.figures.enumerated() {
            let low = lows[index].trimmingCharacters(in: .whitespacesAndNewlines)
            let high = highs[index].trimmingCharacters(in: .whitespacesAndNewlines)
            guard isPlainNumber(low), isPlainNumber(high), let lowValue = Double(low), let highValue = Double(high) else {
                errorMessage = "The \(figure.title) range needs two plain numbers, such as 1.040."
                return
            }
            guard lowValue <= highValue else {
                errorMessage = "The low end of the \(figure.title) range is above its high end."
                return
            }
            style[keyPath: figure.path] = StyleRange(low: lowValue, high: highValue)
        }
        do {
            try save(style)
            dismiss()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func delete(_ remove: () throws -> Void) {
        do {
            try remove()
            dismiss()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    /// Whether `text` is a plain decimal number, such as "1.040". The engine reads exact decimal text, so a sign, a
    /// comma, or an exponent is refused.
    private func isPlainNumber(_ text: String) -> Bool {
        !text.isEmpty && text.allSatisfy({ "0123456789.".contains($0) }) && text.filter({ $0 == "." }).count <= 1
    }

    /// A number as the editor shows it: without a trailing ".0".
    private static func text(_ value: Double) -> String {
        let text = String(value)
        return text.hasSuffix(".0") ? String(text.dropLast(2)) : text
    }
}
