import SwiftUI

/// The recipe list. Each open document is a section, with its folders and recipes, above the
/// ingredient directory. A recipe that has not been saved is italic, with an asterisk after its name.
struct RecipeSidebar: View {
    @Bindable var store: DocumentStore
    /// The folders the user has closed. A folder is open unless it is listed here.
    @State private var closedFolders: Set<ItemKey> = []
    /// The folder name sheet, when one is open.
    @State private var naming: FolderNaming?

    var body: some View {
        List(selection: $store.selection) {
            ForEach(store.documents, id: \.id) { document in
                Section {
                    ForEach(document.snapshot.folders.filter { !$0.trash }) { folder in
                        folderRow(folder, in: document)
                    }

                    ForEach(recipes(in: nil, of: document)) { recipe in
                        recipeRow(recipe, in: document)
                    }

                    // Recently Deleted is listed last.
                    ForEach(document.snapshot.folders.filter { $0.trash }) { folder in
                        folderRow(folder, in: document)
                    }
                } header: {
                    // Dropping a recipe on the document's name moves it to the top level of that document.
                    Text(document.displayName)
                        .dropDestination(for: String.self) { ids, _ in
                            moveAll(ids, toFolder: nil, in: document)
                        }
                }
            }

            Section("Ingredient Directory") {
                ForEach(LibraryPage.all) { page in
                    // The open page is highlighted the way a selected recipe row is, since the list's selection
                    // holds only recipes.
                    let selected = store.library == page
                    Button { store.showLibrary(page) } label: {
                        Label(page.title, systemImage: page.symbol)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .foregroundStyle(selected ? Color.white : Color.primary)
                    .listRowBackground(selected ? Color.accentColor : nil)
                }
            }
        }
        .navigationTitle("Recipes")
        .navigationSplitViewColumnWidth(min: 200, ideal: 240)
        .toolbar {
            ToolbarItem {
                Button("New Recipe", systemImage: "plus") { store.newRecipe() }
                    .help("Add a recipe to the selected document, or start a new one")
            }
            ToolbarItem {
                Button("New Recipe from a Style", systemImage: "wand.and.stars") { store.showsWizard = true }
                    .help("Start a recipe from a beer style, with the amounts worked out for you")
            }
            ToolbarItem {
                Button("New Folder", systemImage: "folder.badge.plus") {
                    if let document = store.document { naming = .new(document: document.id) }
                }
                .help("Add a folder to group recipes")
                .disabled(store.document == nil)
            }
        }
        .sheet(item: $naming) { naming in
            FolderNameSheet(naming: naming, store: store)
        }
    }

    /// A folder and the recipes filed in it. Recently Deleted has no menu, because the app keeps it.
    private func folderRow(_ folder: RecipeFolder, in document: RecipeDocument) -> some View {
        let key = ItemKey(document: document.id, id: folder.id)
        return DisclosureGroup(isExpanded: isOpen(key)) {
            ForEach(recipes(in: folder.id, of: document)) { recipe in
                recipeRow(recipe, in: document)
            }
        } label: {
            Label(folder.name, systemImage: folder.trash ? "trash" : "folder")
                .dropDestination(for: String.self) { ids, _ in
                    moveAll(ids, toFolder: folder.id, in: document)
                }
                .contextMenu {
                    if !folder.trash {
                        Button("Rename Folder…") { naming = .rename(folder: key, name: folder.name) }
                        Button("Delete Folder", role: .destructive) { store.deleteFolder(key) }
                    }
                }
        }
    }

    private func recipeRow(_ recipe: RecipeSnapshot, in document: RecipeDocument) -> some View {
        let key = ItemKey(document: document.id, id: recipe.id)
        let unsaved = document.isUnsaved(recipe)
        let deleted = store.isDeleted(key)
        return Label {
            Text(unsaved ? "\(recipe.name)*" : recipe.name)
                .italic(unsaved)
        } icon: {
            Image(systemName: recipe.isFermentedDrink ? "wineglass" : "mug")
        }
        .tag(key)
        .draggable(key.dragText)
        .contextMenu {
            Button("Rename Recipe…") { naming = .renameRecipe(recipe: key, name: recipe.name) }
            Menu("Move to Folder") {
                Button("Top Level") { store.moveRecipe(key, toFolder: nil) }
                    .disabled(recipe.folderId == nil)
                ForEach(document.snapshot.folders.filter { !$0.trash }) { folder in
                    Button(folder.name) { store.moveRecipe(key, toFolder: folder.id) }
                        .disabled(recipe.folderId == folder.id)
                }
            }
            Divider()
            if deleted {
                Button("Restore") { store.moveRecipe(key, toFolder: nil) }
                Button("Delete Permanently", role: .destructive) { store.requestDelete(key) }
            } else {
                Button("Duplicate Recipe") { store.duplicateRecipe(key) }
                // Deleting moves a recipe to Recently Deleted. Deleting it there removes it for good.
                Button("Delete Recipe", role: .destructive) { store.requestDelete(key) }
            }
        }
    }

    /// The recipes filed in `folder`, or at the top level when `folder` is nil, in document order.
    private func recipes(in folder: UInt64?, of document: RecipeDocument) -> [RecipeSnapshot] {
        document.snapshot.recipes.filter { $0.folderId == folder }
    }

    private func isOpen(_ folder: ItemKey) -> Binding<Bool> {
        Binding(
            get: { !closedFolders.contains(folder) },
            set: { open in
                if open {
                    closedFolders.remove(folder)
                } else {
                    closedFolders.insert(folder)
                }
            })
    }

    /// Moves each dragged recipe that belongs to `document`. Recipes stay in their own document.
    private func moveAll(_ texts: [String], toFolder folder: UInt64?, in document: RecipeDocument) {
        for key in texts.compactMap(ItemKey.init(dragText:)) where key.document == document.id {
            store.moveRecipe(key, toFolder: folder)
        }
    }
}

/// Which name the sheet asks for: a new folder in a document, the rename of a folder, or the rename of a recipe.
enum FolderNaming: Identifiable {
    case new(document: UUID)
    case rename(folder: ItemKey, name: String)
    case renameRecipe(recipe: ItemKey, name: String)

    var id: String {
        switch self {
        case .new(let document): "new-\(document)"
        case .rename(let folder, _): "rename-\(folder.document)-\(folder.id)"
        case .renameRecipe(let recipe, _): "rename-recipe-\(recipe.document)-\(recipe.id)"
        }
    }

    var title: String {
        switch self {
        case .new: "New Folder"
        case .rename: "Rename Folder"
        case .renameRecipe: "Rename Recipe"
        }
    }

    var fieldTitle: String {
        switch self {
        case .new, .rename: "Folder name"
        case .renameRecipe: "Recipe name"
        }
    }

    var actionTitle: String {
        switch self {
        case .new: "Create"
        case .rename, .renameRecipe: "Rename"
        }
    }

    var currentName: String {
        switch self {
        case .new: ""
        case .rename(_, let name), .renameRecipe(_, let name): name
        }
    }
}

/// Asks for a folder's name, for a new folder or a rename, or for a recipe's new name.
struct FolderNameSheet: View {
    let naming: FolderNaming
    let store: DocumentStore

    @Environment(\.dismiss) private var dismiss
    @State private var name: String

    init(naming: FolderNaming, store: DocumentStore) {
        self.naming = naming
        self.store = store
        _name = State(initialValue: naming.currentName)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(naming.title).font(.headline)
            TextField(naming.fieldTitle, text: $name)
                .textFieldStyle(.roundedBorder)
                .onSubmit(save)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button(naming.actionTitle, action: save)
                    .keyboardShortcut(.defaultAction)
                    .disabled(trimmed(name).isEmpty)
            }
        }
        .padding(20)
        .frame(width: 340)
    }

    private func save() {
        let text = trimmed(name)
        guard !text.isEmpty else { return }
        switch naming {
        case .new(let document): store.createFolder(named: text, in: document)
        case .rename(let folder, _): store.renameFolder(folder, to: text)
        case .renameRecipe(let recipe, _): store.renameRecipe(recipe, to: text)
        }
        dismiss()
    }
}
