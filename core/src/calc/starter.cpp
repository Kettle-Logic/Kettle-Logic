#include "kettlelogic/calc/starter.hpp"

#include <cmath>
#include <stdexcept>

namespace kettlelogic::calc {

namespace {

constexpr double kLitersPerGallon = 3.785411784;
constexpr double kGramsPerPound = 453.59237;
constexpr double kDmePointsPerPoundGallon = 44;

// Degrees Plato from specific gravity, by the usual linear relation.
double plato(double specificGravity) {
    return 259.0 - 259.0 / specificGravity;
}

}  // namespace

StarterPlan planStarter(double batchLiters, double targetGravity, const StarterInputs& in) {
    if (!(batchLiters > 0)) {
        throw std::invalid_argument("the batch size must be more than zero");
    }
    if (!(targetGravity >= 1.0 && targetGravity < 1.2)) {
        throw std::invalid_argument("the original gravity must be from 1.000 to below 1.200");
    }
    if (!(in.packCellsBillion > 0) || !(in.growthBillionPerLiterPlato >= 0)) {
        throw std::invalid_argument("the pack size must be more than zero, and the growth cannot be negative");
    }
    if (!(in.starterGravity >= 1.0 && in.starterGravity < 1.1)) {
        throw std::invalid_argument("the starter gravity must be from 1.000 to below 1.100");
    }
    if (!(in.starterLiters >= 0) || !(in.firstStageLiters >= 0)) {
        throw std::invalid_argument("a starter volume cannot be negative");
    }

    StarterPlan plan;
    plan.targetGravity = targetGravity;
    plan.targetPlato = plato(targetGravity);
    plan.batchLiters = batchLiters;
    plan.pitchRate = in.lager ? 1.5 : 0.75;

    // Million cells per mL and degree Plato, times mL (1000 per liter), over a thousand: billions.
    plan.cellsNeededBillion = plan.pitchRate * batchLiters * plan.targetPlato;
    if (plan.cellsNeededBillion > 0) {
        plan.packsWithoutStarter = static_cast<int>(std::ceil(plan.cellsNeededBillion / in.packCellsBillion));
    }
    plan.cellsWithoutStarterBillion = plan.packsWithoutStarter * in.packCellsBillion;

    // The liquid starter the inputs describe, grown from one pack.
    const double starterPlato = plato(in.starterGravity);
    const double growthPerLiter = in.growthBillionPerLiterPlato * starterPlato;
    plan.starterLiters = in.starterLiters;
    plan.starterGravity = in.starterGravity;
    const double gallons = in.starterLiters / kLitersPerGallon;
    const double pounds = (in.starterGravity - 1.0) * 1000.0 * gallons / kDmePointsPerPoundGallon;
    plan.dryMaltOunces = pounds * 16.0;
    plan.dryMaltGrams = pounds * kGramsPerPound;
    plan.cellsWithStarterBillion = in.packCellsBillion + growthPerLiter * in.starterLiters;
    plan.addToBottlingLiters = in.addToBottling ? in.starterLiters : 0;

    // One stage: the pack alone grows to the target, so the starter is what the pack lacks.
    const double missing = plan.cellsNeededBillion - in.packCellsBillion;
    if (growthPerLiter > 0) {
        plan.oneStageLiters = missing > 0 ? missing / growthPerLiter : 0;
    }

    // Two stages: the first step from the pack, the second step from the first step's cells.
    plan.twoStageFirstLiters = in.firstStageLiters;
    const double firstCells = in.packCellsBillion + growthPerLiter * in.firstStageLiters;
    const double stillMissing = plan.cellsNeededBillion - firstCells;
    if (growthPerLiter > 0) {
        plan.twoStageSecondLiters = stillMissing > 0 ? stillMissing / growthPerLiter : 0;
    }

    if (plan.cellsNeededBillion <= 0) {
        plan.notes.push_back("The batch needs no yeast yet: check the batch size and original gravity.");
    }
    if (plan.oneStageLiters > 4) {
        plan.notes.push_back("A one-stage starter over 4 L is large for one flask. Consider the two-stage plan.");
    }
    if (plan.twoStageSecondLiters > 4) {
        plan.notes.push_back("The second stage is over 4 L. Use a larger first stage or a bigger vessel.");
    }
    if (in.growthBillionPerLiterPlato == 15) {
        plan.notes.push_back("Starter growth is a planning estimate (15 billion cells per liter per degree Plato). "
                             "Measure your own yeast's growth before relying on it.");
    }
    return plan;
}

}  // namespace kettlelogic::calc
