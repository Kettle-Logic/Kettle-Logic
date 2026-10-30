import SwiftUI

/// Chooses the style a recipe is compared against: the BJCP styles and the user's own, with a search field
/// above the list. It works like `IngredientPicker`: typing narrows the list, the arrow keys move the
/// highlight, Return picks the highlighted style (the first match until the arrow keys move it), and
/// clicking a row picks it.
struct StylePicker: View {
    let current: String?
    let userStyles: [UserStyle]
    let pick: (String?) -> Void

    @State private var text = ""
    @State private var highlighted: String?
    @FocusState private var searchFocused: Bool

    var body: some View {
        let shown = matches
        let chosen = shown.first { $0.id == highlighted } ?? shown.first

        VStack(spacing: 0) {
            searchField(shown: shown, chosen: chosen)
            Divider()
            if shown.isEmpty {
                ContentUnavailableView.search(text: text)
            } else {
                list(shown, chosen: chosen)
            }
        }
        .frame(width: 480, height: 520, alignment: .top)
        .onAppear { searchFocused = true }
    }

    // MARK: Search

    /// The choices that match every word typed. "None" is offered only while nothing is typed.
    private var matches: [StyleChoice] {
        let words = text.lowercased().split(separator: " ")
        let all = bjcpStyles.map(StyleChoice.init(bjcp:)) + userStyles.map(StyleChoice.init(own:))
        let found = all.filter { choice in
            let searched = "\(choice.title) \(choice.group) \(choice.detail)".lowercased()
            return words.allSatisfy { searched.contains($0) }
        }
        return words.isEmpty ? [.noStyle] + found : found
    }

    private func searchField(shown: [StyleChoice], chosen: StyleChoice?) -> some View {
        HStack {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            TextField("Search styles", text: $text)
                .textFieldStyle(.plain)
                .focused($searchFocused)
                .onSubmit { if let chosen { pick(chosen.style) } }
                .onKeyPress(.downArrow) { move(by: 1, in: shown, from: chosen) }
                .onKeyPress(.upArrow) { move(by: -1, in: shown, from: chosen) }
            if !text.isEmpty {
                Button("Clear Search", systemImage: "xmark.circle.fill") { text = "" }
                    .labelStyle(.iconOnly)
                    .buttonStyle(.plain)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(10)
    }

    private func move(by step: Int, in shown: [StyleChoice], from chosen: StyleChoice?) -> KeyPress.Result {
        guard let chosen, let index = shown.firstIndex(where: { $0.id == chosen.id }) else { return .ignored }
        highlighted = shown[min(max(index + step, 0), shown.count - 1)].id
        return .handled
    }

    // MARK: Results

    private func list(_ shown: [StyleChoice], chosen: StyleChoice?) -> some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(shown) { choice in
                        Button { pick(choice.style) } label: {
                            StyleRow(
                                choice: choice, highlighted: choice.id == chosen?.id,
                                isCurrent: choice.style == current)
                        }
                        .buttonStyle(.plain)
                        .id(choice.id)
                    }
                }
                .padding(6)
            }
            .onChange(of: chosen?.id) { _, id in
                if let id { proxy.scrollTo(id) }
            }
            // A new search starts at the top.
            .onChange(of: text) { proxy.scrollTo(shown.first?.id, anchor: .top) }
        }
    }
}

/// One choice in the style picker. `id` is the style's id, or "" for no style.
struct StyleChoice: Identifiable {
    let id: String
    let title: String
    /// The guideline category for a BJCP style, or "Yours" for the user's own.
    let group: String
    /// The figures the style is held to, under the name.
    let detail: String

    static let noStyle = StyleChoice(id: "", title: "None", group: "", detail: "The recipe is not compared with a style")

    /// The id the recipe stores for this choice, or nil for no style.
    var style: String? { id.isEmpty ? nil : id }

    init(id: String, title: String, group: String, detail: String) {
        self.id = id
        self.title = title
        self.group = group
        self.detail = detail
    }

    init(bjcp style: BJCPStyle) {
        self.init(id: style.id, title: style.title, group: "Category \(style.category)", detail: style.summary)
    }

    init(own style: UserStyle) {
        self.init(id: style.id, title: style.name, group: "Yours", detail: "Your own style")
    }
}

/// A style's name and figures, with its guideline category at the right.
private struct StyleRow: View {
    let choice: StyleChoice
    let highlighted: Bool
    let isCurrent: Bool

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            VStack(alignment: .leading, spacing: 2) {
                Text(choice.title)
                if !choice.detail.isEmpty {
                    Text(choice.detail)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(2)
                }
            }
            Spacer(minLength: 8)
            if isCurrent {
                Image(systemName: "checkmark").foregroundStyle(.secondary)
            }
            Text(choice.group)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .frame(maxWidth: 140, alignment: .trailing)
        }
        .padding(.vertical, 5)
        .padding(.horizontal, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(highlighted ? Color.accentColor.opacity(0.18) : .clear, in: .rect(cornerRadius: 6))
        .contentShape(.rect)
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(isCurrent ? .isSelected : [])
    }
}
