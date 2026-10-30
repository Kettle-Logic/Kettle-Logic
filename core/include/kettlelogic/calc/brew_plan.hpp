#pragma once

#include "kettlelogic/calc/water.hpp"
#include "kettlelogic/model/model.hpp"

#include <optional>
#include <string>
#include <vector>

// Brewing water and mash planning: the salts and acids added to the brewing water,
// the water's ions and analysis, the mash and sparge volumes and temperatures, and the
// mash pH and sparge acid from the brew water calculations (see brew_water.hpp).
//
// The sources of each constant are in docs/calc.md, "Water and mash planning" and "brew water".
namespace kettlelogic::calc {

// A water salt by name: "gypsum", "calcium chloride", "epsom salt", "baking soda", or "table salt".
// `sparge` puts it in the sparge water, not the mash.
struct WaterSalt {
    std::string kind;
    double grams = 0;
    bool sparge = false;
};

// An acid by name: "lactic acid", "phosphoric acid", "acid blend", "malic acid", or "ascorbic acid".
struct AcidAddition {
    std::string kind;
    double grams = 0;
};

struct BrewPlanInputs {
    Ions baseWater;                       // the brewing water before any salts, in mg/L
    double mashRatioLPerKg = 3.0;         // strike water per kilogram of grain
    double absorptionLPerKg = 1.0;        // water the grain holds, per kilogram
    double grainTempC = 20;
    double tunTempC = 20;
    double mashTempC = 67;
    double spargeTempC = 76;
    double deadspaceLiters = 0.5;         // water left in the tun below the outlet
    bool adjustForDeadspace = true;
    bool leaveOutSalts = false;           // use the base water as it is
    bool holdSpargeSaltsUntilBoil = false;  // sparge salts go in the kettle instead
    std::vector<WaterSalt> salts;
    std::vector<AcidAddition> acids;
    std::string acidForTarget = "lactic acid";
    double targetMashPh = 5.4;            // the mash pH the acid works toward
    std::string spargeAcid = "phosphoric acid";
    double spargeAcidStrengthPercent = 85;
    double spargeStartPh = 7.0;           // the sparge water's pH before the acid
    double roPercent = 0;                 // the share of RO water mixed into the base water, percent
    Ions roWater;                         // the RO water's ions, in mg/L
    std::optional<Ions> targetWater;      // the desired mash water, for the difference from the mash water
    double spargeTargetPh = 5.6;          // the pH the sparge water is acidified to
};

// The analysis of one water: alkalinity and hardness as CaCO3, the Kolbach residual alkalinity,
// the sulfate to chloride ratio, and a word for the balance between them.
struct WaterAnalysis {
    Ions ions;
    double alkalinity = 0;                       // mg/L as CaCO3
    double effectiveHardness = 0;                // calcium and magnesium, mg/L as CaCO3
    double residualAlkalinity = 0;               // Kolbach, mg/L as CaCO3
    std::optional<double> sulfateToChloride;     // nil without chloride
    std::string balance;                         // "malt-forward", "balanced", or "hop-forward"
};

struct BrewPlan {
    double grainKg = 0;
    double strikeLiters = 0;                     // the water added to the mash tun
    double absorptionLiters = 0;                 // the water the grain holds
    double firstRunningsLiters = 0;              // the wort drained before sparging
    std::optional<double> spargeLiters;          // from the pre-boil volume, when the recipe gives one
    double mashVolumeLiters = 0;                 // the volume the mash needs in the tun
    double mashTunVolumeLiters = 0;              // the tun size the mash needs
    std::optional<double> strikeTempC;           // the strike water temperature for the mash
    WaterAnalysis mashWater;                     // the water the mash uses
    WaterAnalysis spargeWater;                   // the water the sparge uses
    std::optional<Ions> mashDifference;          // the target water less the mash water, when a target is given
    double maltAcidityMilliequivalents = 0;      // the grist's acidity
    double quartsPerPound = 0;                   // the mash's water-to-grist ratio
    double acidMilliequivalents = 0;             // the alkalinity the acid additions remove, as milliequivalents
    double alkalinityAfterAcid = 0;              // mg/L as CaCO3, after the additions
    std::optional<double> acidGramsForTarget;    // of the chosen acid, to reach the target mash pH
    // The mash pH from the grist's acidity and the mash water's residual alkalinity, before and after the acids.
    double estimatedMashPhBeforeAcid = 0;
    double estimatedMashPh = 0;
    // The sparge acid for the sparge water to reach its target pH, as the chosen acid sold at its strength.
    std::optional<double> spargeAcidGrams;
    std::optional<double> spargeAcidMilliliters;  // nil for a solid acid
    // Beer color range from the residual alkalinity after the acids (Palmer's rough mapping).
    std::optional<double> colorLowSrm;
    std::optional<double> colorHighSrm;
    std::vector<std::string> notes;
};

// Plans the brewing water, mash, and sparge for the recipe. Throws std::invalid_argument
// for an unknown salt or acid, or for a value that cannot be planned.
BrewPlan planBrewing(const model::Recipe& recipe, const BrewPlanInputs& inputs);

}  // namespace kettlelogic::calc
