import SwiftUI

/// One page of the ingredient directory: the user's own ingredients, or the built-in
/// ingredients of one kind. A built-in page is split into sections by catalog group.
enum LibraryPage: Hashable, Identifiable {
    case yours
    case kind(IngredientKind)
    case equipment

    static let all: [LibraryPage] = [.yours] + IngredientKind.allCases.map { .kind($0) } + [.equipment]

    var id: Self { self }

    var title: String {
        switch self {
        case .yours: "Your Ingredients"
        case .kind(let kind):
            switch kind {
            case .fermentable: "Fermentables"
            case .hop: "Hops"
            case .culture: "Cultures"
            case .water: "Water"
            case .miscellaneous: "Miscellaneous"
            }
        case .equipment: "Equipment"
        }
    }

    var symbol: String {
        switch self {
        case .yours: "person.crop.circle"
        case .kind(.fermentable): "square.stack.3d.up"
        case .kind(.hop): "leaf"
        case .kind(.culture): "ant"
        case .kind(.water): "drop"
        case .kind(.miscellaneous): "shippingbox"
        case .equipment: "flame"
        }
    }

    /// The built-in kinds this page lists.
    var builtInKinds: [IngredientKind] {
        switch self {
        case .yours, .equipment: []
        case .kind(let kind): [kind]
        }
    }

    /// Whether a custom ingredient of this kind belongs on this page.
    func includes(_ kind: IngredientKind) -> Bool {
        switch self {
        case .yours: true
        case .kind(let own): own == kind
        case .equipment: false
        }
    }
}

/// One page of the ingredient directory. Built-in entries are copied, never changed:
/// opening one starts an editor from its values, so a copy can be saved as a custom
/// ingredient. Custom ingredients can be edited and deleted here.
struct IngredientDirectoryView: View {
    let store: DocumentStore
    let page: LibraryPage

    @State private var search = ""
    @State private var request: LibraryRequest?

    var body: some View {
        let custom = store.customIngredients.ingredients.filter {
            page.includes($0.draft.kind) && matches($0.draft.name, $0.draft.kind.title)
        }
        let hasResults = !custom.isEmpty || page.builtInKinds.contains { !entries(for: $0).isEmpty }

        List {
            if !custom.isEmpty {
                Section("Your Ingredients") {
                    ForEach(custom) { item in
                        Button {
                            request = LibraryRequest(
                                title: "Edit Custom Ingredient", saveTitle: "Save", draft: item.draft,
                                menu: [], customID: item.id)
                        } label: {
                            DirectoryRow(name: item.draft.name, group: item.draft.kind.title, detail: item.draft.summary)
                        }
                        .buttonStyle(.plain)
                        .contextMenu {
                            Button("Edit") {
                                request = LibraryRequest(
                                    title: "Edit Custom Ingredient", saveTitle: "Save", draft: item.draft,
                                    menu: [], customID: item.id)
                            }
                            Button("Duplicate") {
                                store.attempt { try store.customIngredients.duplicate(item.id) }
                            }
                            Button("Delete", role: .destructive) {
                                store.attempt { try store.customIngredients.remove(item.id) }
                            }
                        }
                    }
                }
            }

            ForEach(page.builtInKinds) { kind in
                ForEach(groups(for: kind), id: \.self) { group in
                    Section(group) {
                        ForEach(entries(for: kind).filter { $0.group == group }) { entry in
                            Button {
                                openCopy(of: entry)
                            } label: {
                                DirectoryRow(name: entry.name, group: nil, detail: entry.draft.summary)
                            }
                            .buttonStyle(.plain)
                            .contextMenu {
                                Button("Save a Copy…") { openCopy(of: entry) }
                            }
                        }
                    }
                }
            }
        }
        .overlay {
            if !hasResults {
                ContentUnavailableView.search(text: search)
            }
        }
        .toolbar {
            ToolbarItem {
                Menu {
                    ForEach(IngredientKind.allCases) { kind in
                        Button(kind.title) { newCustom(kind) }
                    }
                } label: {
                    Label("New Custom Ingredient", systemImage: "plus")
                }
                .help("Make your own ingredient")
            }
        }
        .searchable(text: $search, prompt: "Search ingredients")
        .navigationTitle(page.title)
        .sheet(item: $request) { request in
            IngredientEditor(
                title: request.title, saveTitle: request.saveTitle, seed: request.draft, menu: request.menu,
                showsQuantities: false
            ) { draft in
                try store.customIngredients.save(draft, replacing: request.customID)
            }
        }
    }

    private func newCustom(_ kind: IngredientKind) {
        request = LibraryRequest(
            title: "New Custom \(kind.title)", saveTitle: "Save", draft: IngredientDraft(kind: kind),
            menu: IngredientDraft.menuEntries(for: kind, custom: store.customIngredients.ingredients),
            customID: nil)
    }

    private func openCopy(of entry: MenuEntry) {
        request = LibraryRequest(
            title: "Save as Custom Ingredient", saveTitle: "Save", draft: entry.draft, menu: [], customID: nil)
    }

    /// The built-in entries of one kind that match the search.
    private func entries(for kind: IngredientKind) -> [MenuEntry] {
        IngredientDraft.catalogEntries(for: kind).filter { matches($0.name, $0.group) }
    }

    /// The catalog groups of one kind, in catalog order, among the matching entries.
    private func groups(for kind: IngredientKind) -> [String] {
        var seen = Set<String>()
        return entries(for: kind).map(\.group).filter { seen.insert($0).inserted }
    }

    private func matches(_ name: String, _ group: String) -> Bool {
        search.isEmpty || name.localizedCaseInsensitiveContains(search) || group.localizedCaseInsensitiveContains(search)
    }
}

/// An editor to open from the directory. A nil `customID` saves a new custom ingredient.
struct LibraryRequest: Identifiable {
    let id = UUID()
    let title: String
    let saveTitle: String
    let draft: IngredientDraft
    let menu: [MenuEntry]
    let customID: UUID?
}

/// An entry's name, with its group under it when the list is not already grouped,
/// and its key figures on the right.
struct DirectoryRow: View {
    let name: String
    let group: String?
    let detail: String

    var body: some View {
        LabeledContent {
            Text(detail).foregroundStyle(.secondary).multilineTextAlignment(.trailing)
        } label: {
            VStack(alignment: .leading) {
                Text(name)
                if let group {
                    Text(group).font(.caption).foregroundStyle(.secondary)
                }
            }
        }
    }
}
