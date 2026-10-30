#include "kettlelogic/calc/water.hpp"

#include "kettlelogic/quantity.hpp"

#include <string>

namespace kettlelogic::calc {

namespace {

// Standard atomic masses, in g/mol.
constexpr double kCa = 40.078;
constexpr double kMg = 24.305;
constexpr double kNa = 22.990;
constexpr double kS = 32.065;
constexpr double kO = 15.999;
constexpr double kCl = 35.453;
constexpr double kH = 1.008;
constexpr double kC = 12.011;

constexpr double kSo4 = kS + 4 * kO;          // 96.061
constexpr double kHco3 = kH + kC + 3 * kO;    // 61.017
constexpr double kWater = 2 * kH + kO;        // 18.015

// Ion contributions per gram of salt, in mg/g: the ion's mass fraction times 1000.
struct SaltFormula {
    const char* name;
    double molarMass;
    Ions perGram;  // mg of each ion per gram of salt
};

SaltFormula gypsum() {  // CaSO4 · 2 H2O
    const double m = kCa + kSo4 + 2 * kWater;
    return {"gypsum", m, {kCa / m * 1000, 0, 0, kSo4 / m * 1000, 0, 0, 0}};
}

SaltFormula calciumChloride() {  // CaCl2 · 2 H2O
    const double m = kCa + 2 * kCl + 2 * kWater;
    return {"calcium chloride", m, {kCa / m * 1000, 0, 0, 0, 2 * kCl / m * 1000, 0, 0}};
}

SaltFormula epsomSalt() {  // MgSO4 · 7 H2O
    const double m = kMg + kSo4 + 7 * kWater;
    return {"Epsom salt", m, {0, kMg / m * 1000, 0, kSo4 / m * 1000, 0, 0, 0}};
}

SaltFormula bakingSoda() {  // NaHCO3
    const double m = kNa + kHco3;
    return {"baking soda", m, {0, 0, kNa / m * 1000, 0, 0, kHco3 / m * 1000, 0}};
}

SaltFormula saltFormula(model::SaltKind kind) {
    switch (kind) {
        case model::SaltKind::Gypsum: return gypsum();
        case model::SaltKind::CalciumChloride: return calciumChloride();
        case model::SaltKind::EpsomSalt: return epsomSalt();
        case model::SaltKind::BakingSoda: return bakingSoda();
    }
    return gypsum();
}

// Adds `grams` of a salt to `ions`, given the brewing water volume in liters.
void dissolve(Ions& ions, const SaltFormula& salt, double grams, double liters) {
    ions.calcium += salt.perGram.calcium * grams / liters;
    ions.magnesium += salt.perGram.magnesium * grams / liters;
    ions.sodium += salt.perGram.sodium * grams / liters;
    ions.sulfate += salt.perGram.sulfate * grams / liters;
    ions.chloride += salt.perGram.chloride * grams / liters;
    ions.bicarbonate += salt.perGram.bicarbonate * grams / liters;
    ions.carbonate += salt.perGram.carbonate * grams / liters;
}

// The grams of a salt that raise one ion by `delta` mg/L in `liters` of water.
double gramsFor(double delta, double ionPerGram, double liters) {
    return delta * liters / ionPerGram;
}

// Adds a salt for one ion if the target is above the current value.
void targetIon(WaterChemistry& chem, const SaltFormula& salt, const char* ion, double current, double goal,
               double liters, double ionPerGram) {
    if (goal <= current) {
        chem.notes.push_back(std::string(ion) + " is already at or above its target, so no " + salt.name +
                             " was added for it");
        return;
    }
    const double grams = gramsFor(goal - current, ionPerGram, liters);
    chem.salts.push_back(SaltAddition{salt.name, grams});
    dissolve(chem.mash, salt, grams, liters);
}

// Sizes the four salts that reach a target profile, each for the ion it controls.
void sizeSalts(WaterChemistry& chem, const Ions& target, double liters) {
    // Sequential targeting: each salt is sized for the ion it controls.
    targetIon(chem, gypsum(), "sulfate", chem.mash.sulfate, target.sulfate, liters, gypsum().perGram.sulfate);
    targetIon(chem, calciumChloride(), "chloride", chem.mash.chloride, target.chloride, liters,
              calciumChloride().perGram.chloride);
    targetIon(chem, epsomSalt(), "magnesium", chem.mash.magnesium, target.magnesium, liters,
              epsomSalt().perGram.magnesium);
    targetIon(chem, bakingSoda(), "bicarbonate", chem.mash.bicarbonate, target.bicarbonate, liters,
              bakingSoda().perGram.bicarbonate);
}

}  // namespace

double residualAlkalinity(const Ions& ions) {
    const double alkalinity = ions.bicarbonate * 50.0 / 61.0 + ions.carbonate * 50.0 / 30.0;
    // Calcium and magnesium counted as CaCO3 over Kolbach's 3.5 and 7. As coefficients on mg/L of the
    // ion, that is 2.5 / 3.5 for calcium and 4.12 / 7 for magnesium.
    return alkalinity * (1.0 + 2e-4) - (0.7143 * ions.calcium + 0.5879 * ions.magnesium);
}

WaterChemistry targetProfile(const Ions& source, double liters, const Ions& target) {
    WaterChemistry chem;
    chem.waterLiters = liters;
    chem.source = source;
    chem.mash = source;
    sizeSalts(chem, target, liters);
    chem.residualAlkalinity = residualAlkalinity(chem.mash);
    return chem;
}

WaterChemistry calculateWater(const model::Recipe& recipe, const std::optional<Ions>& target) {
    WaterChemistry chem;
    if (!recipe.waters) {
        chem.notes.push_back("the recipe has no water additions, so the water chemistry is unknown");
        return chem;
    }

    // Blend the water additions by volume.
    double totalLiters = 0;
    Ions weighted;
    for (const model::WaterAddition& water : *recipe.waters) {
        if (!water.amount) {
            chem.notes.push_back("\"" + water.water.name + "\" has no volume, so it was left out of the blend");
            continue;
        }
        const double liters = water.amount->valueIn(Unit::Liter).toDouble();
        const auto ion = [&](const std::optional<Quantity>& q) {
            return q ? q->valueIn(Unit::MilligramPerLiter).toDouble() : 0.0;
        };
        const model::WaterProfile& p = water.water;
        weighted.calcium += liters * p.calcium.valueIn(Unit::MilligramPerLiter).toDouble();
        weighted.magnesium += liters * p.magnesium.valueIn(Unit::MilligramPerLiter).toDouble();
        weighted.sodium += liters * p.sodium.valueIn(Unit::MilligramPerLiter).toDouble();
        weighted.sulfate += liters * p.sulfate.valueIn(Unit::MilligramPerLiter).toDouble();
        weighted.chloride += liters * p.chloride.valueIn(Unit::MilligramPerLiter).toDouble();
        weighted.bicarbonate += liters * p.bicarbonate.valueIn(Unit::MilligramPerLiter).toDouble();
        weighted.carbonate += liters * ion(p.carbonate);
        totalLiters += liters;
    }
    if (totalLiters <= 0) {
        chem.notes.push_back("the water additions have no volume, so the water chemistry is unknown");
        return chem;
    }
    chem.waterLiters = totalLiters;
    chem.source = Ions{weighted.calcium / totalLiters, weighted.magnesium / totalLiters, weighted.sodium / totalLiters,
                       weighted.sulfate / totalLiters, weighted.chloride / totalLiters,
                       weighted.bicarbonate / totalLiters, weighted.carbonate / totalLiters};
    chem.mash = chem.source;

    // Salts on the miscellaneous list are additions in their own right, when
    // their salt type is set. Their masses dissolve first, so a target only adds
    // the shortfall.
    if (recipe.miscellaneous) {
        for (const model::Miscellaneous& misc : *recipe.miscellaneous) {
            if (!misc.salt) {
                if (misc.type == "water agent") {
                    chem.notes.push_back("\"" + misc.name + "\" is a water agent with no salt type, so it was not added to the water");
                }
                continue;
            }
            if (misc.amount.dimension() != Dimension::Mass) {
                chem.notes.push_back("\"" + misc.name + "\" is measured by volume, so it was not added to the water");
                continue;
            }
            const SaltFormula salt = saltFormula(*misc.salt);
            const double grams = misc.amount.valueIn(Unit::Gram).toDouble();
            chem.salts.push_back(SaltAddition{misc.name, grams});
            dissolve(chem.mash, salt, grams, totalLiters);
        }
    }

    if (target) {
        sizeSalts(chem, *target, totalLiters);
    }

    chem.residualAlkalinity = residualAlkalinity(chem.mash);
    return chem;
}

}  // namespace kettlelogic::calc
