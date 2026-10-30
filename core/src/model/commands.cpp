#include "kettlelogic/model/commands.hpp"

#include "kettlelogic/model/schema.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kettlelogic::model {

namespace {

Recipe& requireRecipe(Document& document, EntityId id) {
    Recipe* recipe = document.findRecipe(id);
    if (recipe == nullptr) {
        throw std::invalid_argument("no recipe with id " + std::to_string(id));
    }
    return *recipe;
}

Folder& requireFolder(Document& document, EntityId id) {
    Folder* folder = document.findFolder(id);
    if (folder == nullptr) {
        throw std::invalid_argument("no folder with id " + std::to_string(id));
    }
    return *folder;
}

void validateFolderName(const std::string& name) {
    if (name.find_first_not_of(" \t") == std::string::npos) {
        throw std::invalid_argument("a folder needs a name");
    }
}

constexpr const char* kTrashFolderName = "Recently Deleted";

void validateFermentable(const Fermentable& fermentable) {
    if (fermentable.yield.empty()) {
        // The schema requires a yield, so the edit is refused rather than
        // recorded in a state the writer could not save.
        throw std::invalid_argument("a fermentable needs a yield");
    }
    const auto& types = schema::kFermentableTypes;
    if (std::find(types.begin(), types.end(), std::string_view(fermentable.type)) == types.end()) {
        throw std::invalid_argument("\"" + fermentable.type + "\" is not a fermentable type");
    }
    const Dimension dimension = fermentable.amount.dimension();
    if (dimension != Dimension::Mass && dimension != Dimension::Volume) {
        throw std::invalid_argument("a fermentable is measured by mass or volume");
    }
}

void validateMiscellaneous(const Miscellaneous& miscellaneous) {
    const auto& kinds = schema::kMiscellaneousTypes;
    if (std::find(kinds.begin(), kinds.end(), std::string_view(miscellaneous.type)) == kinds.end()) {
        throw std::invalid_argument("\"" + miscellaneous.type + "\" is not a miscellaneous type");
    }
    const Dimension dimension = miscellaneous.amount.dimension();
    if (dimension != Dimension::Mass && dimension != Dimension::Volume && dimension != Dimension::Count) {
        throw std::invalid_argument("a miscellaneous ingredient is measured by mass, volume, or count");
    }
}

class AddRecipe : public Edit {
public:
    explicit AddRecipe(Recipe recipe) : recipe_(std::move(recipe)) {}

    void apply(Document& document) override {
        if (recipe_.id == 0) {
            recipe_.id = document.allocateId();
        }
        if (recipe_.uuid.isNil()) {
            recipe_.uuid = Uuid::generate();
        }
        document.recipes.push_back(recipe_);
    }

    void undo(Document& document) override {
        auto& recipes = document.recipes;
        recipes.erase(std::remove_if(recipes.begin(), recipes.end(),
                                     [this](const Recipe& r) { return r.id == recipe_.id; }),
                      recipes.end());
    }

    std::string label() const override { return "Add Recipe"; }

private:
    Recipe recipe_;
};

class SetRecipeName : public Edit {
public:
    SetRecipeName(EntityId recipe, std::string name) : recipe_(recipe), after_(std::move(name)) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.name;
        recipe.name = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).name = before_; }

    std::string label() const override { return "Rename Recipe"; }

    bool mergeWith(const Edit& next) override {
        const auto* other = dynamic_cast<const SetRecipeName*>(&next);
        if (other == nullptr || other->recipe_ != recipe_) {
            return false;
        }
        after_ = other->after_;
        return true;
    }

private:
    EntityId recipe_;
    std::string after_;
    std::string before_;
};

class SetBatchSize : public Edit {
public:
    SetBatchSize(EntityId recipe, Quantity size) : recipe_(recipe), after_(std::move(size)) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.batchSize;
        recipe.batchSize = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).batchSize = before_; }

    std::string label() const override { return "Set Batch Size"; }

private:
    EntityId recipe_;
    Quantity after_;
    Quantity before_;
};

class AddFermentable : public Edit {
public:
    AddFermentable(EntityId recipe, Fermentable fermentable)
        : recipe_(recipe), fermentable_(std::move(fermentable)) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        validateFermentable(fermentable_);
        if (fermentable_.id == 0) {
            fermentable_.id = document.allocateId();
        }
        if (fermentable_.uuid.isNil()) {
            fermentable_.uuid = Uuid::generate();
        }
        recipe.fermentables.push_back(fermentable_);
    }

    void undo(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        const auto it = std::find_if(recipe.fermentables.begin(), recipe.fermentables.end(),
                                     [this](const Fermentable& f) { return f.id == fermentable_.id; });
        if (it != recipe.fermentables.end()) {
            recipe.fermentables.erase(it);
        }
    }

    std::string label() const override { return "Add Fermentable"; }

private:
    EntityId recipe_;
    Fermentable fermentable_;
};

class RemoveFermentable : public Edit {
public:
    RemoveFermentable(EntityId recipe, EntityId fermentable) : recipe_(recipe), target_(fermentable) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        const auto it = std::find_if(recipe.fermentables.begin(), recipe.fermentables.end(),
                                     [this](const Fermentable& f) { return f.id == target_; });
        if (it == recipe.fermentables.end()) {
            throw std::invalid_argument("no fermentable with id " + std::to_string(target_));
        }
        index_ = static_cast<std::size_t>(it - recipe.fermentables.begin());
        removed_ = *it;
        recipe.fermentables.erase(it);
    }

    void undo(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        const std::size_t at = std::min(index_, recipe.fermentables.size());
        recipe.fermentables.insert(recipe.fermentables.begin() + static_cast<std::ptrdiff_t>(at), removed_);
    }

    std::string label() const override { return "Remove Fermentable"; }

private:
    EntityId recipe_;
    EntityId target_;
    std::size_t index_ = 0;
    Fermentable removed_;
};

bool isPresent(const Recipe& recipe, IngredientList list) {
    switch (list) {
        case IngredientList::Hops: return recipe.hops.has_value();
        case IngredientList::Miscellaneous: return recipe.miscellaneous.has_value();
        case IngredientList::Cultures: return recipe.cultures.has_value();
        case IngredientList::Waters: return recipe.waters.has_value();
    }
    return false;
}

std::size_t itemCount(const Recipe& recipe, IngredientList list) {
    switch (list) {
        case IngredientList::Hops: return recipe.hops ? recipe.hops->size() : 0;
        case IngredientList::Miscellaneous: return recipe.miscellaneous ? recipe.miscellaneous->size() : 0;
        case IngredientList::Cultures: return recipe.cultures ? recipe.cultures->size() : 0;
        case IngredientList::Waters: return recipe.waters ? recipe.waters->size() : 0;
    }
    return 0;
}

void setPresent(Recipe& recipe, IngredientList list, bool present) {
    switch (list) {
        case IngredientList::Hops:
            if (present) { if (!recipe.hops) recipe.hops.emplace(); } else { recipe.hops.reset(); }
            break;
        case IngredientList::Miscellaneous:
            if (present) { if (!recipe.miscellaneous) recipe.miscellaneous.emplace(); } else { recipe.miscellaneous.reset(); }
            break;
        case IngredientList::Cultures:
            if (present) { if (!recipe.cultures) recipe.cultures.emplace(); } else { recipe.cultures.reset(); }
            break;
        case IngredientList::Waters:
            if (present) { if (!recipe.waters) recipe.waters.emplace(); } else { recipe.waters.reset(); }
            break;
    }
}

class SetIngredientListPresent : public Edit {
public:
    SetIngredientListPresent(EntityId recipe, IngredientList list, bool present)
        : recipe_(recipe), list_(list), present_(present) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        if (!present_ && itemCount(recipe, list_) > 0) {
            throw std::invalid_argument("the ingredient list still has items");
        }
        wasPresent_ = isPresent(recipe, list_);
        setPresent(recipe, list_, present_);
    }

    void undo(Document& document) override { setPresent(requireRecipe(document, recipe_), list_, wasPresent_); }

    std::string label() const override {
        return present_ ? "Add Ingredient List" : "Remove Ingredient List";
    }

private:
    EntityId recipe_;
    IngredientList list_;
    bool present_;
    bool wasPresent_ = false;
};

class SetBoilStepsPresent : public Edit {
public:
    SetBoilStepsPresent(EntityId recipe, bool present) : recipe_(recipe), present_(present) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        if (!recipe.boil) {
            throw std::invalid_argument("the recipe has no boil procedure");
        }
        Boil& boil = *recipe.boil;
        if (!present_ && boil.steps && !boil.steps->empty()) {
            throw std::invalid_argument("the boil still has steps");
        }
        wasPresent_ = boil.steps.has_value();
        if (present_) {
            if (!boil.steps) {
                boil.steps.emplace();
            }
        } else {
            boil.steps.reset();
        }
    }

    void undo(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        if (!recipe.boil) {
            return;
        }
        if (wasPresent_) {
            if (!recipe.boil->steps) {
                recipe.boil->steps.emplace();
            }
        } else {
            recipe.boil->steps.reset();
        }
    }

    std::string label() const override {
        return present_ ? "Add Boil Steps" : "Remove Boil Steps";
    }

private:
    EntityId recipe_;
    bool present_;
    bool wasPresent_ = false;
};

Miscellaneous& requireMiscellaneous(Recipe& recipe, EntityId id) {
    if (recipe.miscellaneous) {
        const auto it = std::find_if(recipe.miscellaneous->begin(), recipe.miscellaneous->end(),
                                     [id](const Miscellaneous& m) { return m.id == id; });
        if (it != recipe.miscellaneous->end()) {
            return *it;
        }
    }
    throw std::invalid_argument("no miscellaneous ingredient with id " + std::to_string(id));
}

class SetSaltType : public Edit {
public:
    SetSaltType(EntityId recipe, EntityId miscellaneous, std::optional<SaltKind> salt)
        : recipe_(recipe), miscellaneous_(miscellaneous), after_(salt) {}

    void apply(Document& document) override {
        Miscellaneous& misc = requireMiscellaneous(requireRecipe(document, recipe_), miscellaneous_);
        before_ = misc.salt;
        misc.salt = after_;
    }

    void undo(Document& document) override {
        requireMiscellaneous(requireRecipe(document, recipe_), miscellaneous_).salt = before_;
    }

    std::string label() const override { return "Set Salt Type"; }

private:
    EntityId recipe_;
    EntityId miscellaneous_;
    std::optional<SaltKind> after_;
    std::optional<SaltKind> before_;
};

// The price slot and amount of an ingredient of any priced kind.
struct PriceTarget {
    std::optional<Price>& price;
    const Quantity& amount;
};

template <typename Item>
PriceTarget findPriceTarget(std::vector<Item>& items, EntityId id, const char* what) {
    const auto it = std::find_if(items.begin(), items.end(), [id](const Item& item) { return item.id == id; });
    if (it == items.end()) {
        throw std::invalid_argument(std::string("no ") + what + " with id " + std::to_string(id));
    }
    return PriceTarget{it->price, it->amount};
}

PriceTarget requirePriceTarget(Recipe& recipe, IngredientKind kind, EntityId id) {
    switch (kind) {
        case IngredientKind::Fermentable: return findPriceTarget(recipe.fermentables, id, "fermentable");
        case IngredientKind::Hop:
            if (recipe.hops) {
                return findPriceTarget(*recipe.hops, id, "hop");
            }
            throw std::invalid_argument("no hop with id " + std::to_string(id));
        case IngredientKind::Culture:
            if (recipe.cultures) {
                return findPriceTarget(*recipe.cultures, id, "culture");
            }
            throw std::invalid_argument("no culture with id " + std::to_string(id));
        case IngredientKind::Miscellaneous:
            if (recipe.miscellaneous) {
                return findPriceTarget(*recipe.miscellaneous, id, "miscellaneous ingredient");
            }
            throw std::invalid_argument("no miscellaneous ingredient with id " + std::to_string(id));
    }
    throw std::invalid_argument("unknown ingredient kind");
}

class SetPrice : public Edit {
public:
    SetPrice(EntityId recipe, IngredientKind kind, EntityId ingredient, std::optional<Price> price)
        : recipe_(recipe), kind_(kind), ingredient_(ingredient), after_(std::move(price)) {}

    void apply(Document& document) override {
        PriceTarget target = requirePriceTarget(requireRecipe(document, recipe_), kind_, ingredient_);
        if (after_) {
            if (after_->amount < Rational(0)) {
                throw std::invalid_argument("a price cannot be negative");
            }
            const Dimension per = dimensionOf(after_->per);
            if (per != Dimension::Mass && per != Dimension::Volume && per != Dimension::Count) {
                throw std::invalid_argument("a price is for a mass, a volume, or a count");
            }
            if (per != target.amount.dimension()) {
                throw std::invalid_argument("the price unit must measure the same thing as the ingredient's amount");
            }
        }
        before_ = target.price;
        target.price = after_;
    }

    void undo(Document& document) override {
        requirePriceTarget(requireRecipe(document, recipe_), kind_, ingredient_).price = before_;
    }

    std::string label() const override { return after_ ? "Set Price" : "Clear Price"; }

private:
    EntityId recipe_;
    IngredientKind kind_;
    EntityId ingredient_;
    std::optional<Price> after_;
    std::optional<Price> before_;
};

class SetCurrency : public Edit {
public:
    SetCurrency(EntityId recipe, std::string currency) : recipe_(recipe), after_(std::move(currency)) {}

    void apply(Document& document) override {
        if (after_.size() != 3 ||
            !std::all_of(after_.begin(), after_.end(), [](unsigned char c) { return std::isalpha(c) != 0 && c < 128; })) {
            throw std::invalid_argument("a currency is a three-letter code, such as USD");
        }
        std::transform(after_.begin(), after_.end(), after_.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.currency;
        recipe.currency = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).currency = before_; }

    std::string label() const override { return "Change Currency"; }

private:
    EntityId recipe_;
    std::string after_;
    std::string before_;
};

class SetFermentationEfficiency : public Edit {
public:
    SetFermentationEfficiency(EntityId recipe, Quantity efficiency) : recipe_(recipe), after_(std::move(efficiency)) {}

    void apply(Document& document) override {
        const Rational& fraction = after_.canonical();
        if (after_.dimension() != Dimension::Fraction || !(Rational(0) < fraction) || fraction < Rational(0) ||
            Rational(1) < fraction) {
            throw std::invalid_argument("the fermentation efficiency must be above 0 % and up to 100 %");
        }
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.fermentationEfficiency;
        recipe.fermentationEfficiency = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).fermentationEfficiency = before_; }

    std::string label() const override { return "Set Fermentation Efficiency"; }

private:
    EntityId recipe_;
    Quantity after_;
    Quantity before_;
};

class SetBoilOff : public Edit {
public:
    SetBoilOff(EntityId recipe, BoilOff boilOff) : recipe_(recipe), after_(std::move(boilOff)) {}

    void apply(Document& document) override {
        const Rational& fraction = after_.evaporationRate.canonical();
        if (after_.evaporationRate.dimension() != Dimension::Fraction || fraction < Rational(0) ||
            !(fraction < Rational(1))) {
            throw std::invalid_argument("the evaporation rate must be from 0 % up to, not including, 100 %");
        }
        if (after_.trubLoss.dimension() != Dimension::Volume || after_.trubLoss.canonical() < Rational(0)) {
            throw std::invalid_argument("the trub loss must be a volume of zero or more");
        }
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.boilOff;
        recipe.boilOff = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).boilOff = before_; }

    std::string label() const override { return "Set Boil-Off"; }

private:
    EntityId recipe_;
    BoilOff after_;
    BoilOff before_;
};

class SetPlanning : public Edit {
public:
    SetPlanning(EntityId recipe, std::string planning) : recipe_(recipe), after_(std::move(planning)) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.planning;
        recipe.planning = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).planning = before_; }

    std::string label() const override { return "Change Planning"; }

private:
    EntityId recipe_;
    std::string after_;
    std::string before_;
};

class SetBjcpStyle : public Edit {
public:
    SetBjcpStyle(EntityId recipe, std::optional<std::string> style) : recipe_(recipe), after_(std::move(style)) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.bjcpStyle;
        recipe.bjcpStyle = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).bjcpStyle = before_; }

    std::string label() const override { return "Set Style"; }

private:
    EntityId recipe_;
    std::optional<std::string> after_;
    std::optional<std::string> before_;
};

styles::UserStyle& requireUserStyle(Document& document, const std::string& id) {
    styles::UserStyle* style = document.findUserStyle(id);
    if (style == nullptr) {
        throw std::invalid_argument("no style with id \"" + id + "\"");
    }
    return *style;
}

class AddUserStyle : public Edit {
public:
    explicit AddUserStyle(styles::UserStyle style) : style_(std::move(style)) {}

    void apply(Document& document) override {
        if (style_.id.empty()) {
            style_.id = styles::newUserStyleId();
        }
        if (!styles::isUserStyleId(style_.id)) {
            throw std::invalid_argument("a user style id must begin with \"" + std::string(styles::kUserStylePrefix) + "\"");
        }
        if (document.findUserStyle(style_.id) != nullptr) {
            throw std::invalid_argument("a style with id \"" + style_.id + "\" already exists");
        }
        styles::validateUserStyle(style_);
        document.userStyles.push_back(style_);
    }

    void undo(Document& document) override {
        auto& list = document.userStyles;
        list.erase(std::remove_if(list.begin(), list.end(),
                                    [this](const styles::UserStyle& s) { return s.id == style_.id; }),
                     list.end());
    }

    std::string label() const override { return "Add Style"; }

private:
    styles::UserStyle style_;
};

class SetUserStyle : public Edit {
public:
    explicit SetUserStyle(styles::UserStyle style) : after_(std::move(style)) {}

    void apply(Document& document) override {
        styles::validateUserStyle(after_);
        styles::UserStyle& style = requireUserStyle(document, after_.id);
        before_ = style;
        style = after_;
    }

    void undo(Document& document) override { requireUserStyle(document, after_.id) = before_; }

    std::string label() const override { return "Edit Style"; }

private:
    styles::UserStyle after_;
    styles::UserStyle before_;
};

class RemoveUserStyle : public Edit {
public:
    explicit RemoveUserStyle(std::string id) : id_(std::move(id)) {}

    void apply(Document& document) override {
        auto& list = document.userStyles;
        const auto found = std::find_if(list.begin(), list.end(),
                                        [this](const styles::UserStyle& s) { return s.id == id_; });
        if (found == list.end()) {
            throw std::invalid_argument("no style with id \"" + id_ + "\"");
        }
        index_ = static_cast<std::size_t>(found - list.begin());
        removed_ = *found;
        list.erase(found);

        clearedRecipes_.clear();
        for (Recipe& recipe : document.recipes) {
            if (recipe.bjcpStyle && *recipe.bjcpStyle == id_) {
                clearedRecipes_.push_back(recipe.id);
                recipe.bjcpStyle.reset();
            }
        }
    }

    void undo(Document& document) override {
        for (EntityId recipe : clearedRecipes_) {
            requireRecipe(document, recipe).bjcpStyle = id_;
        }
        auto& list = document.userStyles;
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(index_, list.size())), removed_);
    }

    std::string label() const override { return "Remove Style"; }

private:
    std::string id_;
    styles::UserStyle removed_;
    std::size_t index_ = 0;
    std::vector<EntityId> clearedRecipes_;
};

class MoveRecipeToFolder : public Edit {
public:
    MoveRecipeToFolder(EntityId recipe, std::optional<EntityId> folder) : recipe_(recipe), after_(folder) {}

    void apply(Document& document) override {
        if (after_ && document.findFolder(*after_) == nullptr) {
            throw std::invalid_argument("no folder with id " + std::to_string(*after_));
        }
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.folder;
        recipe.folder = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).folder = before_; }

    std::string label() const override { return "Move Recipe"; }

private:
    EntityId recipe_;
    std::optional<EntityId> after_;
    std::optional<EntityId> before_;
};

class AddFolder : public Edit {
public:
    explicit AddFolder(Folder folder) : folder_(std::move(folder)) {}

    void apply(Document& document) override {
        validateFolderName(folder_.name);
        if (folder_.id == 0) {
            folder_.id = document.allocateId();
        }
        if (folder_.uuid.isNil()) {
            folder_.uuid = Uuid::generate();
        }
        document.folders.push_back(folder_);
    }

    void undo(Document& document) override {
        auto& folders = document.folders;
        folders.erase(std::remove_if(folders.begin(), folders.end(),
                                     [this](const Folder& f) { return f.id == folder_.id; }),
                      folders.end());
    }

    std::string label() const override { return "Add Folder"; }

private:
    Folder folder_;
};

class RenameFolder : public Edit {
public:
    RenameFolder(EntityId folder, std::string name) : folder_(folder), after_(std::move(name)) {}

    void apply(Document& document) override {
        validateFolderName(after_);
        Folder& folder = requireFolder(document, folder_);
        if (folder.trash) {
            throw std::invalid_argument("Recently Deleted cannot be renamed");
        }
        before_ = folder.name;
        folder.name = after_;
    }

    void undo(Document& document) override { requireFolder(document, folder_).name = before_; }

    std::string label() const override { return "Rename Folder"; }

private:
    EntityId folder_;
    std::string after_;
    std::string before_;
};

// Removes a folder and moves its recipes to the top level. Undo puts the folder
// back where it was, and files its recipes in it again.
class RemoveFolder : public Edit {
public:
    explicit RemoveFolder(EntityId folder) : folder_(folder) {}

    void apply(Document& document) override {
        const auto found = std::find_if(document.folders.begin(), document.folders.end(),
                                        [this](const Folder& f) { return f.id == folder_; });
        if (found == document.folders.end()) {
            throw std::invalid_argument("no folder with id " + std::to_string(folder_));
        }
        if (found->trash) {
            throw std::invalid_argument("Recently Deleted cannot be removed");
        }
        index_ = static_cast<std::size_t>(found - document.folders.begin());
        removed_ = *found;
        movedRecipes_.clear();
        for (Recipe& recipe : document.recipes) {
            if (recipe.folder == folder_) {
                movedRecipes_.push_back(recipe.id);
                recipe.folder.reset();
            }
        }
        document.folders.erase(found);
    }

    void undo(Document& document) override {
        document.folders.insert(document.folders.begin() + static_cast<std::ptrdiff_t>(index_), removed_);
        for (const EntityId id : movedRecipes_) {
            if (Recipe* recipe = document.findRecipe(id)) {
                recipe->folder = folder_;
            }
        }
    }

    std::string label() const override { return "Remove Folder"; }

private:
    EntityId folder_;
    std::size_t index_ = 0;
    Folder removed_;
    std::vector<EntityId> movedRecipes_;
};

class SetRecipeType : public Edit {
public:
    SetRecipeType(EntityId recipe, RecipeType type) : recipe_(recipe), after_(type) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.type;
        recipe.type = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).type = before_; }

    std::string label() const override { return "Set Recipe Type"; }

private:
    EntityId recipe_;
    RecipeType after_;
    RecipeType before_ = RecipeType::AllGrain;
};

class AddMiscellaneous : public Edit {
public:
    AddMiscellaneous(EntityId recipe, Miscellaneous miscellaneous)
        : recipe_(recipe), miscellaneous_(std::move(miscellaneous)) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        validateMiscellaneous(miscellaneous_);
        if (miscellaneous_.id == 0) {
            miscellaneous_.id = document.allocateId();
        }
        if (miscellaneous_.uuid.isNil()) {
            miscellaneous_.uuid = Uuid::generate();
        }
        wasPresent_ = recipe.miscellaneous.has_value();
        if (!recipe.miscellaneous) {
            recipe.miscellaneous.emplace();
        }
        recipe.miscellaneous->push_back(miscellaneous_);
    }

    void undo(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        if (recipe.miscellaneous) {
            auto& list = *recipe.miscellaneous;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [this](const Miscellaneous& m) { return m.id == miscellaneous_.id; }),
                       list.end());
        }
        if (!wasPresent_) {
            recipe.miscellaneous.reset();
        }
    }

    std::string label() const override { return "Add Miscellaneous"; }

private:
    EntityId recipe_;
    Miscellaneous miscellaneous_;
    bool wasPresent_ = false;
};

class RemoveMiscellaneous : public Edit {
public:
    RemoveMiscellaneous(EntityId recipe, EntityId miscellaneous) : recipe_(recipe), target_(miscellaneous) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        Miscellaneous& found = requireMiscellaneous(recipe, target_);
        index_ = static_cast<std::size_t>(&found - recipe.miscellaneous->data());
        removed_ = found;
        recipe.miscellaneous->erase(recipe.miscellaneous->begin() + static_cast<std::ptrdiff_t>(index_));
    }

    void undo(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        if (!recipe.miscellaneous) {
            recipe.miscellaneous.emplace();
        }
        auto& list = *recipe.miscellaneous;
        const std::size_t at = std::min(index_, list.size());
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), removed_);
    }

    std::string label() const override { return "Remove Miscellaneous"; }

private:
    EntityId recipe_;
    EntityId target_;
    std::size_t index_ = 0;
    Miscellaneous removed_;
};

class SetAdditive : public Edit {
public:
    SetAdditive(EntityId recipe, EntityId miscellaneous, std::optional<Additive> additive)
        : recipe_(recipe), miscellaneous_(miscellaneous), after_(additive) {}

    void apply(Document& document) override {
        Miscellaneous& misc = requireMiscellaneous(requireRecipe(document, recipe_), miscellaneous_);
        if (after_ == Additive::Sulfite && misc.amount.dimension() != Dimension::Mass) {
            throw std::invalid_argument("a sulfite is measured by mass");
        }
        before_ = misc.additive;
        misc.additive = after_;
    }

    void undo(Document& document) override {
        requireMiscellaneous(requireRecipe(document, recipe_), miscellaneous_).additive = before_;
    }

    std::string label() const override { return "Set Additive"; }

private:
    EntityId recipe_;
    EntityId miscellaneous_;
    std::optional<Additive> after_;
    std::optional<Additive> before_;
};

// Hops, cultures, and waters are optional lists that behave alike, so they share
// these two commands. Each type supplies the rules its items must meet.
template <typename T>
using ListMember = std::optional<std::vector<T>> Recipe::*;

template <typename Values>
bool isOneOf(const Values& values, const std::string& value) {
    return std::find(values.begin(), values.end(), std::string_view(value)) != values.end();
}

void requireDimension(const Quantity& quantity, Dimension dimension, const char* message) {
    if (quantity.dimension() != dimension) {
        throw std::invalid_argument(message);
    }
}

void requirePercentage(const std::optional<Quantity>& quantity, const char* message) {
    if (quantity) {
        requireDimension(*quantity, Dimension::Fraction, message);
    }
}

// Times are whole numbers of their own unit, as the BeerJSON reader requires.
void validateTiming(const Timing& timing) {
    for (const std::optional<Quantity>* time : {&timing.time, &timing.duration}) {
        if (!*time) {
            continue;
        }
        requireDimension(**time, Dimension::Time, "a timing time and duration are measured in time units");
        if (!(*time)->valueIn((*time)->unit()).isInteger()) {
            throw std::invalid_argument("a timing time and duration are whole numbers");
        }
    }
    if (timing.use && !isOneOf(schema::kTimingUses, *timing.use)) {
        throw std::invalid_argument("\"" + *timing.use + "\" is not a timing use");
    }
}

void validateHop(const Hop& hop) {
    requireDimension(hop.alphaAcid, Dimension::Fraction, "the alpha acid must be a percentage");
    requirePercentage(hop.betaAcid, "the beta acid must be a percentage");
    if (hop.amount.dimension() != Dimension::Mass && hop.amount.dimension() != Dimension::Volume) {
        throw std::invalid_argument("a hop is measured by mass or volume");
    }
    if (hop.form && !isOneOf(schema::kHopForms, *hop.form)) {
        throw std::invalid_argument("\"" + *hop.form + "\" is not a hop form");
    }
    validateTiming(hop.timing);
}

void validateCulture(const Culture& culture) {
    if (!isOneOf(schema::kCultureTypes, culture.type)) {
        throw std::invalid_argument("\"" + culture.type + "\" is not a culture type");
    }
    if (!isOneOf(schema::kCultureForms, culture.form)) {
        throw std::invalid_argument("\"" + culture.form + "\" is not a culture form");
    }
    const Dimension dimension = culture.amount.dimension();
    if (dimension != Dimension::Mass && dimension != Dimension::Volume && dimension != Dimension::Count) {
        throw std::invalid_argument("a culture is measured by mass, volume, or count");
    }
    requirePercentage(culture.attenuation, "the attenuation must be a percentage");
    if (culture.timing) {
        validateTiming(*culture.timing);
    }
}

void validateWater(const WaterAddition& addition) {
    const WaterProfile& water = addition.water;
    for (const Quantity* mineral : {&water.calcium, &water.bicarbonate, &water.sulfate, &water.chloride,
                                    &water.sodium, &water.magnesium}) {
        requireDimension(*mineral, Dimension::Concentration, "a water mineral is a concentration");
    }
    for (const std::optional<Quantity>* mineral :
         {&water.carbonate, &water.potassium, &water.iron, &water.nitrate, &water.nitrite, &water.fluoride}) {
        if (*mineral) {
            requireDimension(**mineral, Dimension::Concentration, "a water mineral is a concentration");
        }
    }
    if (addition.amount) {
        requireDimension(*addition.amount, Dimension::Volume, "a water addition is measured by volume");
    }
}

std::string titled(std::string noun) {
    if (!noun.empty()) {
        noun[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(noun[0])));
    }
    return noun;
}

template <typename T>
class AddToList : public Edit {
public:
    AddToList(EntityId recipe, ListMember<T> list, T item, std::string noun, void (*validate)(const T&))
        : recipe_(recipe), list_(list), item_(std::move(item)), noun_(std::move(noun)), validate_(validate) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        validate_(item_);
        if (item_.id == 0) {
            item_.id = document.allocateId();
        }
        if (item_.uuid.isNil()) {
            item_.uuid = Uuid::generate();
        }
        auto& list = recipe.*list_;
        wasPresent_ = list.has_value();
        if (!list) {
            list.emplace();
        }
        list->push_back(item_);
    }

    void undo(Document& document) override {
        auto& list = requireRecipe(document, recipe_).*list_;
        if (list) {
            list->erase(std::remove_if(list->begin(), list->end(), [this](const T& x) { return x.id == item_.id; }),
                        list->end());
        }
        if (!wasPresent_) {
            list.reset();
        }
    }

    std::string label() const override { return "Add " + titled(noun_); }

private:
    EntityId recipe_;
    ListMember<T> list_;
    T item_;
    std::string noun_;
    void (*validate_)(const T&);
    bool wasPresent_ = false;
};

template <typename T>
class RemoveFromList : public Edit {
public:
    RemoveFromList(EntityId recipe, ListMember<T> list, EntityId target, std::string noun)
        : recipe_(recipe), list_(list), target_(target), noun_(std::move(noun)) {}

    void apply(Document& document) override {
        auto& list = requireRecipe(document, recipe_).*list_;
        if (list) {
            const auto it = std::find_if(list->begin(), list->end(), [this](const T& x) { return x.id == target_; });
            if (it != list->end()) {
                index_ = static_cast<std::size_t>(it - list->begin());
                removed_ = *it;
                list->erase(it);
                return;
            }
        }
        throw std::invalid_argument("no " + noun_ + " with id " + std::to_string(target_));
    }

    void undo(Document& document) override {
        auto& list = requireRecipe(document, recipe_).*list_;
        if (!list) {
            list.emplace();
        }
        const std::size_t at = std::min(index_, list->size());
        list->insert(list->begin() + static_cast<std::ptrdiff_t>(at), removed_);
    }

    std::string label() const override { return "Remove " + titled(noun_); }

private:
    EntityId recipe_;
    ListMember<T> list_;
    EntityId target_;
    std::string noun_;
    std::size_t index_ = 0;
    T removed_;
};

// The list an item sits in, as a pointer, or nullptr when the list is absent.
template <typename T>
using ListAccess = std::vector<T>* (*)(Recipe&);

// Replaces an item in its list, keeping its position, its id, and its UUID. The
// new item is checked by the same rules as the add command.
template <typename T>
class ReplaceInList : public Edit {
public:
    ReplaceInList(EntityId recipe, ListAccess<T> access, T item, std::string noun, void (*validate)(const T&))
        : recipe_(recipe), access_(access), item_(std::move(item)), noun_(std::move(noun)), validate_(validate) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        validate_(item_);
        T& current = find(recipe);
        before_ = current;
        item_.uuid = before_.uuid;
        current = item_;
    }

    void undo(Document& document) override { find(requireRecipe(document, recipe_)) = before_; }

    std::string label() const override { return "Edit " + titled(noun_); }

private:
    T& find(Recipe& recipe) {
        if (auto* list = access_(recipe)) {
            for (T& x : *list) {
                if (x.id == item_.id) {
                    return x;
                }
            }
        }
        throw std::invalid_argument("no " + noun_ + " with id " + std::to_string(item_.id));
    }

    EntityId recipe_;
    ListAccess<T> access_;
    T item_;
    std::string noun_;
    void (*validate_)(const T&);
    T before_;
};

// Removes a recipe from the document. Undo puts it back at its old place.
class RemoveRecipe : public Edit {
public:
    explicit RemoveRecipe(EntityId recipe) : recipe_(recipe) {}

    void apply(Document& document) override {
        auto& recipes = document.recipes;
        const auto it = std::find_if(recipes.begin(), recipes.end(),
                                     [this](const Recipe& r) { return r.id == recipe_; });
        if (it == recipes.end()) {
            throw std::invalid_argument("no recipe with id " + std::to_string(recipe_));
        }
        index_ = static_cast<std::size_t>(it - recipes.begin());
        removed_ = *it;
        recipes.erase(it);
    }

    void undo(Document& document) override {
        auto& recipes = document.recipes;
        const std::size_t at = std::min(index_, recipes.size());
        recipes.insert(recipes.begin() + static_cast<std::ptrdiff_t>(at), removed_);
    }

    std::string label() const override { return "Delete Permanently"; }

private:
    EntityId recipe_;
    std::size_t index_ = 0;
    Recipe removed_;
};

// Moves a recipe into the Recently Deleted folder, and makes the folder if there is none. Undo
// files the recipe back where it was, and removes the folder if this edit made it.
class TrashRecipe : public Edit {
public:
    explicit TrashRecipe(EntityId recipe) : recipe_(recipe) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        if (const Folder* current = recipe.folder ? document.findFolder(*recipe.folder) : nullptr;
            current != nullptr && current->trash) {
            throw std::invalid_argument("the recipe is already in Recently Deleted");
        }
        Folder* trash = document.trashFolder();
        madeFolder_ = trash == nullptr;
        if (madeFolder_) {
            // A redo makes the same folder again, with the id and UUID the first apply gave it.
            if (folder_.id == 0) {
                folder_.id = document.allocateId();
                folder_.uuid = Uuid::generate();
                folder_.name = kTrashFolderName;
                folder_.trash = true;
            }
            document.folders.push_back(folder_);
            trash = &document.folders.back();
        }
        before_ = recipe.folder;
        recipe.folder = trash->id;
    }

    void undo(Document& document) override {
        requireRecipe(document, recipe_).folder = before_;
        if (madeFolder_) {
            auto& folders = document.folders;
            folders.erase(std::remove_if(folders.begin(), folders.end(),
                                         [this](const Folder& f) { return f.id == folder_.id; }),
                          folders.end());
        }
    }

    std::string label() const override { return "Delete Recipe"; }

private:
    EntityId recipe_;
    std::optional<EntityId> before_;
    Folder folder_;
    bool madeFolder_ = false;
};

// Gives an item and its copy a new id and UUID. Only a copy that is being made needs
// one, so it is called once, on the first apply.
template <typename T>
void assignNewIdentity(Document& document, T& item) {
    item.id = document.allocateId();
    item.uuid = Uuid::generate();
}

// Copies a recipe after the original. The copy is built on the first apply, and kept, so
// redo puts back the same copy with the same ids.
class DuplicateRecipe : public Edit {
public:
    explicit DuplicateRecipe(EntityId recipe) : recipe_(recipe) {}

    void apply(Document& document) override {
        auto& recipes = document.recipes;
        const auto it = std::find_if(recipes.begin(), recipes.end(),
                                     [this](const Recipe& r) { return r.id == recipe_; });
        if (it == recipes.end()) {
            throw std::invalid_argument("no recipe with id " + std::to_string(recipe_));
        }
        const auto at = it - recipes.begin() + 1;
        if (copy_.id == 0) {
            copy_ = *it;
            copy_.name += " copy";
            assignNewIdentity(document, copy_);
            for (auto& x : copy_.fermentables) {
                assignNewIdentity(document, x);
            }
            if (copy_.hops) {
                for (auto& x : *copy_.hops) {
                    assignNewIdentity(document, x);
                }
            }
            if (copy_.miscellaneous) {
                for (auto& x : *copy_.miscellaneous) {
                    assignNewIdentity(document, x);
                }
            }
            if (copy_.cultures) {
                for (auto& x : *copy_.cultures) {
                    assignNewIdentity(document, x);
                }
            }
            if (copy_.waters) {
                for (auto& x : *copy_.waters) {
                    assignNewIdentity(document, x);
                }
            }
        }
        recipes.insert(recipes.begin() + at, copy_);
    }

    void undo(Document& document) override {
        auto& recipes = document.recipes;
        recipes.erase(std::remove_if(recipes.begin(), recipes.end(),
                                     [this](const Recipe& r) { return r.id == copy_.id; }),
                      recipes.end());
    }

    std::string label() const override { return "Duplicate Recipe"; }

private:
    EntityId recipe_;
    Recipe copy_;
};

// Copies an ingredient in one of a recipe's lists, after the original. As with
// DuplicateRecipe, the copy is built once, so redo restores it.
template <typename T>
class DuplicateInList : public Edit {
public:
    DuplicateInList(EntityId recipe, ListAccess<T> access, EntityId source, std::string noun)
        : recipe_(recipe), access_(access), source_(source), noun_(std::move(noun)) {}

    void apply(Document& document) override {
        Recipe& recipe = requireRecipe(document, recipe_);
        if (auto* list = access_(recipe)) {
            const auto it = std::find_if(list->begin(), list->end(), [this](const T& x) { return x.id == source_; });
            if (it != list->end()) {
                if (copy_.id == 0) {
                    copy_ = *it;
                    assignNewIdentity(document, copy_);
                }
                list->insert(it + 1, copy_);
                return;
            }
        }
        throw std::invalid_argument("no " + noun_ + " with id " + std::to_string(source_));
    }

    void undo(Document& document) override {
        if (auto* list = access_(requireRecipe(document, recipe_))) {
            list->erase(std::remove_if(list->begin(), list->end(), [this](const T& x) { return x.id == copy_.id; }),
                        list->end());
        }
    }

    std::string label() const override { return "Duplicate " + titled(noun_); }

private:
    EntityId recipe_;
    ListAccess<T> access_;
    EntityId source_;
    std::string noun_;
    T copy_;
};

class SetBrewhouseEfficiency : public Edit {
public:
    SetBrewhouseEfficiency(EntityId recipe, Quantity efficiency) : recipe_(recipe), after_(std::move(efficiency)) {}

    void apply(Document& document) override {
        const Rational& fraction = after_.canonical();
        if (after_.dimension() != Dimension::Fraction || !(Rational(0) < fraction) || Rational(1) < fraction) {
            throw std::invalid_argument("the brewhouse efficiency must be above 0 % and up to 100 %");
        }
        Recipe& recipe = requireRecipe(document, recipe_);
        before_ = recipe.efficiency.brewhouse;
        recipe.efficiency.brewhouse = after_;
    }

    void undo(Document& document) override { requireRecipe(document, recipe_).efficiency.brewhouse = before_; }

    std::string label() const override { return "Set Brewhouse Efficiency"; }

private:
    EntityId recipe_;
    Quantity after_;
    Quantity before_;
};

}  // namespace

EditPtr addHop(EntityId recipe, Hop hop) {
    return std::make_unique<AddToList<Hop>>(recipe, &Recipe::hops, std::move(hop), "hop", validateHop);
}

EditPtr removeHop(EntityId recipe, EntityId hop) {
    return std::make_unique<RemoveFromList<Hop>>(recipe, &Recipe::hops, hop, "hop");
}

EditPtr addCulture(EntityId recipe, Culture culture) {
    return std::make_unique<AddToList<Culture>>(recipe, &Recipe::cultures, std::move(culture), "culture",
                                                validateCulture);
}

EditPtr removeCulture(EntityId recipe, EntityId culture) {
    return std::make_unique<RemoveFromList<Culture>>(recipe, &Recipe::cultures, culture, "culture");
}

EditPtr addWater(EntityId recipe, WaterAddition water) {
    return std::make_unique<AddToList<WaterAddition>>(recipe, &Recipe::waters, std::move(water), "water",
                                                      validateWater);
}

EditPtr removeWater(EntityId recipe, EntityId water) {
    return std::make_unique<RemoveFromList<WaterAddition>>(recipe, &Recipe::waters, water, "water");
}

EditPtr removeRecipe(EntityId recipe) {
    return std::make_unique<RemoveRecipe>(recipe);
}

EditPtr trashRecipe(EntityId recipe) {
    return std::make_unique<TrashRecipe>(recipe);
}

EditPtr duplicateRecipe(EntityId recipe) {
    return std::make_unique<DuplicateRecipe>(recipe);
}

EditPtr duplicateFermentable(EntityId recipe, EntityId fermentable) {
    return std::make_unique<DuplicateInList<Fermentable>>(
        recipe, [](Recipe& r) { return &r.fermentables; }, fermentable, "fermentable");
}

EditPtr duplicateHop(EntityId recipe, EntityId hop) {
    return std::make_unique<DuplicateInList<Hop>>(
        recipe, [](Recipe& r) { return r.hops ? &*r.hops : nullptr; }, hop, "hop");
}

EditPtr duplicateCulture(EntityId recipe, EntityId culture) {
    return std::make_unique<DuplicateInList<Culture>>(
        recipe, [](Recipe& r) { return r.cultures ? &*r.cultures : nullptr; }, culture, "culture");
}

EditPtr duplicateWater(EntityId recipe, EntityId water) {
    return std::make_unique<DuplicateInList<WaterAddition>>(
        recipe, [](Recipe& r) { return r.waters ? &*r.waters : nullptr; }, water, "water");
}

EditPtr duplicateMiscellaneous(EntityId recipe, EntityId miscellaneous) {
    return std::make_unique<DuplicateInList<Miscellaneous>>(
        recipe, [](Recipe& r) { return r.miscellaneous ? &*r.miscellaneous : nullptr; }, miscellaneous,
        "miscellaneous");
}

EditPtr setFermentable(EntityId recipe, Fermentable fermentable) {
    return std::make_unique<ReplaceInList<Fermentable>>(
        recipe, [](Recipe& r) { return &r.fermentables; }, std::move(fermentable), "fermentable",
        validateFermentable);
}

EditPtr setHop(EntityId recipe, Hop hop) {
    return std::make_unique<ReplaceInList<Hop>>(
        recipe, [](Recipe& r) { return r.hops ? &*r.hops : nullptr; }, std::move(hop), "hop", validateHop);
}

EditPtr setCulture(EntityId recipe, Culture culture) {
    return std::make_unique<ReplaceInList<Culture>>(
        recipe, [](Recipe& r) { return r.cultures ? &*r.cultures : nullptr; }, std::move(culture), "culture",
        validateCulture);
}

EditPtr setWater(EntityId recipe, WaterAddition water) {
    return std::make_unique<ReplaceInList<WaterAddition>>(
        recipe, [](Recipe& r) { return r.waters ? &*r.waters : nullptr; }, std::move(water), "water",
        validateWater);
}

EditPtr setMiscellaneous(EntityId recipe, Miscellaneous miscellaneous) {
    return std::make_unique<ReplaceInList<Miscellaneous>>(
        recipe, [](Recipe& r) { return r.miscellaneous ? &*r.miscellaneous : nullptr; },
        std::move(miscellaneous), "miscellaneous ingredient", validateMiscellaneous);
}

EditPtr setRecipeType(EntityId recipe, RecipeType type) {
    return std::make_unique<SetRecipeType>(recipe, type);
}

EditPtr addMiscellaneous(EntityId recipe, Miscellaneous miscellaneous) {
    return std::make_unique<AddMiscellaneous>(recipe, std::move(miscellaneous));
}

EditPtr removeMiscellaneous(EntityId recipe, EntityId miscellaneous) {
    return std::make_unique<RemoveMiscellaneous>(recipe, miscellaneous);
}

EditPtr setAdditive(EntityId recipe, EntityId miscellaneous, std::optional<Additive> additive) {
    return std::make_unique<SetAdditive>(recipe, miscellaneous, additive);
}

EditPtr setSaltType(EntityId recipe, EntityId miscellaneous, std::optional<SaltKind> salt) {
    return std::make_unique<SetSaltType>(recipe, miscellaneous, salt);
}

EditPtr setPrice(EntityId recipe, IngredientKind kind, EntityId ingredient, std::optional<Price> price) {
    return std::make_unique<SetPrice>(recipe, kind, ingredient, std::move(price));
}

EditPtr setCurrency(EntityId recipe, std::string currency) {
    return std::make_unique<SetCurrency>(recipe, std::move(currency));
}

EditPtr setBrewhouseEfficiency(EntityId recipe, Quantity efficiency) {
    return std::make_unique<SetBrewhouseEfficiency>(recipe, std::move(efficiency));
}

// The batch size, efficiency, and boil-off of an equipment profile, as one undo step.
EditPtr applyEquipment(EntityId recipe, Quantity batchSize, Quantity efficiency, BoilOff boilOff) {
    std::vector<EditPtr> parts;
    parts.push_back(setBatchSize(recipe, std::move(batchSize)));
    parts.push_back(setBrewhouseEfficiency(recipe, std::move(efficiency)));
    parts.push_back(setBoilOff(recipe, std::move(boilOff)));
    return std::make_unique<CompositeCommand<Document>>("Apply Equipment Profile", std::move(parts));
}

EditPtr setFermentationEfficiency(EntityId recipe, Quantity efficiency) {
    return std::make_unique<SetFermentationEfficiency>(recipe, std::move(efficiency));
}

EditPtr setBoilOff(EntityId recipe, BoilOff boilOff) {
    return std::make_unique<SetBoilOff>(recipe, std::move(boilOff));
}

EditPtr setBoilStepsPresent(EntityId recipe, bool present) {
    return std::make_unique<SetBoilStepsPresent>(recipe, present);
}

EditPtr setIngredientListPresent(EntityId recipe, IngredientList list, bool present) {
    return std::make_unique<SetIngredientListPresent>(recipe, list, present);
}

EditPtr addRecipe(Recipe recipe) {
    return std::make_unique<AddRecipe>(std::move(recipe));
}

EditPtr setRecipeName(EntityId recipe, std::string name) {
    return std::make_unique<SetRecipeName>(recipe, std::move(name));
}

EditPtr setBatchSize(EntityId recipe, Quantity batchSize) {
    return std::make_unique<SetBatchSize>(recipe, std::move(batchSize));
}

EditPtr addFermentable(EntityId recipe, Fermentable fermentable) {
    return std::make_unique<AddFermentable>(recipe, std::move(fermentable));
}

EditPtr removeFermentable(EntityId recipe, EntityId fermentable) {
    return std::make_unique<RemoveFermentable>(recipe, fermentable);
}

EditPtr setPlanning(EntityId recipe, std::string planning) {
    return std::make_unique<SetPlanning>(recipe, std::move(planning));
}

EditPtr setBjcpStyle(EntityId recipe, std::optional<std::string> style) {
    return std::make_unique<SetBjcpStyle>(recipe, std::move(style));
}

EditPtr addUserStyle(styles::UserStyle style) {
    return std::make_unique<AddUserStyle>(std::move(style));
}

EditPtr setUserStyle(styles::UserStyle style) {
    return std::make_unique<SetUserStyle>(std::move(style));
}

EditPtr removeUserStyle(std::string id) {
    return std::make_unique<RemoveUserStyle>(std::move(id));
}

EditPtr moveRecipeToFolder(EntityId recipe, std::optional<EntityId> folder) {
    return std::make_unique<MoveRecipeToFolder>(recipe, folder);
}

EditPtr addFolder(Folder folder) {
    return std::make_unique<AddFolder>(std::move(folder));
}

EditPtr renameFolder(EntityId folder, std::string name) {
    return std::make_unique<RenameFolder>(folder, std::move(name));
}

EditPtr removeFolder(EntityId folder) {
    return std::make_unique<RemoveFolder>(folder);
}

}  // namespace kettlelogic::model
