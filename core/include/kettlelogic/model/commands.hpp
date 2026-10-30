#pragma once

#include "kettlelogic/command.hpp"
#include "kettlelogic/model/model.hpp"

#include <memory>
#include <string>

// Edits to a model::Document, run through CommandStack<Document>. Each command
// refers to entities by EntityId, captures what it needs to undo at apply
// time, and throws std::invalid_argument from apply() if its target is
// missing. CommandStack does not record a command that throws.
namespace kettlelogic::model {

using Edit = Command<Document>;
using EditPtr = std::unique_ptr<Edit>;

// Adds a recipe to the document. Assigns a fresh id and UUID if they are unset.
EditPtr addRecipe(Recipe recipe);
// Removes a recipe from the document for good. Undo puts it back at its old place.
EditPtr removeRecipe(EntityId recipe);
// Moves a recipe to the Recently Deleted folder, and makes that folder if the document has none.
// Undo files the recipe where it was, and removes the folder if the edit made it. Throws if the
// recipe is missing or is already deleted.
EditPtr trashRecipe(EntityId recipe);

// Copies a recipe, and places the copy after it. The copy is named after the original
// with " copy" added, and it and each of its ingredients get fresh ids and UUIDs. Its
// ids are assigned on the first apply, so redo restores the same entities. Throws if
// the recipe is missing.
EditPtr duplicateRecipe(EntityId recipe);

// Copies an ingredient within its recipe, and places the copy after it, with a fresh id
// and UUID. Throws if the recipe or the ingredient is missing.
EditPtr duplicateFermentable(EntityId recipe, EntityId fermentable);
EditPtr duplicateHop(EntityId recipe, EntityId hop);
EditPtr duplicateCulture(EntityId recipe, EntityId culture);
EditPtr duplicateWater(EntityId recipe, EntityId water);
EditPtr duplicateMiscellaneous(EntityId recipe, EntityId miscellaneous);

// Replaces an item in its list, keeping its position and identity. The new item
// must pass the same rules as its add command, and the item must exist.
EditPtr setFermentable(EntityId recipe, Fermentable fermentable);
EditPtr setHop(EntityId recipe, Hop hop);
EditPtr setCulture(EntityId recipe, Culture culture);
EditPtr setWater(EntityId recipe, WaterAddition water);
EditPtr setMiscellaneous(EntityId recipe, Miscellaneous miscellaneous);

EditPtr setRecipeName(EntityId recipe, std::string name);  // merges with repeated renames
EditPtr setBatchSize(EntityId recipe, Quantity batchSize);
// Assigns a fresh id if `fermentable.id` is 0. The id is assigned on the first
// apply, so redo restores the same entity. Throws if its type is not a
// BeerJSON fermentable type, or if its amount is not a mass or volume.
EditPtr addFermentable(EntityId recipe, Fermentable fermentable);
EditPtr removeFermentable(EntityId recipe, EntityId fermentable);

// The optional ingredient lists. Fermentables are required, so they are not listed.
enum class IngredientList { Hops, Miscellaneous, Cultures, Waters };

// Presence is part of the document: present-but-empty is written as [].
// Removing a list is refused while it still has items, so no ingredients are
// lost by accident.
EditPtr setIngredientListPresent(EntityId recipe, IngredientList list, bool present);

// The same rule for the boil steps. Throws if the recipe has no boil procedure.
EditPtr setBoilStepsPresent(EntityId recipe, bool present);

// Adds a hop addition, and creates the list if the recipe has none. Assigns a
// fresh id and UUID if they are unset, on the first apply, so redo restores the
// same entity. Throws unless the alpha and beta acids are percentages, the
// amount is a mass or volume, the form and timing use are BeerJSON values, and
// any timing time or duration is a whole number of its unit.
EditPtr addHop(EntityId recipe, Hop hop);
EditPtr removeHop(EntityId recipe, EntityId hop);

// Adds a culture (yeast, bacteria, and so on), with the same rules for the list
// and ids. Throws unless the type and form are BeerJSON values, the amount is a
// mass, volume, or count, the attenuation is a percentage, and any timing is valid.
EditPtr addCulture(EntityId recipe, Culture culture);
EditPtr removeCulture(EntityId recipe, EntityId culture);

// Adds a water addition, with the same rules for the list and ids. Throws unless
// each mineral is a concentration and the amount, if any, is a volume.
EditPtr addWater(EntityId recipe, WaterAddition water);
EditPtr removeWater(EntityId recipe, EntityId water);

// Sets which salt a miscellaneous ingredient is, or none. Throws if the ingredient is missing.
EditPtr setSaltType(EntityId recipe, EntityId miscellaneous, std::optional<SaltKind> salt);

// The ingredients that can carry a price. Water is not bought by the recipe, so it has none.
enum class IngredientKind { Fermentable, Hop, Culture, Miscellaneous };

// Sets what an ingredient costs, or clears it with nullopt. Throws if the ingredient is
// missing, the price is negative, or `per` is not a mass, volume, or count unit of the same
// dimension as the ingredient's amount.
EditPtr setPrice(EntityId recipe, IngredientKind kind, EntityId ingredient, std::optional<Price> price);

// Sets the currency the recipe's prices are in, as a three-letter ISO 4217 code. The code is
// stored in upper case, and prices are not converted. Throws unless it is three ASCII letters.
EditPtr setCurrency(EntityId recipe, std::string currency);

// Sets the kind of beverage the recipe makes.
EditPtr setRecipeType(EntityId recipe, RecipeType type);

// Adds a miscellaneous ingredient, and creates the list if the recipe has none.
// Assigns a fresh id and UUID if they are unset. Throws if its type is not a
// BeerJSON miscellaneous type, or if its amount is not a mass, volume, or count.
EditPtr addMiscellaneous(EntityId recipe, Miscellaneous miscellaneous);
EditPtr removeMiscellaneous(EntityId recipe, EntityId miscellaneous);

// Sets what a miscellaneous ingredient does in a drink, or none. A sulfite must
// be a mass. Throws if the ingredient is missing.
EditPtr setAdditive(EntityId recipe, EntityId miscellaneous, std::optional<Additive> additive);

// Sets the recipe's fermentation efficiency, which must be above 0 % and up to 100 %.
EditPtr setFermentationEfficiency(EntityId recipe, Quantity efficiency);

// Sets the recipe's boil-off. Throws unless the evaporation rate is from 0 % up to,
// not including, 100 %, and the trub loss is zero or more.
EditPtr setBoilOff(EntityId recipe, BoilOff boilOff);

// Sets the brewhouse efficiency, which must be above 0 % and up to 100 %.
EditPtr setBrewhouseEfficiency(EntityId recipe, Quantity efficiency);

// The batch size, brewhouse efficiency, and boil-off of an equipment profile, as one
// undo step. Refused as a whole if any part is.
EditPtr applyEquipment(EntityId recipe, Quantity batchSize, Quantity efficiency, BoilOff boilOff);

// Sets the app's planning JSON for a recipe. The engine stores the text as it is.
EditPtr setPlanning(EntityId recipe, std::string planning);

// Sets the BJCP style a recipe is compared against, by guideline id, or none. The
// id is checked by the caller, against the style table.
EditPtr setBjcpStyle(EntityId recipe, std::optional<std::string> style);

// Adds a user style. Assigns a fresh id if it has none, on the first apply. Throws if the style is
// not valid, if its id does not begin with "custom-", or if another style already has the id.
EditPtr addUserStyle(styles::UserStyle style);
// Replaces a user style with the same id, keeping its place. Throws if it is missing or not valid.
EditPtr setUserStyle(styles::UserStyle style);
// Removes a user style. Recipes compared against it are left with no style. Undo restores both.
EditPtr removeUserStyle(std::string id);

// Adds a folder. Assigns a fresh id and UUID if they are unset, on the first apply.
// Throws if the name is blank.
EditPtr addFolder(Folder folder);
// Renames a folder. Throws if the folder is missing, the name is blank, or the folder is Recently Deleted.
EditPtr renameFolder(EntityId folder, std::string name);
// Removes a folder. Its recipes move to the top level. Undo puts them back in it.
// Throws if the folder is Recently Deleted.
EditPtr removeFolder(EntityId folder);
// Files a recipe in a folder, or at the top level when `folder` is nullopt.
EditPtr moveRecipeToFolder(EntityId recipe, std::optional<EntityId> folder);

}  // namespace kettlelogic::model
