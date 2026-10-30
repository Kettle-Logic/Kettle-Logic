#include "kettlelogic/calc/brew_plan.hpp"

#include "kettlelogic/calc/brew_water.hpp"
#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/quantity.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kettlelogic::calc {

namespace {
constexpr double kHco3 = 61.017;  // g/mol of bicarbonate
constexpr double kCaCo3 = 50.04;  // the mg of CaCO3 that one milliequivalent of alkalinity is

// Liters of tun volume that wet grain displaces, per kilogram of grain. A rule of thumb.
constexpr double kGrainDisplacementLPerKg = 0.7;
constexpr double kLitersPerQuart = 0.946352946;
constexpr double kKilogramsPerPound = 0.45359237;

// The milliequivalents of acid per gram, as the planner counts an acid. The brew water acids depend
// on pH, through the protons they give there. The blend and ascorbic acid keep the planner's older
// full-protonation estimate.
double milliequivalentsPerGram(const std::string& kind, double pH) {
    if (kind == "acid blend") {  // 45 % malic, 45 % citric, 10 % tartaric
        return 0.45 * 2 * 1000.0 / 134.09 + 0.45 * 3 * 1000.0 / 192.12 + 0.10 * 2 * 1000.0 / 150.09;
    }
    if (kind == "ascorbic acid") {  // one proton over 176.12 g/mol, weak in the mash
        return 1000.0 / 176.12;
    }
    // The strength each acid is sold at, as the planner has always assumed it.
    double strength = 100.0;
    if (kind == "lactic acid") strength = 88.0;
    if (kind == "phosphoric acid") strength = 85.0;
    const brew_water::Acid* acid = brew_water::findAcid(kind);
    if (acid == nullptr) {
        throw std::invalid_argument("\"" + kind + "\" is not an acid");
    }
    return strength / 100.0 * 1000.0 / acid->molarMass * brew_water::protonsPerMolecule(*acid, pH);
}

WaterAnalysis analyze(const Ions& ions) {
    WaterAnalysis a;
    a.ions = ions;
    a.alkalinity = ions.bicarbonate * kCaCo3 / kHco3 + ions.carbonate * kCaCo3 / 30.01;
    a.effectiveHardness = ions.calcium * kCaCo3 / 20.04 + ions.magnesium * kCaCo3 / 12.15;
    a.residualAlkalinity = residualAlkalinity(ions);
    if (ions.chloride > 0) {
        const double ratio = ions.sulfate / ions.chloride;
        a.sulfateToChloride = ratio;
        // The thresholds are a common reading of the ratio, not a standard.
        a.balance = ratio < 0.5 ? "malt-forward" : ratio <= 1.5 ? "balanced" : "hop-forward";
    } else {
        a.balance = ions.sulfate > 0 ? "hop-forward" : "not set";
    }
    return a;
}

// The salts that go to the sparge or to the mash water.
std::vector<brew_water::SaltGrams> saltsOf(const std::vector<WaterSalt>& salts, bool sparge) {
    std::vector<brew_water::SaltGrams> out;
    for (const WaterSalt& salt : salts) {
        if (salt.sparge == sparge) {
            out.push_back(brew_water::SaltGrams{salt.kind, salt.grams});
        }
    }
    return out;
}

double grainKilograms(const model::Recipe& recipe) {
    double kg = 0;
    for (const model::Fermentable& f : recipe.fermentables) {
        if (f.type == "grain") {
            kg += f.amount.valueIn(Unit::Kilogram).toDouble();
        }
    }
    return kg;
}

// The degrees Lovibond of a grain's color, from the unit it was entered in.
double lovibondOf(const model::Fermentable& grain) {
    const double value = grain.color.value().toDouble();
    if (grain.color.unit() == "Lovi") return value;
    if (grain.color.unit() == "EBC") return brew_water::lovibondFromEbc(value);
    if (grain.color.unit() == "SRM") return (value + 0.6) / 1.3;
    return value;
}

// The malt's role in the grist, from its BeerJSON grain group. A grain with no group is sorted by its color:
// up to 5 °L is base malt, up to 150 °L is crystal, and anything darker is roasted.
brew_water::MaltCategory categoryOf(const model::Fermentable& grain) {
    if (grain.grainGroup) {
        const std::string& group = *grain.grainGroup;
        if (group == "caramel" || group == "specialty") return brew_water::MaltCategory::Crystal;
        if (group == "roasted") return brew_water::MaltCategory::Roast;
        return brew_water::MaltCategory::Base;
    }
    const double lovibond = lovibondOf(grain);
    if (lovibond <= 5) return brew_water::MaltCategory::Base;
    if (lovibond <= 150) return brew_water::MaltCategory::Crystal;
    return brew_water::MaltCategory::Roast;
}

std::vector<brew_water::Grain> grainsOf(const model::Recipe& recipe) {
    std::vector<brew_water::Grain> grist;
    for (const model::Fermentable& f : recipe.fermentables) {
        if (f.type == "grain") {
            grist.push_back(brew_water::Grain{f.amount.valueIn(Unit::Kilogram).toDouble(), lovibondOf(f), categoryOf(f)});
        }
    }
    return grist;
}

}  // namespace

BrewPlan planBrewing(const model::Recipe& recipe, const BrewPlanInputs& in) {
    if (!(in.mashRatioLPerKg > 0) || !(in.absorptionLPerKg >= 0)) {
        throw std::invalid_argument("the mash ratio must be more than zero, and the absorption cannot be negative");
    }
    if (!(in.deadspaceLiters >= 0)) {
        throw std::invalid_argument("the tun deadspace cannot be negative");
    }
    if (!(in.mashTempC > 0 && in.mashTempC < 100)) {
        throw std::invalid_argument("the mash temperature must be between 0 and 100 °C");
    }
    if (!(in.targetMashPh > 0 && in.targetMashPh < 14)) {
        throw std::invalid_argument("the target mash pH must be between 0 and 14");
    }

    BrewPlan plan;
    const Results results = calculate(recipe);
    plan.grainKg = grainKilograms(recipe);
    plan.strikeLiters = in.mashRatioLPerKg * plan.grainKg;
    plan.absorptionLiters = in.absorptionLPerKg * plan.grainKg;
    plan.firstRunningsLiters = std::max(0.0, plan.strikeLiters - plan.absorptionLiters);

    // The sparge makes up the pre-boil volume, from the wort the mash drains first.
    if (results.preBoilLiters) {
        plan.spargeLiters = std::max(0.0, *results.preBoilLiters - plan.firstRunningsLiters);
    } else {
        plan.notes.push_back("The recipe has no pre-boil volume yet, so the sparge volume is not given.");
    }

    // The mash volume is the strike water, with the tun's deadspace when the user asks for it.
    plan.mashVolumeLiters = plan.strikeLiters + (in.adjustForDeadspace ? in.deadspaceLiters : 0);
    plan.mashTunVolumeLiters = plan.mashVolumeLiters + kGrainDisplacementLPerKg * plan.grainKg;

    // The strike temperature, by the usual relation for a water-to-grain ratio in quarts per pound.
    if (plan.strikeLiters > 0 && plan.grainKg > 0) {
        const double quartsPerPound = in.mashRatioLPerKg * 0.4794;
        plan.strikeTempC = (0.2 / quartsPerPound) * (in.mashTempC - in.grainTempC) + in.mashTempC;
    }

    // Salts: none when the user leaves them out. Sparge salts go in the kettle when held.
    const bool useSalts = !in.leaveOutSalts;
    const std::vector<WaterSalt> none;
    const std::vector<WaterSalt>& mashSalts = useSalts ? in.salts : none;
    std::vector<WaterSalt> spargeSalts;
    if (useSalts && !in.holdSpargeSaltsUntilBoil) {
        spargeSalts = in.salts;
    }
    if (useSalts && in.holdSpargeSaltsUntilBoil) {
        plan.notes.push_back("Sparge salts are held until the boil, so they are not in the sparge water.");
    }
    const double spargeLiters = plan.spargeLiters.value_or(0);
    plan.spargeWater = analyze(brew_water::finishWater(in.baseWater, in.roPercent, in.roWater, saltsOf(spargeSalts, true),
                                                 std::max(spargeLiters, 1e-9), 1.0));

    // The mash pH: the grist's acidity and the mash water's residual alkalinity,
    // with the water-to-grist ratio in quarts per pound. Baking soda's bicarbonate depends on the pH, and the
    // acids change the pH, so each pH is found by repeating until it stops moving.
    const double mashLiters = std::max(plan.strikeLiters, 1e-9);
    const bool hasGrist = plan.grainKg > 0 && plan.strikeLiters > 0;
    const bool bakingSoda = std::any_of(mashSalts.begin(), mashSalts.end(),
                                        [](const WaterSalt& salt) { return salt.kind == "baking soda"; });
    if (hasGrist) {
        plan.maltAcidityMilliequivalents = brew_water::maltAcidityMilliequivalents(grainsOf(recipe));
        plan.quartsPerPound = (plan.strikeLiters / kLitersPerQuart) / (plan.grainKg / kKilogramsPerPound);
    }
    // The mash water's residual alkalinity at a pH.
    const auto residualAt = [&](double pH) {
        const double factor = bakingSoda ? brew_water::bakingSodaBicarbonateFactor(pH) : 1.0;
        return residualAlkalinity(brew_water::finishWater(in.baseWater, in.roPercent, in.roWater, saltsOf(mashSalts, false),
                                                    mashLiters, factor));
    };
    // The mash pH when the acids remove `removedPpm` of the alkalinity.
    const auto phWith = [&](double removedPpm) {
        double pH = 5.7;
        for (int i = 0; i < 200; ++i) {
            const double next = brew_water::mashPh(plan.maltAcidityMilliequivalents, residualAt(pH) - removedPpm,
                                             plan.quartsPerPound, mashLiters);
            const bool settled = std::fabs(next - pH) < 1e-10;
            pH = next;
            if (settled) {
                break;
            }
        }
        return pH;
    };

    double removedPpm = 0;
    for (const AcidAddition& acid : in.acids) {
        const double milliequivalents = acid.grams * milliequivalentsPerGram(acid.kind, in.targetMashPh);
        removedPpm += milliequivalents * kCaCo3 / mashLiters;
    }
    plan.acidMilliequivalents = removedPpm * mashLiters / kCaCo3;

    const double preAcidPh = hasGrist ? phWith(0) : 0;
    const double factorBefore = bakingSoda && hasGrist ? brew_water::bakingSodaBicarbonateFactor(preAcidPh) : 1.0;
    plan.mashWater = analyze(brew_water::finishWater(in.baseWater, in.roPercent, in.roWater, saltsOf(mashSalts, false),
                                               mashLiters, factorBefore));
    if (in.targetWater) {
        const Ions& t = *in.targetWater;
        const Ions& m = plan.mashWater.ions;
        Ions diff;
        diff.calcium = t.calcium - m.calcium;
        diff.magnesium = t.magnesium - m.magnesium;
        diff.sodium = t.sodium - m.sodium;
        diff.sulfate = t.sulfate - m.sulfate;
        diff.chloride = t.chloride - m.chloride;
        diff.bicarbonate = t.bicarbonate - m.bicarbonate;
        diff.carbonate = t.carbonate - m.carbonate;
        plan.mashDifference = diff;
    }
    if (hasGrist) {
        plan.estimatedMashPhBeforeAcid = preAcidPh;
        plan.estimatedMashPh = phWith(removedPpm);
    } else {
        plan.notes.push_back("The grain bill or the mash water is empty, so the mash pH is not estimated.");
    }
    plan.alkalinityAfterAcid = plan.mashWater.alkalinity - removedPpm;

    // The acid of the chosen kind that takes the mash to the target pH: the alkalinity the mash has at that pH,
    // less the residual alkalinity the target pH needs.
    if (hasGrist) {
        const double raAtTarget = residualAt(in.targetMashPh);
        const double raNeeded = brew_water::residualAlkalinityForMashPh(in.targetMashPh, plan.maltAcidityMilliequivalents,
                                                                   plan.quartsPerPound, mashLiters);
        const double toRemove = raAtTarget - raNeeded;
        if (toRemove > 0) {
            const double milliequivalents = toRemove * mashLiters / kCaCo3;
            plan.acidGramsForTarget = milliequivalents / milliequivalentsPerGram(in.acidForTarget, in.targetMashPh);
        } else {
            plan.acidGramsForTarget = 0.0;
        }

        // The color range is Palmer's rough mapping, from the residual alkalinity after the acids.
        const double raAfter = residualAt(plan.estimatedMashPh) - removedPpm;
        if (raAfter > -128) {
            plan.colorLowSrm = std::max(0.0, raAfter * 0.082 + 5.2);
            plan.colorHighSrm = (raAfter + 122.4) / 12.2;
        } else {
            plan.notes.push_back("The residual alkalinity is too low for the color estimate.");
        }
    }

    // The sparge acid, by carbonate titration of the sparge water.
    if (spargeLiters > 0) {
        const brew_water::Acid* acid = brew_water::findAcid(in.spargeAcid);
        if (acid == nullptr) {
            throw std::invalid_argument("\"" + in.spargeAcid + "\" is not an acid the sparge calculation knows");
        }
        const brew_water::SpargeAcid sparge = brew_water::spargeAcidification(plan.spargeWater.alkalinity, in.spargeStartPh,
                                                                  in.spargeTargetPh, spargeLiters, *acid,
                                                                  in.spargeAcidStrengthPercent);
        plan.spargeAcidGrams = sparge.solutionGrams ? *sparge.solutionGrams : sparge.pureGrams;
        plan.spargeAcidMilliliters = sparge.milliliters;
    }
    return plan;
}

}  // namespace kettlelogic::calc
