#pragma once

#include "kettlelogic/app/preferences.hpp"
#include "kettlelogic/catalog/recipe_templates.hpp"

#include <string>
#include <vector>

// Starter recipes for the new-recipe wizard live in the engine (`catalog/recipe_templates.hpp`), so every
// front end offers the same ones.
namespace kettlelogic::app {

using catalog::RecipeTemplate;
using catalog::TemplateAromaHop;
using catalog::TemplateBoilHop;
using catalog::TemplateGrain;
using catalog::TempRange;

// The template's tips, with each `{n}` temperature in °C written in the user's unit.
std::vector<std::string> tipsIn(const RecipeTemplate& recipe, UnitSystem units);

}  // namespace kettlelogic::app
