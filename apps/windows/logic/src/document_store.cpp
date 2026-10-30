#include "kettlelogic/app/document_store.hpp"

#include "kettlelogic/app/text.hpp"

#include <algorithm>

namespace kettlelogic::app {

namespace fs = std::filesystem;

std::optional<ItemKey> ItemKey::fromDragText(const std::string& text) {
    const std::size_t slash = text.rfind('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= text.size()) {
        return std::nullopt;
    }
    const std::string digits = text.substr(slash + 1);
    if (digits.find_first_not_of("0123456789") != std::string::npos || digits.size() > 18) {
        return std::nullopt;
    }
    return ItemKey{text.substr(0, slash), static_cast<EntityId>(std::stoull(digits))};
}

// MARK: LibraryPage

const std::vector<LibraryPage>& LibraryPage::all() {
    static const std::vector<LibraryPage> pages = [] {
        std::vector<LibraryPage> out = {LibraryPage{Kind::yours, IngredientKind::fermentable}};
        for (const IngredientKind kind : allIngredientKinds()) {
            out.push_back(LibraryPage{Kind::ingredient, kind});
        }
        out.push_back(LibraryPage{Kind::equipment, IngredientKind::fermentable});
        return out;
    }();
    return pages;
}

std::string LibraryPage::title() const {
    switch (kind) {
        case Kind::yours: return "Your Ingredients";
        case Kind::equipment: return "Equipment";
        case Kind::ingredient:
            switch (ingredient) {
                case IngredientKind::fermentable: return "Fermentables";
                case IngredientKind::hop: return "Hops";
                case IngredientKind::culture: return "Cultures";
                case IngredientKind::water: return "Water";
                case IngredientKind::miscellaneous: return "Miscellaneous";
            }
    }
    return "";
}

std::vector<IngredientKind> LibraryPage::builtInKinds() const {
    return kind == Kind::ingredient ? std::vector<IngredientKind>{ingredient} : std::vector<IngredientKind>{};
}

bool LibraryPage::includes(IngredientKind other) const {
    switch (kind) {
        case Kind::yours: return true;
        case Kind::ingredient: return ingredient == other;
        case Kind::equipment: return false;
    }
    return false;
}

// MARK: DocumentStore

DocumentStore::DocumentStore(const AppPaths& paths)
    : preferences(paths.data / "settings.json"),
      recipeLibrary(preferences, paths.home),
      customIngredients(paths.data / "custom-ingredients.json"),
      equipment(paths.data / "custom-equipment.json"),
      waterProfiles(paths.data / "water-profiles.json") {}

void DocumentStore::notify() {
    if (changed) {
        changed();
    }
}

void DocumentStore::select(const std::optional<ItemKey>& key) {
    selection_ = key;
    if (selection_) {
        library_.reset();
    }
    notify();
}

void DocumentStore::showLibrary(const LibraryPage& page) {
    selection_.reset();
    library_ = page;
    notify();
}

std::string DocumentStore::saveStatus(const RecipeDocument& document, EntityId recipe) const {
    if (!document.file()) return "Not saved yet";
    if (!document.isUnsaved(recipe)) return "Saved";
    return autosaveDue_.count(document.id()) != 0 ? "Saving\xE2\x80\xA6" : "Unsaved changes";
}

std::shared_ptr<RecipeDocument> DocumentStore::document() const {
    return selection_ ? document(*selection_) : nullptr;
}

std::shared_ptr<RecipeDocument> DocumentStore::document(const ItemKey& key) const {
    for (const auto& doc : documents_) {
        if (doc->id() == key.document) {
            return doc;
        }
    }
    return nullptr;
}

const model::Recipe* DocumentStore::recipe(const std::optional<ItemKey>& key) const {
    if (!key) {
        return nullptr;
    }
    const auto doc = document(*key);
    return doc ? doc->recipe(key->id) : nullptr;
}

void DocumentStore::requestDelete(const ItemKey& recipeKey) {
    // A recipe already in Recently Deleted is removed for good, so that always asks first.
    const auto doc = document(recipeKey);
    const bool forGood = doc && doc->isDeleted(recipeKey.id);
    if (forGood || preferences.boolean(PrefKey::confirmDelete)) {
        pendingDeletion = recipeKey;
        notify();
    } else {
        deleteRecipe(recipeKey);
    }
}

void DocumentStore::deleteRecipe(const ItemKey& recipeKey) {
    pendingDeletion.reset();
    const auto doc = document(recipeKey);
    if (!doc) {
        notify();
        return;
    }
    attempt([&] {
        // A recipe goes to Recently Deleted first, and is removed for good only from there.
        if (doc->isDeleted(recipeKey.id)) {
            doc->removeRecipe(recipeKey.id);
        } else {
            doc->trashRecipe(recipeKey.id);
        }
        selection_ = firstRecipe(*doc);
        if (selection_) library_.reset();
    });
}

void DocumentStore::renameRecipe(const ItemKey& recipeKey, const std::string& name) {
    const auto doc = document(recipeKey);
    if (!doc) {
        return;
    }
    attempt([&] { doc->rename(recipeKey.id, name); });
}

void DocumentStore::duplicateRecipe(const ItemKey& recipeKey) {
    const auto doc = document(recipeKey);
    if (!doc) {
        return;
    }
    attempt([&] {
        const EntityId copy = doc->duplicateRecipe(recipeKey.id);
        selection_ = ItemKey{doc->id(), copy};
        library_.reset();
    });
}

void DocumentStore::createFolder(const std::string& name, const std::string& documentId) {
    const auto found = std::find_if(documents_.begin(), documents_.end(),
                                    [&](const auto& d) { return d->id() == documentId; });
    if (found == documents_.end()) {
        return;
    }
    attempt([&] { (*found)->addFolder(name); });
}

void DocumentStore::renameFolder(const ItemKey& folder, const std::string& name) {
    if (const auto doc = document(folder)) {
        attempt([&] { doc->renameFolder(folder.id, name); });
    }
}

void DocumentStore::deleteFolder(const ItemKey& folder) {
    if (const auto doc = document(folder)) {
        attempt([&] { doc->removeFolder(folder.id); });
    }
}

void DocumentStore::moveRecipe(const ItemKey& recipeKey, std::optional<EntityId> folder) {
    if (const auto doc = document(recipeKey)) {
        attempt([&] { doc->moveRecipe(recipeKey.id, folder); });
    }
}

std::optional<ItemKey> DocumentStore::firstRecipe(const RecipeDocument& doc) const {
    // A recipe in Recently Deleted is not offered as the first one to show.
    for (const model::Recipe& recipe : doc.recipes()) {
        if (!doc.isDeleted(recipe.id)) {
            return ItemKey{doc.id(), recipe.id};
        }
    }
    return std::nullopt;
}

void DocumentStore::newRecipe() {
    const auto current = document();
    if (!current) {
        attempt([&] {
            auto doc = RecipeDocument::makeNew();
            const std::string name = doc->recipes().empty() ? "Recipe" : doc->recipes().front().name;
            doc->save(recipeLibrary.fileFor(name));
            documents_.push_back(doc);
            selection_ = firstRecipe(*doc);
            library_.reset();
        });
        return;
    }
    attempt([&] {
        const EntityId id = current->addRecipe();
        selection_ = ItemKey{current->id(), id};
        library_.reset();
    });
}

void DocumentStore::newRecipe(const RecipeBuildRequest& request) {
    if (const auto current = document()) {
        const EntityId id = RecipeBuilder::add(request, *current);
        selection_ = ItemKey{current->id(), id};
        library_.reset();
        scheduleAutosave();
        notify();
        return;
    }
    auto doc = RecipeDocument::makeNew();
    if (doc->recipes().empty()) {
        return;
    }
    RecipeBuilder::populate(request, *doc, doc->recipes().front().id);
    doc->save(recipeLibrary.fileFor(doc->recipes().front().name));
    documents_.push_back(doc);
    selection_ = firstRecipe(*doc);
    library_.reset();
    notify();
}

void DocumentStore::open(const fs::path& file) {
    std::error_code error;
    const fs::path wanted = fs::weakly_canonical(file, error);
    for (const auto& doc : documents_) {
        if (doc->file() && fs::weakly_canonical(*doc->file(), error) == wanted) {
            select(firstRecipe(*doc));
            return;
        }
    }
    attempt([&] {
        auto doc = RecipeDocument::open(file);
        documents_.push_back(doc);
        selection_ = firstRecipe(*doc);
        library_.reset();
    });
}

DocumentStore::SaveResult DocumentStore::save() {
    const auto doc = document();
    if (!doc) {
        return SaveResult::nothingToSave;
    }
    if (!doc->file()) {
        return SaveResult::needsFile;
    }
    attempt([&] { persist(*doc); });
    return SaveResult::saved;
}

void DocumentStore::saveAs(const fs::path& file) {
    if (const auto doc = document()) {
        attempt([&] { doc->save(file); });
    }
}

std::string DocumentStore::suggestedFileName() const {
    const auto doc = document();
    return doc && !doc->recipes().empty() ? doc->recipes().front().name : "Recipe";
}

void DocumentStore::undo() {
    if (const auto doc = document()) {
        attempt([&] { doc->undo(); });
    }
}

void DocumentStore::redo() {
    if (const auto doc = document()) {
        attempt([&] { doc->redo(); });
    }
}

void DocumentStore::attempt(const std::function<void()>& work) {
    try {
        work();
        scheduleAutosave();
    } catch (const std::exception& e) {
        errorMessage = e.what();
    }
    notify();
}

void DocumentStore::loadRecipeFolder() {
    std::vector<std::string> failures;
    for (const fs::path& file : recipeLibrary.recipeFiles()) {
        try {
            documents_.push_back(RecipeDocument::open(file));
        } catch (const std::exception& e) {
            failures.push_back(file.filename().string() + ": " + e.what());
        }
    }
    if (!documents_.empty()) {
        selection_ = firstRecipe(*documents_.front());
    }
    if (!failures.empty()) {
        std::string message = "Some recipes in the recipe folder could not be opened.\n";
        for (const std::string& failure : failures) {
            message += "\n" + failure;
        }
        errorMessage = message;
    }
    notify();
}

void DocumentStore::scheduleAutosave() {
    const auto doc = document();
    if (!preferences.boolean(PrefKey::autosave) || !doc || !doc->file()) {
        return;
    }
    autosaveDue_[doc->id()] = Clock::now() + autosaveDelay;
}

void DocumentStore::tick(Clock::time_point now) {
    std::vector<std::string> due;
    for (const auto& [id, when] : autosaveDue_) {
        if (when <= now) {
            due.push_back(id);
        }
    }
    if (due.empty()) {
        return;
    }
    for (const std::string& id : due) {
        autosaveDue_.erase(id);
        const auto doc = std::find_if(documents_.begin(), documents_.end(), [&](const auto& d) { return d->id() == id; });
        if (doc == documents_.end()) {
            continue;
        }
        // Saved directly, not through `attempt`, which would schedule another autosave.
        try {
            persist(**doc);
        } catch (const std::exception& e) {
            errorMessage = e.what();
        }
    }
    notify();
}

void DocumentStore::persist(RecipeDocument& doc) {
    if (!doc.file()) {
        return;
    }
    const fs::path current = *doc.file();
    if (!recipeLibrary.contains(current) || doc.recipes().empty() ||
        RecipeLibrary::safeFileName(doc.recipes().front().name) == current.stem().string()) {
        doc.save(current);
        return;
    }
    const fs::path renamed = recipeLibrary.fileFor(doc.recipes().front().name, current);
    doc.save(renamed);
    // The name can land on the file it already had, such as "Pale Ale 2". That file is the one just saved.
    if (renamed != current) {
        RecipeLibrary::removeFiles(current);
    }
}

}  // namespace kettlelogic::app
