#pragma once

#include "kettlelogic/app/engine.hpp"
#include "kettlelogic/app/equipment.hpp"
#include "kettlelogic/app/ingredient_draft.hpp"
#include "kettlelogic/app/preferences.hpp"
#include "kettlelogic/app/recipe_builder.hpp"
#include "kettlelogic/app/recipe_library.hpp"
#include "kettlelogic/app/water_profiles.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kettlelogic::app {

// A recipe or folder in one open document. Ids are only unique within a document, so the document is part of the key.
struct ItemKey {
    std::string document;
    EntityId id = 0;

    // The key as text for a drag. A recipe is dropped onto a folder by this text.
    std::string dragText() const { return document + "/" + std::to_string(id); }
    static std::optional<ItemKey> fromDragText(const std::string& text);

    friend bool operator==(const ItemKey&, const ItemKey&) = default;
    friend bool operator<(const ItemKey& a, const ItemKey& b) {
        return a.document != b.document ? a.document < b.document : a.id < b.id;
    }
};

// One page of the ingredient directory: the user's own ingredients, or the built-in ingredients of one kind.
struct LibraryPage {
    enum class Kind { yours, ingredient, equipment };
    Kind kind = Kind::yours;
    IngredientKind ingredient = IngredientKind::fermentable;  // for Kind::ingredient

    static const std::vector<LibraryPage>& all();
    std::string title() const;
    // The built-in kinds this page lists.
    std::vector<IngredientKind> builtInKinds() const;
    // Whether a custom ingredient of this kind belongs on this page.
    bool includes(IngredientKind kind) const;

    friend bool operator==(const LibraryPage&, const LibraryPage&) = default;
};

// Where the app keeps its files.
struct AppPaths {
    std::filesystem::path home;  // the user's home folder, which holds KettleLogic\Recipes
    std::filesystem::path data;  // the app's data folder, which holds the settings and the libraries
};

// The app's state: the open documents, the selected recipe, and the last error. Views read it and call its
// actions, and are told when it changes. Every change reaches the engine through RecipeDocument.
class DocumentStore {
public:
    using Clock = std::chrono::steady_clock;

    explicit DocumentStore(const AppPaths& paths);

    // Called after any change to what a view shows.
    std::function<void()> changed;

    Preferences preferences;
    RecipeLibrary recipeLibrary;
    CustomIngredientStore customIngredients;
    EquipmentLibrary equipment;
    WaterProfileLibrary waterProfiles;

    // The open documents, one per recipe file, in the order the sidebar lists them.
    const std::vector<std::shared_ptr<RecipeDocument>>& documents() const { return documents_; }
    // The selected recipe. Selecting a recipe leaves the ingredient directory, and the directory clears the selection.
    const std::optional<ItemKey>& selection() const { return selection_; }
    void select(const std::optional<ItemKey>& key);
    // The open page of the ingredient directory, if one is showing.
    const std::optional<LibraryPage>& library() const { return library_; }
    void showLibrary(const LibraryPage& page);

    std::optional<std::string> errorMessage;
    // The recipe waiting for the user to confirm its deletion.
    std::optional<ItemKey> pendingDeletion;

    // Where a document's recipe stands against its file, for the label beside the Save button.
    std::string saveStatus(const RecipeDocument& document, EntityId recipe) const;

    // The document that holds the selected recipe, or null when no recipe is selected.
    std::shared_ptr<RecipeDocument> document() const;
    std::shared_ptr<RecipeDocument> document(const ItemKey& key) const;
    // The recipe a key names, if its document is open.
    const model::Recipe* recipe(const std::optional<ItemKey>& key) const;

    // Deletes a recipe, after confirmation when the settings ask for it. A recipe that is in Recently Deleted is
    // removed for good, and always asks first.
    void requestDelete(const ItemKey& recipe);
    // Moves a recipe to Recently Deleted, or removes it for good if it is already there. Undo brings it back.
    void deleteRecipe(const ItemKey& recipe);
    void renameRecipe(const ItemKey& recipe, const std::string& name);
    // Copies a recipe in its document, after the original, and selects the copy.
    void duplicateRecipe(const ItemKey& recipe);

    void createFolder(const std::string& name, const std::string& documentId);
    void renameFolder(const ItemKey& folder, const std::string& name);
    // Deletes a folder. Its recipes stay in the document, at the top level. Undo brings the folder back.
    void deleteFolder(const ItemKey& folder);
    // Files a recipe in a folder of its document, or at the top level when `folder` is empty.
    void moveRecipe(const ItemKey& recipe, std::optional<EntityId> folder);

    // Adds a recipe to the selected document and selects it. With no recipe selected, it starts a new file, saved
    // straight away in the recipe folder, so unsaved work in an open document is never replaced.
    void newRecipe();
    // Adds a recipe built from a style, as one undo step, and selects it. A failure is thrown, so the wizard can
    // show it and stay open.
    void newRecipe(const RecipeBuildRequest& request);

    // Opens a recipe file in the sidebar, or selects its recipe if it is already open.
    void open(const std::filesystem::path& file);

    enum class SaveResult { saved, needsFile, nothingToSave };
    // Saves the selected document over its file. A document with no file needs one: see saveAs.
    SaveResult save();
    void saveAs(const std::filesystem::path& file);
    // The name to offer in a Save As dialog.
    std::string suggestedFileName() const;

    void undo();
    void redo();

    // Runs `work`, and shows its error, if any, instead of throwing. A change that succeeds schedules an autosave
    // when the setting is on.
    void attempt(const std::function<void()>& work);

    // Saves the documents whose autosave is due. The window calls it on a timer.
    void tick(Clock::time_point now = Clock::now());
    // How long after the last change a document is autosaved.
    static constexpr std::chrono::seconds autosaveDelay{2};

    // Opens every recipe file in the recipe folder, and selects the first recipe of the first document. A file that
    // cannot be opened is skipped, and reported with the others.
    void loadRecipeFolder();

private:
    void notify();
    ItemKey firstRecipeKey(const RecipeDocument& document) const;
    std::optional<ItemKey> firstRecipe(const RecipeDocument& document) const;
    void scheduleAutosave();
    void persist(RecipeDocument& document);
    UnitSystem units() const { return preferences.units(); }

    std::vector<std::shared_ptr<RecipeDocument>> documents_;
    std::optional<ItemKey> selection_;
    std::optional<LibraryPage> library_;
    std::map<std::string, Clock::time_point> autosaveDue_;
};

}  // namespace kettlelogic::app
