#include "kettlelogic/calc/recipe.hpp"

#include "kettlelogic/calc/drink.hpp"
#include "kettlelogic/calc/fermented.hpp"
#include "kettlelogic/measure.hpp"
#include "kettlelogic/quantity.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace kettlelogic::calc {

// Cider, mead, and wine: their ingredients are fully dissolved, so brewhouse
// efficiency does not apply, and they have no boil procedure of their own.
bool isFermentedDrink(model::RecipeType type) {
    return type == model::RecipeType::Cider || type == model::RecipeType::Mead || type == model::RecipeType::Wine;
}

namespace {

// Sucrose yields 46.214 points per pound per gallon (37 points is 80 %).
constexpr double kSucrosePointsPerPound = 46.214;

// Exact definitions used by the conversions below.
constexpr double kLitersPerGallon = 3.785411784;
constexpr double kKilogramsPerPound = 0.45359237;
constexpr double kLitersPerQuart = 0.946352946;
constexpr double kLitersPerFluidOunce = 0.0295735295625;
constexpr double kKilogramsPerOunce = 0.45359237 / 16.0;

double gallons(const Quantity& volume) {
    return volume.valueIn(Unit::Gallon).toDouble();
}

double liters(const Quantity& volume) {
    return volume.valueIn(Unit::Liter).toDouble();
}

double pounds(const Quantity& mass) {
    return mass.valueIn(Unit::Pound).toDouble();
}

double grams(const Quantity& mass) {
    return mass.valueIn(Unit::Gram).toDouble();
}

double celsius(const Quantity& temperature) {
    return temperature.valueIn(Unit::Celsius).toDouble();
}

// Converts a gravity to specific gravity. Plato and brix use the standard
// Balling relation, which is accurate for brewing gravities.
std::optional<double> specificGravity(const Measure& gravity) {
    const double value = gravity.value().toDouble();
    if (gravity.unit() == "sg") {
        return value;
    }
    if (gravity.unit() == "plato" || gravity.unit() == "brix") {
        return 1.0 + value / (258.6 - (value / 258.2) * 227.1);
    }
    return std::nullopt;
}

// Points per pound per gallon from a fermentable's yield, or nullopt if it has none.
std::optional<double> pointsPerPound(const model::Fermentable& fermentable, std::string& note) {
    if (fermentable.yield.potential) {
        if (const auto sg = specificGravity(*fermentable.yield.potential)) {
            return (*sg - 1.0) * 1000.0;
        }
    }
    if (fermentable.yield.fineGrind) {
        return fermentable.yield.fineGrind->valueIn(Unit::Percent).toDouble() * kSucrosePointsPerPound / 100.0;
    }
    if (fermentable.yield.coarseGrind) {
        note = "\"" + fermentable.name + "\": only a coarse-grind yield is given; it was used as the fine-grind yield";
        return fermentable.yield.coarseGrind->valueIn(Unit::Percent).toDouble() * kSucrosePointsPerPound / 100.0;
    }
    return std::nullopt;
}

// Lovibond from a color measure. SRM is converted by SRM = 1.3 * L - 0.6 (inverted),
// and EBC is converted to SRM by SRM = EBC / 1.97.
std::optional<double> lovibond(const Measure& color) {
    const double value = color.value().toDouble();
    if (color.unit() == "Lovi") {
        return value;
    }
    const double srm = color.unit() == "SRM" ? value : color.unit() == "EBC" ? value / 1.97 : -1.0;
    if (srm < 0) {
        return std::nullopt;
    }
    return (srm + 0.6) / 1.3;
}

// A specific volume (water to grain) converted to quarts per pound, or nullopt
// for a unit this calculation does not know.
std::optional<double> quartsPerPound(const Measure& ratio) {
    const double q = ratio.value().toDouble();
    const std::string_view unit = ratio.unit();
    if (unit == "qt/lb") {
        return q;
    }
    // Everything else goes through liters per kilogram.
    double litersPerKilogram = 0;
    if (unit == "l/kg") {
        litersPerKilogram = q;
    } else if (unit == "l/g") {
        litersPerKilogram = q * 1000.0;
    } else if (unit == "m^3/kg") {
        litersPerKilogram = q * 1000.0;
    } else if (unit == "gal/lb") {
        litersPerKilogram = q * kLitersPerGallon / kKilogramsPerPound;
    } else if (unit == "gal/oz") {
        litersPerKilogram = q * kLitersPerGallon / kKilogramsPerOunce;
    } else if (unit == "floz/oz") {
        litersPerKilogram = q * kLitersPerFluidOunce / kKilogramsPerOunce;
    } else if (unit == "ft^3/lb") {
        litersPerKilogram = q * 28.316846592 / kKilogramsPerPound;
    } else {
        return std::nullopt;
    }
    return litersPerKilogram * kKilogramsPerPound / kLitersPerQuart;
}

// The recipe's hop additions, or none if the list is absent.
const std::vector<model::Hop>& hopsOf(const model::Recipe& recipe) {
    static const std::vector<model::Hop> none;
    return recipe.hops ? *recipe.hops : none;
}

// The boil's volumes. A stated pre-boil size is used directly. Otherwise the
// pre-boil volume is worked back from the batch: the post-boil volume is the
// batch plus trub loss, and the pre-boil volume is that divided by what the
// boil keeps.
void boilVolumesOf(const model::Recipe& recipe, Results& results) {
    if (isFermentedDrink(recipe.type)) {
        return;
    }
    double minutes = 60;
    if (recipe.boil) {
        minutes = recipe.boil->boilTime.valueIn(Unit::Minute).toDouble();
    } else {
        results.notes.push_back("the recipe has no boil procedure; a 60-minute boil was assumed");
    }
    const double evaporationPerHour = recipe.boilOff.evaporationRate.valueIn(Unit::Percent).toDouble() / 100.0;
    const double trubLossLiters = liters(recipe.boilOff.trubLoss);
    const double keep = 1.0 - evaporationPerHour * minutes / 60.0;
    if (keep <= 0) {
        results.notes.push_back("the boil would evaporate all the wort, so the volumes were not calculated");
        return;
    }

    if (recipe.boil && recipe.boil->preBoilSize) {
        const double pre = liters(*recipe.boil->preBoilSize);
        results.preBoilLiters = pre;
        results.postBoilLiters = pre * keep;
    } else {
        const double post = liters(recipe.batchSize) + trubLossLiters;
        results.postBoilLiters = post;
        results.preBoilLiters = post / keep;
    }
}

// Original gravity: the sum of each fermentable's points, scaled by brewhouse efficiency.
std::optional<double> originalGravityOf(const model::Recipe& recipe, std::vector<std::string>& notes) {
    const double batch = gallons(recipe.batchSize);
    if (batch <= 0) {
        notes.push_back("the batch size is not positive, so gravity cannot be calculated");
        return std::nullopt;
    }
    const double efficiency =
        isFermentedDrink(recipe.type) ? 1.0 : recipe.efficiency.brewhouse.valueIn(Unit::Percent).toDouble() / 100.0;

    double points = 0;
    for (const model::Fermentable& f : recipe.fermentables) {
        std::string note;
        if (f.amount.dimension() == Dimension::Volume) {
            // A liquid, such as juice, gives its gravity points per gallon.
            const auto sg = f.yield.potential ? specificGravity(*f.yield.potential) : std::nullopt;
            if (!sg) {
                notes.push_back("\"" + f.name + "\" is measured by volume and needs a gravity yield, so it adds no gravity");
                continue;
            }
            points += (*sg - 1.0) * 1000.0 * gallons(f.amount);
            continue;
        }
        const auto ppg = pointsPerPound(f, note);
        if (!note.empty()) {
            notes.push_back(note);
        }
        if (!ppg) {
            notes.push_back("\"" + f.name + "\" has no usable yield, so it adds no gravity");
            continue;
        }
        points += pounds(f.amount) * *ppg;
    }
    return 1.0 + points * efficiency / (batch * 1000.0);
}

std::optional<double> finalGravityOf(const model::Recipe& recipe, double og, std::vector<std::string>& notes) {
    if (recipe.finalGravity) {
        if (const auto sg = specificGravity(*recipe.finalGravity)) {
            return sg;
        }
        notes.push_back("the final gravity unit is not supported");
    }
    // Attenuation: the first culture that gives one, else the recipe's own figure.
    static const std::vector<model::Culture> noCultures;
    for (const model::Culture& culture : recipe.cultures ? *recipe.cultures : noCultures) {
        if (culture.attenuation) {
            return og - (og - 1.0) * culture.attenuation->valueIn(Unit::Percent).toDouble() / 100.0;
        }
    }
    if (recipe.apparentAttenuation) {
        return og - (og - 1.0) * recipe.apparentAttenuation->valueIn(Unit::Percent).toDouble() / 100.0;
    }
    notes.push_back(
        "final gravity and ABV were not calculated, because no culture has an attenuation and the recipe has no "
        "final gravity or apparent attenuation. Add an attenuation to a culture to calculate them");
    return std::nullopt;
}

// Tinseth bitterness of each boil hop addition. Boil time is the addition's
// time in minutes. Only additions made to the boil contribute; others add 0.
// The wort volume is the post-boil volume from the boil model.
void bitternessOf(const model::Recipe& recipe, double og, Results& results) {
    const double volume = results.postBoilLiters.value_or(liters(recipe.batchSize));
    if (volume <= 0) {
        return;
    }
    double total = 0;
    for (const model::Hop& hop : hopsOf(recipe)) {
        if (hop.timing.use != "add_to_boil" || !hop.timing.time) {
            results.ibuByHop.push_back(HopBitterness{hop.uuid, 0});
            continue;
        }
        if (hop.amount.dimension() != Dimension::Mass) {
            results.notes.push_back("\"" + hop.name + "\" is measured by volume, so its bitterness was not calculated");
            results.ibuByHop.push_back(HopBitterness{hop.uuid, 0});
            continue;
        }
        const double minutes = hop.timing.time->valueIn(Unit::Minute).toDouble();
        const double alpha = hop.alphaAcid.valueIn(Unit::Percent).toDouble() / 100.0;
        const double utilization = tinsethUtilization(og, minutes);
        const double ibu = utilization * alpha * grams(hop.amount) * 1000.0 / volume;
        results.ibuByHop.push_back(HopBitterness{hop.uuid, ibu});
        total += ibu;
    }
    results.ibu = total;
}

// Morey color from the fermentables' malt color units.
void colorOf(const model::Recipe& recipe, Results& results) {
    const double batch = gallons(recipe.batchSize);
    if (batch <= 0) {
        return;
    }
    double mcu = 0;
    for (const model::Fermentable& f : recipe.fermentables) {
        const auto lovi = lovibond(f.color);
        if (!lovi) {
            results.notes.push_back("\"" + f.name + "\" has a color unit that is not supported; it was left out of color");
            continue;
        }
        if (f.amount.dimension() == Dimension::Volume) {
            // Malt color is per pound, so a liquid with a color is not calculated.
            if (*lovi > 0) {
                results.notes.push_back("\"" + f.name + "\" is measured by volume, so its color was not calculated");
            }
            continue;
        }
        mcu += pounds(f.amount) * *lovi / batch;
    }
    results.srm = 1.4922 * std::pow(mcu, 0.6859);
}

// The sulfite and tartaric acid added to the batch, from the additive roles.
void additivesOf(const model::Recipe& recipe, Results& results) {
    if (!recipe.miscellaneous) {
        return;
    }
    const double batch = liters(recipe.batchSize);
    if (batch <= 0) {
        return;
    }
    double sulfiteGrams = 0;
    double acidGrams = 0;
    bool anySulfite = false;
    bool anyAcid = false;
    for (const model::Miscellaneous& misc : *recipe.miscellaneous) {
        if (!misc.additive) {
            continue;
        }
        if (misc.amount.dimension() != Dimension::Mass) {
            results.notes.push_back("\"" + misc.name + "\" is measured by volume, so its additive was not calculated");
            continue;
        }
        if (*misc.additive == model::Additive::Sulfite) {
            anySulfite = true;
            sulfiteGrams += grams(misc.amount);
        } else {
            anyAcid = true;
            acidGrams += grams(misc.amount);
        }
    }
    // Potassium metabisulfite is 57.6 % SO2, so each g/L of it gives 576 mg/L of SO2.
    if (anySulfite) {
        results.sulfiteSo2Ppm = sulfiteGrams / batch * fermented::kSo2MgPerLiterPerMetabisulfiteGramPerLiter;
    }
    if (anyAcid) {
        results.tartaricAcidGramsPerLiter = acidGrams / batch;
    }
}

void strikeWaterOf(const model::Recipe& recipe, Results& results) {
    if (!recipe.mash) {
        return;
    }
    const model::Mash& mash = *recipe.mash;
    const double grainTemperature = celsius(mash.grainTemperature);
    for (std::size_t i = 0; i < mash.steps.size(); ++i) {
        const model::MashStep& step = mash.steps[i];
        if (step.type != "infusion") {
            continue;
        }
        if (!step.waterGrainRatio) {
            results.notes.push_back("mash step \"" + step.name + "\" has no water-to-grain ratio, so no strike temperature was calculated");
            continue;
        }
        const auto ratio = quartsPerPound(*step.waterGrainRatio);
        if (!ratio || *ratio <= 0) {
            results.notes.push_back("mash step \"" + step.name + "\" has a water-to-grain unit that is not supported");
            continue;
        }
        // Thermal balance: strike = target + (0.2 / ratio) * (target - grain).
        const double target = celsius(step.stepTemperature);
        results.strikeWater.push_back(StrikeWater{i, target + (0.2 / *ratio) * (target - grainTemperature)});
    }
}

// The cost of the priced ingredients. Nothing is reported when no ingredient has a price, so
// a recipe that is not costed shows no cost at all.
template <typename Item>
void addCosts(const std::vector<Item>& items, RecipeCost& cost, bool& anyPrice) {
    for (const Item& item : items) {
        if (!item.price) {
            ++cost.unpriced;
            continue;
        }
        anyPrice = true;
        if (item.amount.dimension() != dimensionOf(item.price->per)) {
            // The amount was changed to something the price cannot measure, such as grams for a price per liter.
            ++cost.unpriced;
            continue;
        }
        const double line = item.price->amount.toDouble() * item.amount.valueIn(item.price->per).toDouble();
        cost.total += line;
        cost.byIngredient.push_back(IngredientCost{item.uuid, line});
    }
}

void costOf(const model::Recipe& recipe, Results& results) {
    RecipeCost cost;
    cost.currency = recipe.currency;
    bool anyPrice = false;
    addCosts(recipe.fermentables, cost, anyPrice);
    if (recipe.hops) {
        addCosts(*recipe.hops, cost, anyPrice);
    }
    if (recipe.cultures) {
        addCosts(*recipe.cultures, cost, anyPrice);
    }
    if (recipe.miscellaneous) {
        addCosts(*recipe.miscellaneous, cost, anyPrice);
    }
    if (!anyPrice) {
        return;
    }
    const double batch = liters(recipe.batchSize);
    if (batch > 0) {
        cost.perLiter = cost.total / batch;
        cost.perGallon = cost.total / (batch / kLitersPerGallon);
    }
    if (cost.unpriced > 0) {
        results.notes.push_back(std::to_string(cost.unpriced) +
                                (cost.unpriced == 1 ? " ingredient has" : " ingredients have") +
                                " no usable price, so the cost leaves " + (cost.unpriced == 1 ? "it" : "them") + " out");
    }
    results.cost = std::move(cost);
}

}  // namespace

double tinsethUtilization(double og, double minutes) {
    return 1.65 * std::pow(0.000125, og - 1.0) * (1.0 - std::exp(-0.04 * minutes)) / 4.15;
}

Results calculate(const model::Recipe& recipe) {
    Results results;
    boilVolumesOf(recipe, results);
    results.water = calculateWater(recipe);

    const auto og = originalGravityOf(recipe, results.notes);
    if (og) {
        results.originalGravity = og;
        results.finalGravity = finalGravityOf(recipe, *og, results.notes);
        if (results.finalGravity) {
            const double fg = *results.finalGravity;
            if (!isFermentedDrink(recipe.type)) {
                results.abv = (*og - fg) * 131.25;
            } else if (*og > fg && *og < 1.775 && fg > 0) {
                // Drinks have higher gravities than beer, so they use the advanced formula.
                results.abv = fermented::abvFromGravities(*og, fg);
            } else {
                results.notes.push_back("the gravities do not give an alcohol, so ABV was not calculated");
            }
        }
        bitternessOf(recipe, *og, results);
    } else {
        // Without an original gravity, bitterness cannot use its utilization term.
        for (const model::Hop& hop : hopsOf(recipe)) {
            results.ibuByHop.push_back(HopBitterness{hop.uuid, 0});
        }
        results.notes.push_back("bitterness needs an original gravity, so it was not calculated");
    }

    colorOf(recipe, results);
    additivesOf(recipe, results);
    if (isFermentedDrink(recipe.type)) {
        results.drink = planDrink(recipe, results, recipe.fermentationEfficiency.valueIn(Unit::Percent).toDouble() / 100.0);
    }
    strikeWaterOf(recipe, results);
    costOf(recipe, results);
    return results;
}

}  // namespace kettlelogic::calc
