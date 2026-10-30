#pragma once

#include "kettlelogic/calc/water.hpp"
#include "kettlelogic/model/model.hpp"
#include "kettlelogic/uuid.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Recipe calculations: gravities, alcohol, bitterness, color, and mash
// strike temperatures.
//
// Inputs come from the model, exactly, and every calculation is done in
// floating point. Results are derived values for display. They are never
// written back into the model as if the user had entered them.
//
// The formulas and their sources are in docs/calc.md. Each result that cannot
// be computed is left empty, and the reason goes in `notes`, so a missing
// input shows as "no value" rather than as a wrong number.
namespace kettlelogic::calc {

// The sugar that brings a cider, mead, or wine to the recipe's own targets: its
// original gravity and alcohol. See docs/calc.md, "Cider, mead, and wine".
struct DrinkPlan {
    std::optional<double> gravityGrams;  // to reach the original gravity: sucrose for cider and wine, honey for mead
    std::optional<double> alcoholGrams;  // sucrose to reach the alcohol: cider and wine only
    std::vector<std::string> notes;
};

struct HopBitterness {
    Uuid hop;  // the hop addition
    double ibu = 0;
};

// What one priced ingredient costs in the recipe.
struct IngredientCost {
    Uuid ingredient;
    double cost = 0;
};

// The cost of the ingredients that have a price, in the recipe's currency. Ingredients without
// a price are left out of the total, and `notes` on the results says how many. See docs/calc.md,
// "Cost".
struct RecipeCost {
    std::string currency;
    double total = 0;
    std::vector<IngredientCost> byIngredient;
    std::optional<double> perLiter;    // of the batch size
    std::optional<double> perGallon;   // of the batch size, US gallons
    std::size_t unpriced = 0;          // ingredients with no price, or a price that does not fit their amount
};

struct StrikeWater {
    std::size_t stepIndex = 0;  // index into the mash's steps
    double celsius = 0;
};

struct Results {
    std::optional<double> originalGravity;  // specific gravity
    std::optional<double> finalGravity;     // specific gravity
    std::optional<double> abv;              // percent by volume
    std::optional<double> ibu;              // Tinseth
    std::vector<HopBitterness> ibuByHop;
    std::optional<double> srm;              // Morey
    std::optional<double> preBoilLiters;    // from the recipe's pre-boil size, or derived from the batch
    std::optional<double> postBoilLiters;   // the volume at the end of the boil; bitterness uses it
    WaterChemistry water;                   // without a target; see calculateWater for one with a target
    std::vector<StrikeWater> strikeWater;   // one per infusion step with a usable ratio
    std::optional<double> sulfiteSo2Ppm;    // SO2 from the sulfite additions, theoretical, mg/L
    std::optional<double> tartaricAcidGramsPerLiter;  // from the tartaric acid additions, g/L as tartaric acid
    std::optional<RecipeCost> cost;         // only when at least one ingredient has a price
    std::optional<DrinkPlan> drink;         // cider, mead, and wine only
    std::vector<std::string> notes;         // inputs that were missing or not supported
};

// True for the fermented drinks, which are planned from their sugar and not brewed.
bool isFermentedDrink(model::RecipeType type);

// The boil's volumes come from the recipe's boil-off settings (model::BoilOff).
Results calculate(const model::Recipe& recipe);

// Tinseth utilization: the fraction of a hop's alpha acid that is isomerized for a boil of `minutes`
// in wort of original gravity `og`. See docs/calc.md, "Formulas".
double tinsethUtilization(double og, double minutes);

}  // namespace kettlelogic::calc
