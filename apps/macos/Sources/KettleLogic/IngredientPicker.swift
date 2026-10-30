import SwiftUI

/// A list with fewer entries than this stays a plain menu. A longer one gets the picker.
let ingredientPickerThreshold = 15

/// Chooses one ingredient from a long list: search, sort, filters, and a line of detail under each
/// name, so the user does not have to open the directory to compare them. The engine finds and
/// orders the entries (see `IngredientSearch.swift`), so this view only draws its answer.
///
/// The search field keeps the keyboard focus. Typing narrows the list, the arrow keys move the
/// highlight, and Return picks the highlighted ingredient, which is the first match until the
/// arrow keys move it. Clicking a row picks it too.
struct IngredientPicker: View {
    let kind: IngredientKind
    let entries: [MenuEntry]
    let pick: (MenuEntry) -> Void

    @State private var search = IngredientSearch()
    @State private var highlighted: String?
    @State private var showsRange = false
    @FocusState private var searchFocused: Bool

    private let searchEntries: [IngredientSearchEntry]

    init(
        kind: IngredientKind, entries: [MenuEntry], search: IngredientSearch = IngredientSearch(),
        pick: @escaping (MenuEntry) -> Void
    ) {
        self.kind = kind
        self.entries = entries
        self.pick = pick
        _search = State(initialValue: search)
        searchEntries = entries.map(\.searchEntry)
    }

    var body: some View {
        let result = IngredientSearchEngine.run(kind: kind, entries: searchEntries, search: search)
        let shown = result.map { $0.indices.map { entries[$0] } } ?? entries
        let current = shown.first { $0.id == highlighted } ?? shown.first

        VStack(spacing: 0) {
            searchField(shown: shown, current: current)
            controls(result: result, count: shown.count)
            chips(result: result)
            Divider()
            if shown.isEmpty {
                emptyState
            } else {
                list(shown, current: current)
            }
        }
        .frame(width: 480, height: 520, alignment: .top)
        .onAppear { searchFocused = true }
    }

    // MARK: Search

    private func searchField(shown: [MenuEntry], current: MenuEntry?) -> some View {
        HStack {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            TextField("Search \(kind.title.lowercased()) ingredients", text: $search.text)
                .textFieldStyle(.plain)
                .focused($searchFocused)
                .onSubmit { if let current { pick(current) } }
                .onKeyPress(.downArrow) { move(by: 1, in: shown, from: current) }
                .onKeyPress(.upArrow) { move(by: -1, in: shown, from: current) }
            if !search.text.isEmpty {
                Button("Clear Search", systemImage: "xmark.circle.fill") { search.text = "" }
                    .labelStyle(.iconOnly)
                    .buttonStyle(.plain)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(10)
    }

    private func move(by step: Int, in shown: [MenuEntry], from current: MenuEntry?) -> KeyPress.Result {
        guard let current, let index = shown.firstIndex(where: { $0.id == current.id }) else { return .ignored }
        highlighted = shown[min(max(index + step, 0), shown.count - 1)].id
        return .handled
    }

    // MARK: Sort and filters

    private func controls(result: IngredientSearchResult?, count: Int) -> some View {
        HStack {
            sortMenu(metrics: result?.metrics)
            filterMenu(facets: result?.facets)
            if let metric = result?.metrics.primary {
                Button("\(metric.label) Range", systemImage: "slider.horizontal.3") { showsRange = true }
                    .popover(isPresented: $showsRange) { rangeEditor(metric: metric, facets: result?.facets) }
            }
            Spacer()
            Text(count == entries.count ? "\(count) ingredients" : "\(count) of \(entries.count)")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .menuStyle(.button)
        .padding(.horizontal, 10)
        .padding(.bottom, 8)
    }

    private func sortMenu(metrics: IngredientSearchResult.Metrics?) -> some View {
        Menu {
            Picker("Sort by", selection: $search.sort) {
                Text("Catalog order").tag(IngredientSortKey.catalog)
                Text("Name").tag(IngredientSortKey.name)
                Text("Group").tag(IngredientSortKey.group)
                if let metric = metrics?.primary { Text(metric.title).tag(IngredientSortKey.primary) }
                if let metric = metrics?.secondary { Text(metric.title).tag(IngredientSortKey.secondary) }
            }
            .pickerStyle(.inline)
            Divider()
            Picker("Direction", selection: $search.descending) {
                Text("Ascending").tag(false)
                Text("Descending").tag(true)
            }
            .pickerStyle(.inline)
        } label: {
            Label("Sort", systemImage: "arrow.up.arrow.down")
        }
        .help("Choose how ingredients are ordered")
    }

    private func filterMenu(facets: IngredientSearchResult.Facets?) -> some View {
        Menu {
            if let facets {
                // A facet with one value cannot narrow anything, so it is not offered.
                filterSubmenu("Group", values: facets.groups, selection: $search.groups)
                filterSubmenu("Type", values: facets.types, selection: $search.types)
                filterSubmenu("Form", values: facets.forms, selection: $search.forms)
                if entries.contains(where: \.isCustom) && entries.contains(where: { !$0.isCustom }) {
                    Picker("Source", selection: $search.source) {
                        Text("All").tag(IngredientSourceFilter.any)
                        Text("Built in").tag(IngredientSourceFilter.builtIn)
                        Text("Yours").tag(IngredientSourceFilter.custom)
                    }
                }
            }
            if search.isFiltering {
                Divider()
                Button("Clear Filters") { search.clearFilters() }
            }
        } label: {
            Label("Filter", systemImage: search.isFiltering
                ? "line.3.horizontal.decrease.circle.fill" : "line.3.horizontal.decrease.circle")
        }
        .help("Show only some ingredients")
    }

    @ViewBuilder
    private func filterSubmenu(_ title: String, values: [String], selection: Binding<Set<String>>) -> some View {
        if values.count > 1 {
            Menu(title) {
                ForEach(values, id: \.self) { value in
                    Toggle(value.capitalized, isOn: Binding(
                        get: { selection.wrappedValue.contains(value) },
                        set: { on in
                            if on { selection.wrappedValue.insert(value) } else { selection.wrappedValue.remove(value) }
                        }))
                }
            }
        }
    }

    private func rangeEditor(metric: IngredientSearchResult.Metric, facets: IngredientSearchResult.Facets?) -> some View {
        Form {
            TextField("At least", value: $search.minPrimary, format: .number, prompt: Text(prompt(facets?.primaryMin)))
            TextField("At most", value: $search.maxPrimary, format: .number, prompt: Text(prompt(facets?.primaryMax)))
            Button("Clear") {
                search.minPrimary = nil
                search.maxPrimary = nil
            }
            .disabled(search.minPrimary == nil && search.maxPrimary == nil)
        }
        .formStyle(.columns)
        .navigationTitle(metric.title)
        .padding()
        .frame(width: 220)
    }

    private func prompt(_ value: Double?) -> String {
        value.map { $0.formatted(.number.precision(.fractionLength(0...2))) } ?? ""
    }

    /// The active filters, each removable with a click.
    @ViewBuilder
    private func chips(result: IngredientSearchResult?) -> some View {
        if search.isFiltering {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    ForEach(search.groups.sorted(), id: \.self) { value in chip(value) { search.groups.remove(value) } }
                    ForEach(search.types.sorted(), id: \.self) { value in chip(value.capitalized) { search.types.remove(value) } }
                    ForEach(search.forms.sorted(), id: \.self) { value in chip(value.capitalized) { search.forms.remove(value) } }
                    if search.source != .any {
                        chip(search.source == .custom ? "Yours" : "Built in") { search.source = .any }
                    }
                    if search.minPrimary != nil || search.maxPrimary != nil {
                        chip(rangeText(result?.metrics.primary)) {
                            search.minPrimary = nil
                            search.maxPrimary = nil
                        }
                    }
                    Button("Clear All") { search.clearFilters() }.buttonStyle(.link)
                }
                .padding(.horizontal, 10)
            }
            .padding(.bottom, 8)
        }
    }

    private func chip(_ title: String, remove: @escaping () -> Void) -> some View {
        Button(action: remove) {
            Label(title, systemImage: "xmark").labelStyle(ChipLabelStyle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel("\(title), filter on")
        .accessibilityHint("Removes this filter")
    }

    private func rangeText(_ metric: IngredientSearchResult.Metric?) -> String {
        let name = metric?.title ?? "Value"
        switch (search.minPrimary, search.maxPrimary) {
        case let (low?, high?): return "\(name): \(prompt(low))–\(prompt(high))"
        case let (low?, nil): return "\(name): ≥ \(prompt(low))"
        case let (nil, high?): return "\(name): ≤ \(prompt(high))"
        case (nil, nil): return name
        }
    }

    // MARK: Results

    private func list(_ shown: [MenuEntry], current: MenuEntry?) -> some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(shown) { entry in
                        Button { pick(entry) } label: {
                            PickerRow(entry: entry, highlighted: entry.id == current?.id)
                        }
                        .buttonStyle(.plain)
                        .id(entry.id)
                    }
                }
                .padding(6)
            }
            .onChange(of: current?.id) { _, id in
                if let id { proxy.scrollTo(id) }
            }
            // A new search starts at the top.
            .onChange(of: search) { proxy.scrollTo(shown.first?.id, anchor: .top) }
        }
    }

    private var emptyState: some View {
        ContentUnavailableView {
            Label("No Matching Ingredients", systemImage: "magnifyingglass")
        } description: {
            Text(search.isFiltering ? "Try other words, or take off a filter." : "Try other words.")
        } actions: {
            if search.isFiltering { Button("Clear Filters") { search.clearFilters() } }
        }
    }
}

/// An ingredient's name and group, with its key figures under them.
private struct PickerRow: View {
    let entry: MenuEntry
    let highlighted: Bool

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            VStack(alignment: .leading, spacing: 2) {
                Text(entry.name)
                Text(entry.draft.summary)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }
            Spacer(minLength: 8)
            Text(entry.isCustom ? "Yours" : entry.group)
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
        .accessibilityAddTraits(highlighted ? .isSelected : [])
    }
}

private struct ChipLabelStyle: LabelStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack(spacing: 4) {
            configuration.title
            configuration.icon.font(.caption2)
        }
        .font(.caption)
        .padding(.vertical, 3)
        .padding(.horizontal, 8)
        .background(.quaternary, in: .capsule)
    }
}
