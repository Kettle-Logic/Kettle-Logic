#include "kettlelogic/calc/drink.hpp"

#include "kettlelogic/calc/cider.hpp"
#include "kettlelogic/calc/mead.hpp"
#include "kettlelogic/calc/wine.hpp"

#include <stdexcept>

namespace kettlelogic::calc {

DrinkPlan planDrink(const model::Recipe& recipe, const Results& results, double fermentationEfficiency) {
    DrinkPlan plan;
    if (!isFermentedDrink(recipe.type)) {
        plan.notes.push_back("the recipe is not a cider, mead, or wine, so there is nothing to plan");
        return plan;
    }
    if (!(fermentationEfficiency > 0 && fermentationEfficiency <= 1)) {
        throw std::invalid_argument("the fermentation efficiency must be above 0 and up to 1");
    }
    const double liters = recipe.batchSize.valueIn(Unit::Liter).toDouble();
    if (!(liters > 0)) {
        plan.notes.push_back("the batch size is not positive, so no sugar was planned");
        return plan;
    }
    const bool mead = recipe.type == model::RecipeType::Mead;

    // Gravity: the sugar that takes the drink from its gravity to the target.
    if (recipe.originalGravity) {
        const double target = recipe.originalGravity->value().toDouble();
        if (recipe.originalGravity->unit() != "sg") {
            plan.notes.push_back("the target gravity is not in specific gravity, so no sugar was planned for it");
        } else if (!results.originalGravity) {
            plan.notes.push_back("no original gravity was calculated, so no sugar was planned for it");
        } else if (target < *results.originalGravity) {
            plan.notes.push_back("the target gravity is below the drink's gravity; sugar cannot lower it");
        } else if (mead) {
            // Honey gives its points linearly, so the honey to add is the difference between two gravities.
            const double current = *results.originalGravity;
            const double before = current > 1.0 ? mead::honeyGramsForGravity(current, liters) : 0.0;
            plan.gravityGrams = mead::honeyGramsForGravity(target, liters) - before;
        } else {
            plan.gravityGrams = cider::sucroseGramsToReachGravity(*results.originalGravity, target, liters);
        }
    }

    // Alcohol: the sucrose that adds the missing alcohol. Mead is planned by gravity only.
    if (recipe.alcoholByVolume) {
        if (mead) {
            plan.notes.push_back("a mead's alcohol target is planned from its gravity target; no sugar was planned for alcohol");
        } else if (recipe.alcoholByVolume->dimension() != Dimension::Fraction) {
            plan.notes.push_back("the target alcohol is not a percentage, so no sugar was planned for it");
        } else if (!results.abv) {
            plan.notes.push_back("no alcohol was calculated, so no sugar was planned for it");
        } else {
            const double target = recipe.alcoholByVolume->valueIn(Unit::Percent).toDouble();
            if (target < *results.abv) {
                plan.notes.push_back("the target alcohol is below the drink's alcohol; sugar cannot lower it");
            } else {
                plan.alcoholGrams = wine::sucroseGramsForAbv(target - *results.abv, liters, fermentationEfficiency);
            }
        }
    }
    return plan;
}

}  // namespace kettlelogic::calc
