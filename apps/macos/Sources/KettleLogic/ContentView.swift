import SwiftUI

struct ContentView: View {
    @Bindable var store: DocumentStore
    @AppStorage(Preferences.interfaceChosenKey) private var interfaceChosen = false
    @AppStorage(Preferences.telemetryNoticeShownKey) private var telemetryNoticeShown = false

    var body: some View {
        NavigationSplitView {
            RecipeSidebar(store: store)
        } detail: {
            if store.library == .equipment {
                EquipmentDirectoryView(store: store)
            } else if let library = store.library {
                IngredientDirectoryView(store: store, page: library)
            } else if let document = store.document,
               let recipe = store.recipe(for: store.selection) {
                RecipeDetailView(document: document, recipe: recipe, store: store)
            } else {
                ContentUnavailableView {
                    Label("No Recipe Selected", systemImage: "mug")
                } description: {
                    Text("Open a recipe, or start a new one.")
                } actions: {
                    Button("New Recipe from a Style…") { store.showsWizard = true }
                        .keyboardShortcut(.defaultAction)
                    Button("Blank Recipe") { store.newRecipe() }
                    Button("Open…") { store.open() }
                }
            }
        }
        .sheet(isPresented: $store.showsWizard) {
            NewRecipeWizard(store: store)
        }
        .sheet(isPresented: Binding(get: { !interfaceChosen }, set: { _ in })) {
            InterfaceChoiceSheet()
        }
        // An upgrade from a version that never asked: tell the user once.
        .sheet(isPresented: Binding(get: { interfaceChosen && !telemetryNoticeShown }, set: { _ in })) {
            TelemetryNoticeSheet()
        }
        .alert("Kettle Logic", isPresented: errorIsShown) {
            Button("OK") { store.errorMessage = nil }
        } message: {
            Text(store.errorMessage ?? "")
        }
        .confirmationDialog(
            deletionTitle, isPresented: deletionIsShown, titleVisibility: .visible
        ) {
            Button(deletionIsForGood ? "Delete Permanently" : "Delete Recipe", role: .destructive) {
                if let recipe = store.pendingDeletion { store.deleteRecipe(recipe) }
            }
        } message: {
            Text(deletionIsForGood ? "It is removed for good." : "It moves to Recently Deleted, where you can restore it.")
        }
    }

    private var errorIsShown: Binding<Bool> {
        Binding(
            get: { store.errorMessage != nil },
            set: { if !$0 { store.errorMessage = nil } }
        )
    }

    private var deletionIsShown: Binding<Bool> {
        Binding(
            get: { store.pendingDeletion != nil },
            set: { if !$0 { store.pendingDeletion = nil } }
        )
    }

    /// Whether the recipe waiting for deletion is already in Recently Deleted, so it is removed for good.
    private var deletionIsForGood: Bool {
        store.pendingDeletion.map { store.isDeleted($0) } ?? false
    }

    private var deletionTitle: String {
        let name = store.recipe(for: store.pendingDeletion)?.name ?? "this recipe"
        return "Delete \u{201C}\(name)\u{201D}?"
    }
}
