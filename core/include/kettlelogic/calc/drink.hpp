#pragma once

#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/model/model.hpp"

// Plans the sugar for a cider, mead, or wine. It uses the cider, mead, and wine
// engines, with the recipe's own targets: its original gravity and its alcohol
// (BeerJSON's original_gravity and alcohol_by_volume).
namespace kettlelogic::calc {

// `fermentationEfficiency` is a fraction above 0 and up to 1. Throws
// std::invalid_argument if it is out of range. Missing targets and targets that
// cannot be reached leave their result empty, with a note.
DrinkPlan planDrink(const model::Recipe& recipe, const Results& results, double fermentationEfficiency);

}  // namespace kettlelogic::calc
