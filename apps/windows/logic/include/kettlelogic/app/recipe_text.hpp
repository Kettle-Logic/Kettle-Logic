#pragma once

#include "kettlelogic/app/engine.hpp"

#include <map>
#include <string>

// How a recipe's ingredients and results are written on screen: the one-line details of each ingredient, the
// prices, and the figures a calculator opened from a recipe starts with. These are the macOS app's
// `IngredientViews.swift` and `RecipeTools.swift`.
namespace kettlelogic::app {

// The alpha acid, the amount, and when it goes in, such as "5.5 % AA · 28 g · Boil 60 min".
std::string hopDetail(const model::Hop& hop);
// The amount, and the potential and color when they are set.
std::string fermentableDetail(const model::Fermentable& fermentable);
std::string cultureDetail(const model::Culture& culture);
// The minerals in mg/L, and the volume if there is one.
std::string waterDetail(const model::WaterAddition& water);

// What an ingredient row adds when the ingredient has a price: the price, and what the amount costs, such as
// " · $4.50 / kg · $2.05". Empty without a price.
std::string priceDetail(const model::Recipe& recipe, const calc::Results& results,
                        const std::optional<model::Price>& price, const Uuid& uuid);

// The value of a quantity in liters or grams, for the units the app offers, or nothing for any other unit.
std::optional<double> litersOf(const Quantity& quantity);
std::optional<double> gramsOf(const Quantity& quantity);

// The yeast starter calculator's fields, from the batch, the gravity, and the first culture.
std::map<std::string, std::string> starterPrefill(const model::Recipe& recipe, const calc::Results& results);
// The hop bitterness calculator's fields for one hop, from the hop, the volume, and the gravity.
std::map<std::string, std::string> bitternessPrefill(const model::Recipe& recipe, const calc::Results& results,
                                                     const model::Hop& hop);

// A batch size in liters, as the journal shows it. The recipe keeps the unit it was given in.
double batchLiters(const model::Recipe& recipe);

// The color of a beer for an SRM value, by interpolating between published reference swatches.
struct Rgb {
    int r = 0, g = 0, b = 0;
};
Rgb srmColor(double srm);

}  // namespace kettlelogic::app
