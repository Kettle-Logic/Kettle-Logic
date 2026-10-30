#pragma once

#include "kettlelogic/model/model.hpp"

#include <optional>
#include <string>
#include <vector>

// Water chemistry: the mash water's ion concentrations, its residual alkalinity,
// and the salt additions that reach a target profile.
//
// A miscellaneous ingredient is a salt only when its salt type is set
// (model::Miscellaneous::salt). The name is never used. See docs/calc.md,
// "Water chemistry".
namespace kettlelogic::calc {

// Ion concentrations, in mg/L.
struct Ions {
    double calcium = 0;
    double magnesium = 0;
    double sodium = 0;
    double sulfate = 0;
    double chloride = 0;
    double bicarbonate = 0;
    double carbonate = 0;
};

struct SaltAddition {
    std::string name;   // as named in the recipe, for example "Gypsum"
    double grams = 0;   // the mass to add to the brewing water
};

struct WaterChemistry {
    std::optional<double> waterLiters;         // total brewing water: the sum of the water additions
    Ions source;                               // the water additions, blended by volume
    std::vector<SaltAddition> salts;           // to reach the target; empty without a target
    Ions mash;                                 // the source with the salts added
    std::optional<double> residualAlkalinity;  // Kolbach, in ppm as CaCO3, of `mash`
    std::vector<std::string> notes;
};

// Computes the water chemistry. Without a target, `salts` is empty and `mash`
// is the source. With one, salts are added in this order, each for one ion:
// gypsum for sulfate, calcium chloride for chloride, Epsom salt for magnesium,
// and baking soda for bicarbonate. Calcium and sodium follow from those
// salts and are reported, not targeted. Salts are never removed: an ion already
// at or above its target gets no salt, and a note says so.
WaterChemistry calculateWater(const model::Recipe& recipe, const std::optional<Ions>& target = std::nullopt);

// The same salt sizing as calculateWater, for water that is not in a recipe: `liters` of water with the
// ions `source` are brought to `target` by adding salts. Used by the water profile tool.
WaterChemistry targetProfile(const Ions& source, double liters, const Ions& target);

// Kolbach residual alkalinity, in ppm as CaCO3: alkalinity − (calcium and magnesium as CaCO3)/3.5 and /7,
// (1 + 2e-4) on the alkalinity. The coefficients are in water.cpp.
double residualAlkalinity(const Ions& ions);

}  // namespace kettlelogic::calc
