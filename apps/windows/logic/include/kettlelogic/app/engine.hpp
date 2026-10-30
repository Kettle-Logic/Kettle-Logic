#pragma once

#include "kettlelogic/calc/brew_plan.hpp"
#include "kettlelogic/calc/journal.hpp"
#include "kettlelogic/calc/procedure.hpp"
#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/calc/starter.hpp"
#include "kettlelogic/calc/style.hpp"
#include "kettlelogic/calc/tools.hpp"
#include "kettlelogic/command.hpp"
#include "kettlelogic/model/commands.hpp"
#include "kettlelogic/model/model.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "kettlelogic/app/planning.hpp"

// The front end's side of the engine. Unlike the macOS app, which reaches the engine through the C
// facade, the Windows app links the core directly (docs/architecture.md): a document is a
// model::Document with its CommandStack, and the views read the model and the calc results as they are.
// Every change goes through a command, so undo and redo cover all of them.
namespace kettlelogic::app {

// A failure reported by the engine, with its message.
class EngineError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using EntityId = model::EntityId;

// The kinds of ingredient the app edits.
enum class IngredientKind { fermentable, hop, culture, water, miscellaneous };

const std::vector<IngredientKind>& allIngredientKinds();
std::string ingredientKindTitle(IngredientKind kind);   // "Fermentable", "Hop", ...
std::string ingredientKindName(IngredientKind kind);    // the stable name: "fermentable", "hop", ...
std::optional<IngredientKind> ingredientKindFromName(const std::string& name);

// An exact quantity as decimal text, as the engine writes it, so no value is rounded on its way to the screen.
struct QuantityText {
    std::string value;
    std::string unit;

    std::string display() const { return value + " " + unit; }
};
QuantityText describe(const Quantity& quantity);
QuantityText describe(const Measure& measure);

// What an ingredient costs: `amount` in the recipe's currency for each one `per`, such as 4.5 per "kg".
struct PriceText {
    std::string amount;
    std::string per;

    friend bool operator==(const PriceText&, const PriceText&) = default;
};
std::optional<PriceText> describe(const std::optional<model::Price>& price);

// Lists the pickers offer, by BeerJSON name.
const std::vector<std::string>& currencyCodes();
const std::vector<std::string>& recipeTypes();
const std::vector<std::string>& fermentableTypes();
const std::vector<std::string>& fermentableUnits();
const std::vector<std::string>& miscellaneousTypes();
const std::vector<std::string>& miscellaneousUnits();
const std::vector<std::string>& hopForms();
const std::vector<std::string>& hopUnits();
const std::vector<std::string>& hopTimeUnits();
const std::vector<std::string>& hopUses();
const std::vector<std::string>& cultureTypes();
const std::vector<std::string>& cultureForms();
const std::vector<std::string>& cultureUnits();

// The name shown for a hop use: "add_to_boil" is "Boil", and an empty use is "Not set".
std::string hopUseName(const std::string& use);
// The name of a hop form, where an empty value means it is not set.
std::string hopFormName(const std::string& form);

// The units a price can be per, for an ingredient measured in `amountUnit`. A price has to measure the same
// thing as the amount, so a price per kg suits an amount in lb, and a price per package suits a count.
std::vector<std::string> priceUnits(const std::string& amountUnit);

// The choices for a picker: the usual list, with the current value added when the list does not have it, so
// an existing item keeps its own unit or type.
std::vector<std::string> options(const std::vector<std::string>& base, const std::string& current);

// The values of one ingredient, as the exact decimal text the user typed.
struct FermentableInput {
    std::string name, type, amount, unit, yieldValue, yieldUnit, colorValue, colorUnit;
};
struct HopInput {
    std::string name, alphaPercent;
    std::optional<std::string> betaPercent;
    std::string amount, unit;
    std::optional<std::string> form, use, time, timeUnit;  // `time` and `timeUnit` are both given or both empty
};
struct CultureInput {
    std::string name, type, form, amount, unit;
    std::optional<std::string> attenuationPercent;
};
struct WaterInput {
    std::string name, calcium, bicarbonate, sulfate, chloride, sodium, magnesium;  // mg/L
    std::optional<std::string> amountLiters;
};
struct MiscellaneousInput {
    std::string name, type, amount, unit;
};

// A recipe's values placed against its style.
struct StyleComparison {
    std::string id;    // the guideline id, or the user style's id
    std::string name;  // the guideline's name, or the user style's name
    std::vector<calc::Check> checks;
};

// A style the picker offers: a guideline style, or one the user defined.
struct StyleChoice {
    std::string id;
    std::string title;  // "21A American IPA" for a guideline style, the user's name for their own
    bool custom = false;
};

// One open recipe file. It owns the engine's document and its command stack.
class RecipeDocument {
public:
    // A new document with one empty all-grain recipe, not yet saved anywhere.
    static std::shared_ptr<RecipeDocument> makeNew();
    // Opens a BeerJSON recipe and its sidecar, with the fallbacks in docs/model.md.
    static std::shared_ptr<RecipeDocument> open(const std::filesystem::path& file);

    // Identifies the document in the sidebar while the app runs. Recipe ids are only unique within one
    // document.
    const std::string& id() const { return id_; }
    // Where the document lives on disk, or nothing for a new, unsaved document.
    const std::optional<std::filesystem::path>& file() const { return file_; }
    // The file name without its extension, as the sidebar shows it.
    std::string displayName() const;
    // Where the open document came from: "current", "autosave", or "previous".
    const std::string& source() const { return source_; }

    void save(const std::filesystem::path& file);

    // Reading.
    const std::vector<model::Recipe>& recipes() const { return document_.recipes; }
    const std::vector<model::Folder>& folders() const { return document_.folders; }
    const model::Recipe* recipe(EntityId id) const { return document_.findRecipe(id); }
    // The calculated values of a recipe, which are recalculated after every change.
    const calc::Results& results(EntityId recipe) const;
    // Whether the recipe differs from its last saved copy, or has never been saved.
    bool isUnsaved(EntityId recipe) const;

    bool canUndo() const { return stack_.canUndo(); }
    bool canRedo() const { return stack_.canRedo(); }
    std::string undoLabel() const { return stack_.undoLabel(); }
    std::string redoLabel() const { return stack_.redoLabel(); }
    // Each returns whether it did something.
    bool undo();
    bool redo();

    // A bumped number that changes after every edit, so a view can tell when to draw again.
    unsigned long long revision() const { return revision_; }

    // Commands. `value` is exact decimal text, such as "5.25".
    void rename(EntityId recipe, const std::string& name);
    void setType(EntityId recipe, const std::string& type);
    void setBatchSize(EntityId recipe, const std::string& value, const std::string& unit);
    void setFermentationEfficiency(EntityId recipe, const std::string& percent);
    void setBoilOff(EntityId recipe, const std::string& evaporationPercent, const std::string& trubLossLiters);
    // Sets the batch size, brewhouse efficiency, and boil-off, as one undo step.
    void applyEquipment(EntityId recipe, const std::string& batchValue, const std::string& batchUnit,
                        const std::string& efficiencyPercent, const std::string& evaporationPercent,
                        const std::string& trubLossLiters);

    // Runs `work` and records everything it does as one undo step called `label`. If `work` throws, whatever it
    // changed is taken back and the error is rethrown, so a half-built result never remains.
    void grouped(const std::string& label, const std::function<void()>& work);

    // Adds a recipe with a default name, and returns its id.
    EntityId addRecipe();
    // Deletes a recipe for good. Undo brings it back.
    void removeRecipe(EntityId recipe);
    // Moves a recipe to Recently Deleted, where it stays until it is restored or removed for good.
    void trashRecipe(EntityId recipe);
    // Whether a recipe is in Recently Deleted.
    bool isDeleted(EntityId recipe) const;
    // Copies a recipe after the original, named "<name> copy", and returns the copy's id.
    EntityId duplicateRecipe(EntityId recipe);
    // Copies an ingredient within its recipe, after the original.
    void duplicateIngredient(IngredientKind kind, EntityId recipe, EntityId item);

    void addFermentable(EntityId recipe, const FermentableInput& input);
    void setFermentable(EntityId recipe, EntityId item, const FermentableInput& input);
    void removeFermentable(EntityId recipe, EntityId item);
    void addHop(EntityId recipe, const HopInput& input);
    void setHop(EntityId recipe, EntityId item, const HopInput& input);
    void removeHop(EntityId recipe, EntityId item);
    void addCulture(EntityId recipe, const CultureInput& input);
    void setCulture(EntityId recipe, EntityId item, const CultureInput& input);
    void removeCulture(EntityId recipe, EntityId item);
    void addWater(EntityId recipe, const WaterInput& input);
    void setWater(EntityId recipe, EntityId item, const WaterInput& input);
    void removeWater(EntityId recipe, EntityId item);
    void addMiscellaneous(EntityId recipe, const MiscellaneousInput& input);
    void setMiscellaneous(EntityId recipe, EntityId item, const MiscellaneousInput& input);
    void removeMiscellaneous(EntityId recipe, EntityId item);

    // Sets what an ingredient costs: `amount` for each one `per`. Both empty clears the price. `per` must measure
    // the same thing as the ingredient's amount, or the engine refuses.
    void setPrice(EntityId recipe, IngredientKind kind, EntityId item, const std::optional<std::string>& amount,
                  const std::optional<std::string>& per);
    // Sets the currency the recipe's prices are in, a three-letter code.
    void setRecipeCurrency(EntityId recipe, const std::string& currency);

    // Stores the app's planning JSON for the recipe. The engine keeps it as it is.
    void setPlanning(EntityId recipe, const std::string& planning);
    // Sets the style a recipe is compared against, by id: a guideline id or a user style id, or nothing for none.
    void setStyle(EntityId recipe, const std::optional<std::string>& style);

    // The styles the picker offers: the BJCP styles in guideline order, then the user's styles.
    std::vector<StyleChoice> styleChoices() const;
    // The user's styles, in the order they were made.
    std::vector<styles::UserStyle> userStyles() const;
    // The user style with this id, if there is one.
    std::optional<styles::UserStyle> userStyle(const std::string& id) const;
    // Adds a user style and returns its new id. Throws EngineError if the style is not valid.
    std::string addUserStyle(const styles::UserStyle& style);
    // Replaces the user style with the same id. Throws EngineError if it is missing or not valid.
    void updateUserStyle(const styles::UserStyle& style);
    // Removes a user style. Recipes compared against it are left with no style.
    void removeUserStyle(const std::string& id);

    EntityId addFolder(const std::string& name);
    void renameFolder(EntityId folder, const std::string& name);
    // Removes a folder. Its recipes move to the top level. Undo puts them back.
    void removeFolder(EntityId folder);
    // Files a recipe in a folder, or at the top level when `folder` is empty.
    void moveRecipe(EntityId recipe, std::optional<EntityId> folder);

    // Calculations over a recipe.
    // A brew-day procedure, given its boil length in minutes and its mash system, "separate tun" or "all in one".
    std::vector<calc::ProcedureStep> procedure(EntityId recipe, int boilMinutes, const std::string& system) const;
    // The brewing water, mash, and sparge plan for the recipe.
    calc::BrewPlan brewingPlan(EntityId recipe, const calc::BrewPlanInputs& inputs) const;
    // The brew journal's expected and measured figures for the recipe.
    calc::JournalResults journalPlan(EntityId recipe, const calc::JournalInputs& measured) const;
    // The yeast plan for the recipe, sized from its batch and original gravity.
    calc::StarterPlan starterPlan(EntityId recipe, const StarterSettings& settings) const;
    // The recipe's values placed against its style, or nothing when it has no style.
    std::optional<StyleComparison> styleComparison(EntityId recipe) const;
    // The recipe as text in an export format: "beerxml", "beerjson", or "text".
    std::string exportRecipe(EntityId recipe, const std::string& format) const;
    // The recipe as a PDF file's bytes, for reading and printing. It is not the brew sheet.
    std::string exportRecipePdf(EntityId recipe) const;

private:
    RecipeDocument() = default;

    void execute(model::EditPtr edit);
    void refresh();
    const model::Recipe& require(EntityId recipe) const;
    std::string fingerprint(const model::Recipe& recipe) const;
    void rememberSaved();

    std::string id_;
    std::optional<std::filesystem::path> file_;
    std::string source_ = "current";
    model::Document document_;
    CommandStack<model::Document> stack_;
    // The undo depth where the open group began, or nothing when no group is open.
    std::optional<std::size_t> groupDepth_;
    unsigned long long revision_ = 0;
    mutable std::map<EntityId, calc::Results> results_;
    std::map<EntityId, std::string> current_;  // fingerprints of the recipes now
    std::map<EntityId, std::string> saved_;    // fingerprints as of the last save, none for a new document
};

// Every BJCP style the engine knows, in guideline order.
const std::vector<const calc::BjcpStyle*>& bjcpStyles();
// The name shown in the style picker, such as "21A American IPA".
std::string styleTitle(const calc::BjcpStyle& style);

// Runs a brewing tool or converter in the engine (calc/tools.hpp). The inputs are the text the user typed.
calc::tools::Output runTool(const std::string& tool, const std::map<std::string, std::string>& inputs);

}  // namespace kettlelogic::app
