#pragma once

#include "kettlelogic/app/engine.hpp"
#include "kettlelogic/app/equipment.hpp"
#include "kettlelogic/app/templates.hpp"

#include <string>

namespace kettlelogic::app {

// What the new-recipe wizard has asked for.
struct RecipeBuildRequest {
    RecipeTemplate recipeTemplate;
    std::string batchValue;  // the batch size as typed, in the units' volume unit
    EquipmentProfile profile;
    UnitSystem units = UnitSystem::metric;

    // The batch in liters, or nothing when the text is not a positive number.
    std::optional<double> batchLiters() const;
};

// Builds a recipe from a template. The amounts are not stored in the template. They are worked out from the engine's
// own results: the grain is scaled until the original gravity reaches the target, and each boil hop until its share
// of the target bitterness is reached. Everything is one undo step.
namespace RecipeBuilder {

// Adds a new recipe to `document`, built from the request, and returns its id.
EntityId add(const RecipeBuildRequest& request, RecipeDocument& document);

// Builds the request into a recipe that already exists, such as a new document's first recipe.
void populate(const RecipeBuildRequest& request, RecipeDocument& document, EntityId recipe);

}  // namespace RecipeBuilder
}  // namespace kettlelogic::app
