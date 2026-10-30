#pragma once

#include "kettlelogic/calc/water.hpp"

#include <optional>
#include <string>
#include <vector>

// The brewing water calculations: the report's ion balance and alkalinity, the malt's
// acidity, the mash pH, the acid that reaches a target mash pH, and the acid for the
// sparge water. The formulas are checked value by value against a reference
// spreadsheet.
namespace kettlelogic::calc::brew_water {

// Ions that the report balance counts, beyond the seven in Ions. In mg/L.
struct OtherIons {
    double potassium = 0;
    double iron = 0;
    double nitrate = 0;
    double nitrite = 0;
    double fluoride = 0;
};

struct ReportBalance {
    double cationsMeq = 0;  // calcium, magnesium, sodium, potassium, iron
    double anionsMeq = 0;   // bicarbonate, carbonate, sulfate, chloride, nitrate, nitrite, fluoride
    bool balanced = true;   // the two sides differ by no more than 0.5 meq/L
};

// The cation and anion totals of a water report, in milliequivalents per liter.
ReportBalance balanceReport(const Ions& ions, const OtherIons& other);

// Bicarbonate and carbonate, mg/L, from a total alkalinity (mg/L as CaCO3) at a pH. This is the
// conversion from a reported alkalinity, using carbonic acid's second dissociation
// constant (pK2 10.33).
struct Speciation {
    double bicarbonate = 0;
    double carbonate = 0;
};
Speciation speciesFromAlkalinity(double alkalinityCaCO3, double pH);

// The grain's role in the grist. The malt's acidity depends on it.
enum class MaltCategory { Base, Crystal, Roast, Acid };

struct Grain {
    double kilograms = 0;
    double lovibond = 0;  // color in degrees Lovibond
    MaltCategory category = MaltCategory::Base;
};

// Lovibond from EBC, through SRM: (EBC / 1.97 + 0.6) / 1.3.
double lovibondFromEbc(double ebc);

// The malt's acidity, in milliequivalents, per pound: base malt 0.28 per
// degree Lovibond, crystal 0.21 per degree plus 2.5, roast 38, and acid malt 95.
double maltAcidityMilliequivalents(const std::vector<Grain>& grist);

// The mash pH. The residual alkalinity is the one calc::residualAlkalinity gives for the mash water. Model is linear: pH = 5.76 − 0.17 × (acid per liter − RA × q / 75),
// where the acid per liter is the grist's malt acidity over the mash water's liters, and q is the
// water-to-grist ratio in quarts per pound. Returns 0.1 when this is below zero.
double mashPh(double maltAcidityMilliequivalents, double residualAlkalinity, double qtPerLb, double mashLiters);

// The pH at which the mash lands for a given residual alkalinity, the inverse of mashPh.
double residualAlkalinityForMashPh(double targetPh, double maltAcidityMilliequivalents, double qtPerLb,
                                   double mashLiters);

// The share of baking soda's bicarbonate that stays in the mash at `mashPh`:
// 2 − P(pH), with P a fit of pH to the fourth power. At a pH of 5.8 about 73 % remains. The mash pH
// depends on this, so the planner finds them together.
double bakingSodaBicarbonateFactor(double mashPh);

// A salt added to water, by its kind as the planner names it, and its mass in grams.
struct SaltGrams {
    std::string kind;
    double grams = 0;
};

// The ions one gram of a salt adds to water, in mg per gram. The kinds are
// "gypsum", "calcium chloride", "epsom salt", "magnesium chloride", "table salt", "baking soda", "chalk", and
// "pickling lime". The calcium chloride is the anhydrous salt. Throws
// std::invalid_argument for any other kind.
Ions ionsPerGramOfSalt(const std::string& kind);

// The water after `roPercent` of `ro` is mixed into `base`, and each salt is dissolved in `liters` of it.
// Baking soda's bicarbonate is scaled by `bicarbonateFactor` (see
// bakingSodaBicarbonateFactor); pass 1 when the mash pH is not known.
Ions finishWater(const Ions& base, double roPercent, const Ions& ro, const std::vector<SaltGrams>& salts,
                 double liters, double bicarbonateFactor);

// A water acid
struct Acid {
    const char* name;      // "lactic acid", "phosphoric acid", ...
    double pK1;
    double pK2;
    double pK3;            // pKs of 20 are placeholders for acids that give one proton
    double molarMass;      // g/mol
    bool liquid;           // true for a solution sold by strength, false for a solid sold by weight
};

// The acid by its name, or nullptr if not listed. Names are as the planner uses them.
const Acid* findAcid(const std::string& name);

// Average protons removed per molecule of the acid at `pH`.
double protonsPerMolecule(const Acid& acid, double pH);

// The density of the acid as sold, g/L, at `strengthPercent`.
// Throws std::invalid_argument for a solid acid, which is sold by weight.
double solutionDensityGramsPerLiter(const Acid& acid, double strengthPercent);

// The alkalinity (mg/L as CaCO3) that `grams` of an acid, as sold at `strengthPercent`, removes from `liters` of
// water at `pH`: 50 per milliequivalent of protons the acid gives there.
double alkalinityRemovedByAcid(const Acid& acid, double grams, double strengthPercent, double liters, double pH);

// The grams of an acid, as sold at `strengthPercent`, that remove `alkalinityPpm` (mg/L as CaCO3)
// from `liters` of water at `pH`. The inverse of alkalinityRemovedByAcid.
double acidGramsToRemoveAlkalinity(const Acid& acid, double alkalinityPpm, double strengthPercent, double liters,
                                   double pH);

// Add this to every sparge acid requirement, in mmol/L. It is kept so the results match it.
inline constexpr double kSpargeRequirementOffsetMmol = 0.01;

struct SpargeAcid {
    double acidMmolPerLiter = 0;                 // acid the sparge water needs
    double pureGrams = 0;                        // the acid's mass, pure
    std::optional<double> solutionGrams;         // as sold, at the strength; nil for a solid
    std::optional<double> milliliters;           // as sold, by volume; nil for a solid
};

// The acid that takes sparge water from `startPh` to `targetPh`, by the carbonate titration.
// Alkalinity is mg/L as CaCO3, and `liters` is the sparge volume.
SpargeAcid spargeAcidification(double alkalinityCaCO3, double startPh, double targetPh, double liters,
                               const Acid& acid, double strengthPercent);

}  // namespace kettlelogic::calc::brew_water
