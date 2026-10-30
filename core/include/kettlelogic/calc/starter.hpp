#pragma once

#include <string>
#include <vector>

// Yeast cell counts and liquid yeast starters. A pitch is sized from the batch
// and its original gravity, as cells per milliliter per degree Plato. The
// starter's growth is a rule of thumb the user can change, so the result is an
// estimate for planning, not a measurement.
//
// The pitch rates are Mr. Malty's: about 0.75 million cells per mL per degree
// Plato for ales and 1.5 for lagers. Dry malt is taken as 44 points per pound per
// gallon, and a pack as 100 billion cells unless the user says otherwise.
namespace kettlelogic::calc {

struct StarterInputs {
    bool lager = false;
    double packCellsBillion = 100;             // cells in one liquid pack
    double growthBillionPerLiterPlato = 15;    // cells a liter of starter adds, per degree Plato
    double starterLiters = 2;                  // the one-stage or second-stage starter
    double starterGravity = 1.040;             // specific gravity of the starter wort
    bool twoStage = false;                     // whether the two-stage plan is used
    double firstStageLiters = 1;               // the first stage of a two-stage starter
    bool addToBottling = false;                // the starter is poured in with the beer, not decanted
};

struct StarterPlan {
    double targetGravity = 0;          // the recipe's original gravity
    double targetPlato = 0;
    double batchLiters = 0;
    double pitchRate = 0;              // million cells per mL per degree Plato
    double cellsNeededBillion = 0;
    int packsWithoutStarter = 0;
    double cellsWithoutStarterBillion = 0;

    // The liquid starter the inputs describe.
    double starterLiters = 0;
    double starterGravity = 0;
    double dryMaltGrams = 0;
    double dryMaltOunces = 0;
    double cellsWithStarterBillion = 0;
    double addToBottlingLiters = 0;

    // Planned starter volumes. The one-stage size is for one step from the pack. The
    // two-stage sizes are the first step and the second step, which takes the rest.
    double oneStageLiters = 0;
    double twoStageFirstLiters = 0;
    double twoStageSecondLiters = 0;

    std::vector<std::string> notes;
};

// Plans the yeast for a batch of `batchLiters` at `targetGravity` (specific gravity).
// Throws std::invalid_argument for values that cannot be planned.
StarterPlan planStarter(double batchLiters, double targetGravity, const StarterInputs& inputs);

}  // namespace kettlelogic::calc
