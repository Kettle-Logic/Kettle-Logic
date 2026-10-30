import AppKit
import Observation
import UniformTypeIdentifiers

/// A recipe or folder in one open document. Ids are only unique within a document, so the
/// document is part of the key.
struct ItemKey: Hashable {
    let document: UUID
    let id: UInt64

    /// The key as text for a drag. A recipe is dropped onto a folder by this text.
    var dragText: String { "\(document.uuidString)/\(id)" }

    init(document: UUID, id: UInt64) {
        self.document = document
        self.id = id
    }

    init?(dragText: String) {
        let parts = dragText.split(separator: "/")
        guard parts.count == 2, let document = UUID(uuidString: String(parts[0])),
              let id = UInt64(parts[1]) else { return nil }
        self.init(document: document, id: id)
    }
}

/// The app's state: the open documents, the selected recipe, and the last error.
/// Views read it and call its actions. Every change reaches the engine through
/// `RecipeDocument`.
@MainActor
@Observable
final class DocumentStore {
    /// The open documents, one per recipe file, in the order the sidebar lists them.
    private(set) var documents: [RecipeDocument] = []
    /// The selected recipe. Selecting a recipe leaves the ingredient directory, and the directory clears the selection.
    var selection: ItemKey? {
        didSet { if selection != nil { library = nil } }
    }
    /// The open page of the ingredient directory, if one is showing.
    var library: LibraryPage?
    /// The user's own ingredients, kept across launches.
    let customIngredients = CustomIngredientStore()
    /// The brewing systems, built-in and the user's own, that a recipe can be brewed on.
    let equipment = EquipmentLibrary()
    var errorMessage: String?
    /// Whether the new-recipe wizard is showing.
    var showsWizard = false
    /// The recipe waiting for the user to confirm its deletion.
    var pendingDeletion: ItemKey?
    private var autosaveTasks: [UUID: Task<Void, Never>] = [:]

    /// Where a document's recipe stands against its file, for the label beside the Save button.
    func saveStatus(of document: RecipeDocument, recipe: RecipeSnapshot) -> String {
        if document.fileURL == nil { return "Not saved yet" }
        if !document.isUnsaved(recipe) { return "Saved" }
        return autosaveTasks[document.id] != nil ? "Saving…" : "Unsaved changes"
    }

    /// The document that holds the selected recipe, or nil when no recipe is selected.
    var document: RecipeDocument? {
        guard let selection else { return nil }
        return document(for: selection)
    }

    init() {
        loadRecipeFolder()
    }

    func showLibrary(_ page: LibraryPage) {
        selection = nil
        library = page
    }

    /// The recipe a key names, if its document is open.
    func recipe(for key: ItemKey?) -> RecipeSnapshot? {
        guard let key, let document = document(for: key) else { return nil }
        return document.snapshot.recipes.first { $0.id == key.id }
    }

    /// Whether a recipe is in Recently Deleted.
    func isDeleted(_ recipe: ItemKey) -> Bool {
        document(for: recipe)?.isDeleted(recipe.id) ?? false
    }

    /// Deletes a recipe, after confirmation when the settings ask for it. A recipe that is in Recently
    /// Deleted is removed for good, and always asks first.
    func requestDelete(_ recipe: ItemKey) {
        if isDeleted(recipe) || UserDefaults.standard.bool(forKey: Preferences.confirmDeleteKey) {
            pendingDeletion = recipe
        } else {
            deleteRecipe(recipe)
        }
    }

    /// Moves a recipe to Recently Deleted, or removes it for good if it is already there. Undo brings it back.
    func deleteRecipe(_ recipe: ItemKey) {
        pendingDeletion = nil
        guard let document = document(for: recipe) else { return }
        attempt {
            if isDeleted(recipe) {
                try document.removeRecipe(recipe.id)
            } else {
                try document.trashRecipe(recipe.id)
            }
            selection = firstRecipe(of: document)
        }
    }

    /// Gives a recipe a new name. Undo restores the old one.
    func renameRecipe(_ recipe: ItemKey, to name: String) {
        guard let document = document(for: recipe) else { return }
        attempt { try document.rename(recipe.id, to: name) }
    }

    /// Copies a recipe in its document, after the original, and selects the copy. Undo removes the copy.
    func duplicateRecipe(_ recipe: ItemKey) {
        guard let document = document(for: recipe) else { return }
        attempt {
            let copy = try document.duplicateRecipe(recipe.id)
            selection = ItemKey(document: document.id, id: copy)
        }
    }

    /// Creates a folder in a document. Undo removes it.
    func createFolder(named name: String, in documentID: UUID) {
        guard let document = documents.first(where: { $0.id == documentID }) else { return }
        attempt { _ = try document.addFolder(named: name) }
    }

    func renameFolder(_ folder: ItemKey, to name: String) {
        guard let document = document(for: folder) else { return }
        attempt { try document.renameFolder(folder.id, to: name) }
    }

    /// Deletes a folder. Its recipes stay in the document, at the top level. Undo brings the folder back.
    func deleteFolder(_ folder: ItemKey) {
        guard let document = document(for: folder) else { return }
        attempt { try document.removeFolder(folder.id) }
    }

    /// Files a recipe in a folder of its document, or at the top level when `folder` is nil.
    func moveRecipe(_ recipe: ItemKey, toFolder folder: UInt64?) {
        guard let document = document(for: recipe) else { return }
        attempt { try document.moveRecipe(recipe.id, toFolder: folder) }
    }

    /// Adds a recipe to the selected document and selects it. With no recipe selected, it
    /// starts a new file, saved straight away in the recipe folder, so unsaved work in an
    /// open document is never replaced.
    func newRecipe() {
        guard let document else {
            attempt {
                let document = try RecipeDocument.makeNew()
                try document.save(to: RecipeLibrary.fileURL(named: document.snapshot.recipes.first?.name ?? "Recipe"))
                documents.append(document)
                selection = firstRecipe(of: document)
            }
            return
        }
        attempt {
            let id = try document.addRecipe()
            selection = ItemKey(document: document.id, id: id)
        }
    }

    /// Adds a recipe built from a style, as one undo step, and selects it. With no recipe selected it starts
    /// a new file, as `newRecipe` does. A failure is thrown, so the wizard can show it and stay open.
    func newRecipe(building request: RecipeBuildRequest) throws {
        if let document {
            let id = try RecipeBuilder.add(request, to: document)
            selection = ItemKey(document: document.id, id: id)
            scheduleAutosave()
            return
        }
        let document = try RecipeDocument.makeNew()
        guard let first = document.snapshot.recipes.first else { return }
        try RecipeBuilder.populate(request, in: document, recipe: first.id)
        try document.save(to: RecipeLibrary.fileURL(named: document.snapshot.recipes.first?.name ?? "Recipe"))
        documents.append(document)
        selection = firstRecipe(of: document)
    }

    /// Opens a recipe file in the sidebar, or selects its recipe if it is already open.
    func open() {
        guard let url = FilePanels.chooseFileToOpen() else { return }
        if let open = documents.first(where: { $0.fileURL?.standardizedFileURL == url.standardizedFileURL }) {
            selection = firstRecipe(of: open)
            return
        }
        attempt {
            let document = try RecipeDocument.open(url)
            documents.append(document)
            selection = firstRecipe(of: document)
        }
    }

    func save() {
        guard let document else { return }
        guard document.fileURL != nil else {
            saveAs()
            return
        }
        attempt { try persist(document) }
    }

    func saveAs() {
        guard let document else { return }
        let suggested = document.snapshot.recipes.first?.name ?? "Recipe"
        guard let url = FilePanels.chooseFileToSave(suggestedName: suggested) else { return }
        attempt { try document.save(to: url) }
    }

    func undo() {
        attempt { try document?.undo() }
    }

    func redo() {
        attempt { try document?.redo() }
    }

    /// Runs `work`, and throws its error to the caller, so a sheet can show it where the user is.
    /// A change that succeeds schedules an autosave when the setting is on.
    func perform(_ work: () throws -> Void) throws {
        try work()
        scheduleAutosave()
    }

    /// Runs `work`, and shows its error, if any, instead of throwing. A change that
    /// succeeds schedules an autosave when the setting is on.
    func attempt(_ work: () throws -> Void) {
        do {
            try work()
            scheduleAutosave()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    /// Opens every recipe file in the recipe folder, and selects the first recipe of the first
    /// document. A file that cannot be opened is skipped, and reported with the others.
    private func loadRecipeFolder() {
        var failures: [String] = []
        for url in RecipeLibrary.recipeFiles() {
            do {
                documents.append(try RecipeDocument.open(url))
            } catch {
                failures.append("\(url.lastPathComponent): \(error.localizedDescription)")
            }
        }
        if let first = documents.first {
            selection = firstRecipe(of: first)
        }
        if !failures.isEmpty {
            errorMessage = "Some recipes in the recipe folder could not be opened.\n\n"
                + failures.joined(separator: "\n")
        }
    }

    private func document(for key: ItemKey) -> RecipeDocument? {
        documents.first { $0.id == key.document }
    }

    /// The first recipe of a document that is not in Recently Deleted, as a selection, or nil when it has none.
    private func firstRecipe(of document: RecipeDocument) -> ItemKey? {
        document.snapshot.recipes.first { !document.isDeleted($0.id) }
            .map { ItemKey(document: document.id, id: $0.id) }
    }

    /// Saves the selected document over its file a moment after the last change. A document
    /// with no file yet is left alone, so the save panel still appears on Save.
    private func scheduleAutosave() {
        guard UserDefaults.standard.bool(forKey: Preferences.autosaveKey),
              let document, document.fileURL != nil else { return }
        autosaveTasks[document.id]?.cancel()
        autosaveTasks[document.id] = Task {
            try? await Task.sleep(for: .seconds(2))
            guard !Task.isCancelled else { return }
            // Saved directly, not through `attempt`, which would schedule another autosave.
            do {
                try persist(document)
            } catch {
                errorMessage = error.localizedDescription
            }
            autosaveTasks[document.id] = nil
        }
    }

    /// Writes the document to its file. A document in the recipe folder is renamed to match
    /// its first recipe, and its old files are removed, so a renamed recipe keeps one file.
    private func persist(_ document: RecipeDocument) throws {
        guard let current = document.fileURL else { return }
        guard RecipeLibrary.contains(current),
              let name = document.snapshot.recipes.first?.name,
              RecipeLibrary.safeFileName(name) != current.deletingPathExtension().lastPathComponent else {
            try document.save(to: current)
            return
        }
        try document.save(to: RecipeLibrary.fileURL(named: name, keeping: current))
        RecipeLibrary.removeFiles(of: current)
    }
}

/// The standard open and save panels, for a BeerJSON file. They start in the recipe folder.
@MainActor
enum FilePanels {
    static func chooseFileToOpen() -> URL? {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.json]
        panel.allowsMultipleSelection = false
        panel.directoryURL = RecipeLibrary.folder
        panel.message = "Open a BeerJSON recipe. Its sidecar file is opened with it."
        return panel.runModal() == .OK ? panel.url : nil
    }

    static func chooseFileToSave(suggestedName: String) -> URL? {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.directoryURL = try? RecipeLibrary.ensureFolder()
        panel.nameFieldStringValue = "\(suggestedName).json"
        return panel.runModal() == .OK ? panel.url : nil
    }

    /// A folder chosen in the settings, or nil if the panel is canceled.
    static func chooseFolder() -> URL? {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = RecipeLibrary.folder
        panel.message = "Choose the folder where new recipes are saved."
        panel.prompt = "Choose"
        return panel.runModal() == .OK ? panel.url : nil
    }
}
