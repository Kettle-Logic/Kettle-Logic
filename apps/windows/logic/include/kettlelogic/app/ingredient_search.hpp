#pragma once

#include "kettlelogic/app/ingredient_draft.hpp"
#include "kettlelogic/catalog/ingredient_query.hpp"

#include <string>

// What the ingredient picker searches: each menu entry, described to the engine's query. The app's front ends
// share this, so they offer the same figures for sorting and filtering.
namespace kettlelogic::app {

// The kind's name as the engine's query takes it: "fermentable", "hop", "culture", "water", or "miscellaneous".
std::string searchKindName(IngredientKind kind);

// A menu entry as the engine's query sees it. `primary` and `secondary` are in the units that
// `catalog::ingredientMetrics` names: a fermentable's color in SRM and its yield as a gravity, a hop's alpha and
// beta acid, and a culture's attenuation. A color in SRM, EBC, or Lovibond is converted. Any other unit is left out,
// so a bound or a sort skips that entry.
catalog::IngredientEntry searchEntry(const MenuEntry& entry);

}  // namespace kettlelogic::app
