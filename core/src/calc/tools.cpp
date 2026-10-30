#include "kettlelogic/calc/tools.hpp"

#include "kettlelogic/calc/fermented.hpp"
#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/calc/brew_water.hpp"
#include "kettlelogic/calc/starter.hpp"
#include "kettlelogic/calc/water.hpp"
#include "kettlelogic/quantity.hpp"
#include "kettlelogic/rational.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

// Formulas and their sources are in docs/calc.md, "Tools".
namespace kettlelogic::calc::tools {
namespace {

constexpr double kKilogramsPerPound = 0.45359237;
constexpr double kLitersPerQuart = 0.946352946;

// Brix to specific gravity, and Plato to specific gravity, use the same
// sucrose relation: SG = 1 + P / (258.6 - P * 227.1 / 258.2).
constexpr double kBallingScale = 258.6;
constexpr double kBallingSlope = 227.1 / 258.2;

constexpr double kPsiPerAtmosphere = 14.695948775;       // 101325 Pa / 6894.757293168 Pa per psi
constexpr double kKilopascalsPerAtmosphere = 101.325;

constexpr double kCo2MolarMass = 44.009;                 // g/mol
constexpr double kGlucoseMolarMass = 180.156;            // g/mol, C6H12O6
constexpr double kSucroseMolarMass = 342.297;            // g/mol, C12H22O11
constexpr double kMolarVolumeLiters = 22.414;            // one mole of gas at 0 C and 1 atm
constexpr double kNitrogenPerDapMgPerGram = 210.0;       // diammonium phosphate is 21 % nitrogen

// Inputs are read here. A number is decimal text, read exactly and then used as a double.
class Reader {
public:
    explicit Reader(const Inputs& inputs) : inputs_(inputs) {}

    const std::string& text(const std::string& key) const {
        const auto found = inputs_.find(key);
        if (found == inputs_.end()) {
            throw std::invalid_argument("missing input \"" + key + "\"");
        }
        return found->second;
    }

    double number(const std::string& key) const {
        return Rational::fromDecimal(text(key)).toDouble();
    }

    bool flag(const std::string& key) const {
        return number(key) != 0;
    }

    bool has(const std::string& key) const {
        return inputs_.find(key) != inputs_.end();
    }

    // The number when the input is given, or `fallback` when it is left out.
    double numberOr(const std::string& key, double fallback) const {
        return has(key) ? number(key) : fallback;
    }

private:
    const Inputs& inputs_;
};

void require(bool ok, const std::string& message) {
    if (!ok) {
        throw std::invalid_argument(message);
    }
}

// Volumes of CO2 (at 0 C and 1 atm) that water holds per volume at 1 atm. This is the Bunsen
// coefficient, 0.878 at 20 C, with a van 't Hoff temperature dependence fitted to 1.713 at 0 C.
double co2VolumesPerAtmosphere(double celsius) {
    const double kelvin = celsius + 273.15;
    return 0.878 * std::exp(2674.0 * (1.0 / kelvin - 1.0 / 293.15));
}

// Grams of sugar that give one volume of CO2 in a liter of beer, when each gram of sugar gives
// `co2GramsPerGram` grams of CO2 as it ferments.
double sugarGramsPerVolume(double co2GramsPerGram) {
    const double litersOfCo2PerGramOfSugarPerLiter = co2GramsPerGram * kMolarVolumeLiters / kCo2MolarMass;
    return 1.0 / litersOfCo2PerGramOfSugarPerLiter;
}

// Specific gravity from the named gravity unit.
double specificGravityFrom(double value, const std::string& unit) {
    if (unit == "sg") {
        return value;
    }
    if (unit == "plato" || unit == "brix") {
        require(value >= 0 && value < kBallingScale / kBallingSlope, "that Brix or Plato value is out of range");
        return 1.0 + value / (kBallingScale - value * kBallingSlope);
    }
    if (unit == "points") {
        return 1.0 + value / 1000.0;
    }
    throw std::invalid_argument("unknown gravity unit \"" + unit + "\"");
}

// The named gravity unit from specific gravity.
double gravityIn(double sg, const std::string& unit) {
    if (unit == "sg") {
        return sg;
    }
    if (unit == "plato" || unit == "brix") {
        const double d = sg - 1.0;
        return kBallingScale * d / (1.0 + kBallingSlope * d);
    }
    if (unit == "points") {
        return (sg - 1.0) * 1000.0;
    }
    throw std::invalid_argument("unknown gravity unit \"" + unit + "\"");
}

// Pascals in one of the pressure units.
double pascalsPer(const std::string& unit) {
    if (unit == "psi") {
        return 6894.757293168;
    }
    if (unit == "kPa") {
        return 1000.0;
    }
    if (unit == "bar") {
        return 100000.0;
    }
    if (unit == "atm") {
        return 101325.0;
    }
    throw std::invalid_argument("unknown pressure unit \"" + unit + "\"");
}

// Density of water in kg/m^3 at `celsius`, by Kell (1975), valid from 0 to 100 C.
double waterDensity(double celsius) {
    const double t = celsius;
    const double numerator = 999.83952 + 16.945176 * t - 7.9870401e-3 * t * t - 46.170461e-6 * t * t * t +
                             105.56302e-9 * t * t * t * t - 280.54253e-12 * t * t * t * t * t;
    return numerator / (1.0 + 16.879850e-3 * t);
}

// ---- Water and mash ----

// The brewing water profile: a base water mixed with RO water, then the salts added as grams, with the
// result, the residual alkalinity, and the difference from a target. Baking soda is scaled by the mash pH when
// one is given.
Output waterProfile(const Reader& in) {
    const double liters = in.number("volume");
    require(liters > 0, "the water volume must be above zero");
    const double roPercent = in.numberOr("roPercent", 0);

    const auto ionsWithPrefix = [&](const std::string& prefix) {
        const auto key = [&](const char* name) {
            return prefix.empty() ? std::string(name) : prefix + std::string(1, static_cast<char>(name[0] - 32)) +
                                                          std::string(name + 1);
        };
        Ions ions;
        ions.calcium = in.numberOr(key("calcium"), 0);
        ions.magnesium = in.numberOr(key("magnesium"), 0);
        ions.sodium = in.numberOr(key("sodium"), 0);
        ions.sulfate = in.numberOr(key("sulfate"), 0);
        ions.chloride = in.numberOr(key("chloride"), 0);
        ions.bicarbonate = in.numberOr(key("bicarbonate"), 0);
        ions.carbonate = in.numberOr(key("carbonate"), 0);
        return ions;
    };
    const Ions base = ionsWithPrefix("");
    const Ions ro = ionsWithPrefix("ro");

    const std::vector<brew_water::SaltGrams> salts{
        {"gypsum", in.numberOr("gypsum", 0)},
        {"calcium chloride", in.numberOr("calciumChloride", 0)},
        {"epsom salt", in.numberOr("epsomSalt", 0)},
        {"magnesium chloride", in.numberOr("magnesiumChloride", 0)},
        {"table salt", in.numberOr("tableSalt", 0)},
        {"baking soda", in.numberOr("bakingSoda", 0)},
        {"chalk", in.numberOr("chalk", 0)},
        {"pickling lime", in.numberOr("picklingLime", 0)},
    };
    const double mashPh = in.numberOr("mashPh", 0);
    const double factor = mashPh > 0 ? brew_water::bakingSodaBicarbonateFactor(mashPh) : 1.0;
    const Ions finished = brew_water::finishWater(base, roPercent, ro, salts, liters, factor);

    Output out;
    out.results["calcium"] = finished.calcium;
    out.results["magnesium"] = finished.magnesium;
    out.results["sodium"] = finished.sodium;
    out.results["sulfate"] = finished.sulfate;
    out.results["chloride"] = finished.chloride;
    out.results["bicarbonate"] = finished.bicarbonate;
    out.results["residualAlkalinity"] = residualAlkalinity(finished);
    if (finished.chloride > 0) {
        out.results["sulfateToChloride"] = finished.sulfate / finished.chloride;
    }

    // Each ion's difference from the target, when the user gives a target for it.
    const std::vector<std::pair<const char*, double Ions::*>> ions{
        {"Calcium", &Ions::calcium},         {"Magnesium", &Ions::magnesium}, {"Sodium", &Ions::sodium},
        {"Sulfate", &Ions::sulfate},         {"Chloride", &Ions::chloride},   {"Bicarbonate", &Ions::bicarbonate},
    };
    for (const auto& [name, member] : ions) {
        const std::string key = std::string("target") + name;
        if (in.has(key)) {
            out.results[std::string("difference") + name] = in.number(key) - finished.*member;
        }
    }

    out.notes.push_back("Dilution and salts. Calcium chloride is anhydrous.");
    if (mashPh <= 0) {
        out.notes.push_back("No mash pH was given, so baking soda is not scaled for the mash.");
    }
    return out;
}

// The mash: up to four grains, the mash water's ions, the mash pH and the acid that reaches a
// target pH, and an optional acid dose. Grains are numbered "grain1Kg", "grain1Lovibond", and "grain1Category".
Output brewMash(const Reader& in) {
    Ions ions;
    ions.calcium = in.numberOr("calcium", 0);
    ions.magnesium = in.numberOr("magnesium", 0);
    ions.sodium = in.numberOr("sodium", 0);
    ions.sulfate = in.numberOr("sulfate", 0);
    ions.chloride = in.numberOr("chloride", 0);
    ions.bicarbonate = in.numberOr("bicarbonate", 0);
    ions.carbonate = in.numberOr("carbonate", 0);

    std::vector<brew_water::Grain> grist;
    double grainKg = 0;
    for (int i = 1; i <= 4; ++i) {
        const std::string n = std::to_string(i);
        const double kg = in.numberOr("grain" + n + "Kg", 0);
        if (!(kg > 0)) {
            continue;
        }
        brew_water::Grain grain;
        grain.kilograms = kg;
        grain.lovibond = in.numberOr("grain" + n + "Lovibond", 0);
        const std::string category = in.has("grain" + n + "Category") ? in.text("grain" + n + "Category") : "base";
        if (category == "base") grain.category = brew_water::MaltCategory::Base;
        else if (category == "crystal") grain.category = brew_water::MaltCategory::Crystal;
        else if (category == "roast") grain.category = brew_water::MaltCategory::Roast;
        else if (category == "acid") grain.category = brew_water::MaltCategory::Acid;
        else throw std::invalid_argument("the malt category \"" + category + "\" is not known");
        grist.push_back(grain);
        grainKg += kg;
    }
    const double liters = in.numberOr("waterLiters", 0);
    require(grainKg > 0 && liters > 0, "the mash needs grain and water to find its pH");
    const double targetPh = in.numberOr("targetPh", 5.4);
    const double pounds = grainKg / kKilogramsPerPound;
    const double ratio = (liters / kLitersPerQuart) / pounds;
    const double acidity = brew_water::maltAcidityMilliequivalents(grist);

    // The mash water with its baking soda: the baking soda's bicarbonate depends on the pH, so the pH is found
    // by repeating, as the brewing plan does.
    const double bakingSodaGrams = in.numberOr("bakingSodaGrams", 0);
    const auto residualAt = [&](double pH) {
        const double factor = brew_water::bakingSodaBicarbonateFactor(pH);
        return residualAlkalinity(brew_water::finishWater(ions, 0, Ions{}, {brew_water::SaltGrams{"baking soda", bakingSodaGrams}},
                                                    liters, factor));
    };
    const auto phWith = [&](double removed) {
        double pH = 5.7;
        for (int i = 0; i < 200; ++i) {
            const double next = brew_water::mashPh(acidity, residualAt(pH) - removed, ratio, liters);
            const bool settled = std::fabs(next - pH) < 1e-10;
            pH = next;
            if (settled) {
                break;
            }
        }
        return pH;
    };

    // The acid added to the mash, if the user names one.
    double removed = 0;
    if (in.has("mashAcid") && in.text("mashAcid") != "none") {
        const std::string kind = in.text("mashAcid");
        const brew_water::Acid* acid = brew_water::findAcid(kind);
        require(acid != nullptr, "\"" + kind + "\" is not an acid");
        removed = brew_water::alkalinityRemovedByAcid(*acid, in.numberOr("mashAcidGrams", 0),
                                                in.numberOr("mashAcidStrength", 88), liters, targetPh);
    }

    Output out;
    out.results["maltAcidityMeq"] = acidity;
    out.results["quartsPerPound"] = ratio;
    out.results["residualAlkalinityBefore"] = residualAt(phWith(0));
    out.results["phBefore"] = phWith(0);
    out.results["residualAlkalinityAfter"] = residualAt(phWith(0)) - removed;
    out.results["phAfter"] = phWith(removed);

    // The grams of the target acid that bring the mash to the target pH, with the baking soda at that pH.
    const brew_water::Acid* target = brew_water::findAcid(in.has("targetAcid") ? in.text("targetAcid") : "lactic acid");
    require(target != nullptr, "the target acid is not known");
    const double raTarget = brew_water::residualAlkalinityForMashPh(targetPh, acidity, ratio, liters);
    const double toRemove = std::max(0.0, residualAt(targetPh) - raTarget);
    out.results["acidGramsForTarget"] = brew_water::acidGramsToRemoveAlkalinity(
        *target, toRemove, in.numberOr("targetAcidStrength", 88), liters, targetPh);
    out.notes.push_back("Enter the water without baking soda, and give the baking soda in grams. Its bicarbonate follows "
                        "the mash pH, as in the brewing plan.");
    return out;
}

// The sparge acid: the alkalinity of the sparge water, its start and target pH, and the acid.
Output brewSparge(const Reader& in) {
    const brew_water::Acid* acid = brew_water::findAcid(in.has("acid") ? in.text("acid") : "phosphoric acid");
    require(acid != nullptr, "the sparge acid is not known");
    const brew_water::SpargeAcid sparge = brew_water::spargeAcidification(
        in.number("alkalinity"), in.number("startPh"), in.number("targetPh"), in.number("liters"), *acid,
        in.number("strength"));
    Output out;
    out.results["acidMmolPerLiter"] = sparge.acidMmolPerLiter;
    out.results["pureGrams"] = sparge.pureGrams;
    out.results["solutionGrams"] = sparge.solutionGrams.value_or(sparge.pureGrams);
    out.results["milliliters"] = sparge.milliliters.value_or(0.0);
    out.notes.push_back("Titration. It adds 0.01 mmol/L to every sparge acid requirement.");
    return out;
}

Output infusion(const Reader& in) {
    const double grainKg = in.number("grainKilograms");
    const double grainC = in.number("grainTemperature");
    const double mashC = in.number("mashTemperature");
    const double litersPerKg = in.number("waterToGrain");
    require(grainKg > 0, "the grain weight must be above zero");
    require(litersPerKg > 0, "the water-to-grain ratio must be above zero");
    require(mashC > grainC, "the mash temperature must be above the grain temperature");

    // The thermal balance is written in quarts per pound.
    const double quartsPerPound = litersPerKg * kKilogramsPerPound / kLitersPerQuart;
    const double strike = mashC + (0.2 / quartsPerPound) * (mashC - grainC);

    Output out;
    out.results["strikeTemperature"] = strike;
    out.results["waterLiters"] = grainKg * litersPerKg;
    out.results["quartsPerPound"] = quartsPerPound;
    if (strike > 100) {
        out.notes.push_back("The strike water would be above boiling. Use a lower water-to-grain ratio or a lower mash temperature.");
    }
    out.notes.push_back("Heat lost to the tun is not included.");
    return out;
}

Output decoction(const Reader& in) {
    const double liters = in.number("mashLiters");
    const double fromC = in.number("currentTemperature");
    const double toC = in.number("targetTemperature");
    require(liters > 0, "the mash volume must be above zero");
    require(fromC >= 0 && toC > fromC && toC < 100,
            "the target temperature must be above the mash temperature and below boiling");

    // The decoction leaves the mash at its temperature, boils to 100 C, and goes back in.
    Output out;
    out.results["decoctionLiters"] = liters * (toC - fromC) / (100.0 - fromC);
    out.notes.push_back("Assumes the decoction boils at 100 C and that no heat is lost.");
    return out;
}

Output mashTemperature(const Reader& in) {
    const double liters = in.number("mashLiters");
    const double mashC = in.number("mashTemperature");
    const double targetC = in.number("targetTemperature");
    const double waterC = in.number("waterTemperature");
    require(liters > 0, "the mash volume must be above zero");
    require(waterC != targetC, "the added water must differ in temperature from the target");

    // Equal heat capacities: mash * (target - mash) = water * (water - target), in sign-consistent form.
    const double water = liters * (targetC - mashC) / (waterC - targetC);
    require(water >= 0, "water at that temperature cannot bring the mash to the target temperature");

    Output out;
    out.results["waterLiters"] = water;
    out.results["totalLiters"] = liters + water;
    out.notes.push_back("Assumes the water and the mash have the same heat capacity, and that no heat is lost.");
    return out;
}

Output mashAlkalinity(const Reader& in) {
    Ions ions;
    ions.bicarbonate = in.number("bicarbonate");
    ions.calcium = in.number("calcium");
    ions.magnesium = in.number("magnesium");

    Output out;
    out.results["residualAlkalinity"] = residualAlkalinity(ions);
    out.notes.push_back(
        "Residual alkalinity drives the mash pH, but this tool does not predict the pH itself. "
        "A pH estimate needs the grist's acidity, which the engine does not model yet.");
    return out;
}

// ---- Yeast and fermentation ----

Output yeastStarter(const Reader& in) {
    StarterInputs inputs;
    inputs.lager = in.flag("lager");
    inputs.packCellsBillion = in.number("packCellsBillion");
    inputs.growthBillionPerLiterPlato = in.number("growth");
    inputs.starterLiters = in.number("starterLiters");
    inputs.starterGravity = in.number("starterGravity");
    inputs.twoStage = in.flag("twoStage");
    inputs.firstStageLiters = in.number("firstStageLiters");

    const StarterPlan plan = planStarter(in.number("batchLiters"), in.number("originalGravity"), inputs);

    Output out;
    out.results["cellsNeededBillion"] = plan.cellsNeededBillion;
    out.results["packsWithoutStarter"] = plan.packsWithoutStarter;
    out.results["cellsWithoutStarterBillion"] = plan.cellsWithoutStarterBillion;
    out.results["oneStageLiters"] = plan.oneStageLiters;
    out.results["twoStageFirstLiters"] = plan.twoStageFirstLiters;
    out.results["twoStageSecondLiters"] = plan.twoStageSecondLiters;
    out.results["dryMaltGrams"] = plan.dryMaltGrams;
    out.results["dryMaltOunces"] = plan.dryMaltOunces;
    out.results["cellsWithStarterBillion"] = plan.cellsWithStarterBillion;
    out.notes = plan.notes;
    return out;
}

Output hydrometer(const Reader& in) {
    const double reading = in.number("reading");
    const double sampleC = in.number("sampleTemperature");
    const double calibrationC = in.number("calibrationTemperature");
    require(reading > 0, "the hydrometer reading must be above zero");
    require(sampleC >= 0 && sampleC <= 100 && calibrationC >= 0 && calibrationC <= 100,
            "the temperatures must be between 0 and 100 C");

    // A hydrometer reads the sample's density against water at its calibration temperature.
    Output out;
    out.results["correctedGravity"] = reading * waterDensity(calibrationC) / waterDensity(sampleC);
    out.notes.push_back("Uses water's density for the correction, which is close for wort but not exact.");
    return out;
}

Output refractometer(const Reader& in) {
    const double brix = in.number("brix");
    require(brix >= 0 && brix < kBallingScale / kBallingSlope, "the Brix reading is out of range");

    Output out;
    out.results["specificGravity"] = gravityIn(specificGravityFrom(brix, "brix"), "sg");
    out.notes.push_back(
        "This is for wort before fermentation. After fermentation, alcohol changes the reading, "
        "and no correction is applied here.");
    return out;
}

Output alcohol(const Reader& in) {
    const double og = in.number("originalGravity");
    const double fg = in.number("finalGravity");
    require(fg > 0 && og > fg, "the original gravity must be above the final gravity, and both above zero");

    Output out;
    out.results["standardAbv"] = (og - fg) * 131.25;
    out.results["advancedAbv"] = fermented::abvFromGravities(og, fg);
    out.notes.push_back("The standard formula is an approximation. The advanced formula is more accurate at higher gravities.");
    return out;
}

Output hopBitterness(const Reader& in) {
    const double alpha = in.number("alphaPercent");
    const double grams = in.number("grams");
    const double liters = in.number("postBoilLiters");
    const double minutes = in.number("boilMinutes");
    const double og = in.number("gravity");
    require(alpha >= 0 && grams >= 0, "the alpha acid and amount cannot be negative");
    require(liters > 0, "the post-boil volume must be above zero");
    require(minutes >= 0 && og >= 1, "the boil time cannot be negative, and the gravity must be at least 1");

    const double utilization = tinsethUtilization(og, minutes);
    Output out;
    out.results["utilizationPercent"] = utilization * 100.0;
    out.results["ibu"] = utilization * (alpha / 100.0) * grams * 1000.0 / liters;
    out.notes.push_back("Tinseth. Uses the post-boil volume. Hop form (pellet, whole, extract) is not corrected.");
    return out;
}

Output hopAge(const Reader& in) {
    const double alpha = in.number("alphaPercent");
    const double months = in.number("months");
    const double celsius = in.number("storageTemperature");
    const double annualLoss = in.number("annualLossPercent");
    require(alpha >= 0 && months >= 0, "the alpha acid and age cannot be negative");
    require(annualLoss >= 0 && annualLoss < 100, "the annual loss must be at least 0 and below 100");

    // The loss compounds each year at 20 C, and is assumed to double for every 10 C of storage.
    const double speed = std::pow(2.0, (celsius - 20.0) / 10.0);
    const double retention = std::pow(1.0 - annualLoss / 100.0, (months / 12.0) * speed);

    Output out;
    out.results["remainingAlphaPercent"] = alpha * retention;
    out.results["lostPercent"] = (1.0 - retention) * 100.0;
    out.notes.push_back(
        "The annual loss is an assumption you set, not a measurement. The doubling every 10 C is also assumed.");
    return out;
}

Output meadNutrient(const Reader& in) {
    const double liters = in.number("volumeLiters");
    const double target = in.number("targetYan");
    const double current = in.number("currentYan");
    require(liters > 0, "the volume must be above zero");
    require(target >= 0 && current >= 0, "the nitrogen values cannot be negative");

    const double grams = std::max(0.0, target - current) * liters / kNitrogenPerDapMgPerGram;
    Output out;
    out.results["dapGrams"] = grams;
    out.results["dapGramsPerLiter"] = grams / liters;
    out.notes.push_back("Diammonium phosphate (DAP) is 21 % nitrogen. The YAN target is yours to set.");
    return out;
}

Output backsweeten(const Reader& in) {
    const double liters = in.number("volumeLiters");
    const double current = in.number("currentGravity");
    const double target = in.number("targetGravity");
    const double yield = in.number("sugarYield");
    require(liters > 0 && current > 0 && target > 0, "the volume and gravities must be above zero");
    require(yield > 0, "the sugar yield must be above zero");

    const double points = (target - current) * 1000.0;
    require(points >= 0, "backsweetening raises the gravity, so the target cannot be below the current gravity");

    const double grams = points * liters / (fermented::kSucrosePointsPerGramPerLiter * yield);
    Output out;
    out.results["sugarGrams"] = grams;
    out.results["sugarGramsPerLiter"] = grams / liters;
    out.notes.push_back("Add the sugar only after the wine or mead is stable. Otherwise the yeast can ferment it.");
    out.notes.push_back("The sugar yield is relative to sucrose: 1.0 for sucrose, about 0.8 for honey.");
    return out;
}

Output sulfite(const Reader& in) {
    const double liters = in.number("volumeLiters");
    const double molecular = in.number("molecularSo2");
    const double pH = in.number("pH");
    require(liters > 0, "the volume must be above zero");
    require(molecular > 0, "the molecular SO2 target must be above zero");

    // Molecular SO2 is the active form. The free SO2 needed is the target divided by its fraction at this pH.
    const double fraction = fermented::molecularSo2Fraction(pH);
    const double freePpm = molecular / fraction;

    Output out;
    out.results["molecularFractionPercent"] = fraction * 100.0;
    out.results["freeSo2Ppm"] = freePpm;
    out.results["metabisulfiteGrams"] = fermented::metabisulfiteGramsPerLiterForSo2(freePpm) * liters;
    out.notes.push_back("The result is theoretical. Sulfite binds to the wine, so less free SO2 remains than added.");
    return out;
}

Output boilVolume(const Reader& in) {
    const double pre = in.number("preBoilLiters");
    const double minutes = in.number("boilMinutes");
    const double rate = in.number("evaporationPercentPerHour");
    require(pre > 0, "the pre-boil volume must be above zero");
    require(minutes >= 0 && rate >= 0, "the boil time and evaporation rate cannot be negative");

    const double evaporated = pre * (rate / 100.0) * (minutes / 60.0);
    require(evaporated < pre, "the boil would evaporate all the liquid");

    Output out;
    out.results["postBoilLiters"] = pre - evaporated;
    out.results["evaporatedLiters"] = evaporated;
    out.notes.push_back("Assumes the evaporation rate is a fixed share of the pre-boil volume per hour.");
    return out;
}

Output boilRate(const Reader& in) {
    const double pre = in.number("preBoilLiters");
    const double post = in.number("postBoilLiters");
    const double minutes = in.number("boilMinutes");
    require(pre > 0 && post > 0 && post <= pre, "the post-boil volume must be above zero and no more than the pre-boil volume");
    require(minutes > 0, "the boil time must be above zero");

    Output out;
    out.results["evaporationPercentPerHour"] = (1.0 - post / pre) * 100.0 * 60.0 / minutes;
    return out;
}

// ---- Dilution and gravity ----

Output dilution(const Reader& in) {
    const double liters = in.number("volumeLiters");
    const double current = in.number("currentValue");
    const double target = in.number("targetValue");
    require(liters > 0, "the volume must be above zero");
    require(target > 0 && current >= target, "the target must be above zero and no more than the current value");

    // The concentration times the volume is constant, so the final volume is V * current / target.
    Output out;
    out.results["waterLiters"] = liters * (current / target - 1.0);
    out.results["finalLiters"] = liters * current / target;
    out.notes.push_back("Assumes the volumes add exactly. Ethanol contraction, which makes the mix slightly smaller, is ignored.");
    return out;
}

Output gravityAdjustment(const Reader& in) {
    const double liters = in.number("volumeLiters");
    const double current = in.number("currentGravity");
    const double target = in.number("targetGravity");
    const double yield = in.number("sugarYield");
    require(liters > 0, "the volume must be above zero");
    require(current > 1 && target > 1, "the gravities must be above 1");
    require(yield > 0, "the sugar yield must be above zero");

    // Gravity points are proportional to the extract in the liquid.
    const double pointsNow = (current - 1.0) * 1000.0;
    const double pointsTarget = (target - 1.0) * 1000.0;

    Output out;
    out.results["sugarGrams"] = pointsTarget > pointsNow
                                    ? (pointsTarget - pointsNow) * liters / (fermented::kSucrosePointsPerGramPerLiter * yield)
                                    : 0.0;
    out.results["waterLiters"] = pointsTarget < pointsNow ? liters * (pointsNow / pointsTarget - 1.0) : 0.0;
    out.notes.push_back("Sugar raises the gravity with almost no change in volume. Water lowers it by diluting.");
    return out;
}

// ---- Weight and volume ----

Output weightFromVolume(const Reader& in) {
    const double density = in.number("density");
    const double milliliters = in.number("milliliters");
    require(density > 0 && milliliters >= 0, "the density must be above zero, and the volume cannot be negative");
    Output out;
    out.results["grams"] = density * milliliters;
    return out;
}

Output volumeFromWeight(const Reader& in) {
    const double density = in.number("density");
    const double grams = in.number("grams");
    require(density > 0 && grams >= 0, "the density must be above zero, and the weight cannot be negative");
    Output out;
    out.results["milliliters"] = grams / density;
    return out;
}

// ---- Carbonation ----

Output carbonationPressure(const Reader& in) {
    const double volumes = in.number("volumes");
    const double celsius = in.number("temperature");
    require(volumes > 0, "the volumes must be above zero");
    require(celsius >= 0 && celsius <= 50, "the temperature must be between 0 and 50 C");

    const double atmospheres = volumes / co2VolumesPerAtmosphere(celsius);
    Output out;
    out.results["gaugePsi"] = (atmospheres - 1.0) * kPsiPerAtmosphere;
    out.results["gaugeKilopascals"] = (atmospheres - 1.0) * kKilopascalsPerAtmosphere;
    out.notes.push_back("Henry's law for pure water. Beer differs by a psi or so, so check the result with a keg gauge.");
    if (atmospheres < 1.0) {
        out.notes.push_back("A negative gauge pressure means the beer holds this much gas with no pressure applied.");
    }
    return out;
}

Output carbonationVolumes(const Reader& in) {
    const double psi = in.number("gaugePsi");
    const double celsius = in.number("temperature");
    require(celsius >= 0 && celsius <= 50, "the temperature must be between 0 and 50 C");
    require(psi > -kPsiPerAtmosphere, "the gauge pressure cannot be below a vacuum");

    Output out;
    out.results["volumes"] = co2VolumesPerAtmosphere(celsius) * (psi / kPsiPerAtmosphere + 1.0);
    out.notes.push_back("Henry's law for pure water. Beer differs by a little, so check the result with a keg gauge.");
    return out;
}

Output carbonationPriming(const Reader& in) {
    const double liters = in.number("volumeLiters");
    const double target = in.number("targetVolumes");
    const double residual = in.number("residualVolumes");
    require(liters > 0, "the volume must be above zero");
    require(target >= 0 && residual >= 0, "the volumes cannot be negative");

    // Dextrose gives 2 CO2 per glucose, and sucrose gives 4 CO2 per sucrose.
    const double dextrose = sugarGramsPerVolume(2.0 * kCo2MolarMass / kGlucoseMolarMass);
    const double sucrose = sugarGramsPerVolume(4.0 * kCo2MolarMass / kSucroseMolarMass);
    const double needed = std::max(0.0, target - residual) * liters;

    Output out;
    out.results["dextroseGrams"] = needed * dextrose;
    out.results["sucroseGrams"] = needed * sucrose;
    out.notes.push_back("Assumes all the sugar ferments to CO2. Real yeast leaves a little unfermented.");
    return out;
}

// ---- Unit converters ----

// Temperature, weight, and volume convert exactly through Quantity.
Output convertExact(const Reader& in) {
    const auto from = unitFromBeerJson(in.text("from"));
    const auto to = unitFromBeerJson(in.text("to"));
    require(from.has_value(), "unknown unit \"" + in.text("from") + "\"");
    require(to.has_value(), "unknown unit \"" + in.text("to") + "\"");

    const Quantity value = Quantity::fromUnit(Rational::fromDecimal(in.text("value")), *from);
    Output out;
    out.results["value"] = value.valueIn(*to).toDouble();
    return out;
}

Output convertGravity(const Reader& in) {
    const double value = in.number("value");
    const std::string& from = in.text("from");
    const std::string& to = in.text("to");
    Output out;
    out.results["value"] = gravityIn(specificGravityFrom(value, from), to);
    out.notes.push_back("Brix and Plato are treated as the same sucrose scale.");
    return out;
}

Output convertPressure(const Reader& in) {
    const double value = in.number("value");
    Output out;
    out.results["value"] = value * pascalsPer(in.text("from")) / pascalsPer(in.text("to"));
    return out;
}

struct Entry {
    const char* name;
    Output (*run)(const Reader&);
};

const Entry kTools[] = {
    {"water.profile", waterProfile},
    {"brew.mash", brewMash},
    {"brew.sparge", brewSparge},
    {"water.infusion", infusion},
    {"mash.decoction", decoction},
    {"mash.temperature", mashTemperature},
    {"mash.alkalinity", mashAlkalinity},
    {"yeast.starter", yeastStarter},
    {"gravity.hydrometer", hydrometer},
    {"gravity.refractometer", refractometer},
    {"alcohol.gravities", alcohol},
    {"hop.bitterness", hopBitterness},
    {"hop.age", hopAge},
    {"mead.nutrient", meadNutrient},
    {"sweeten.backsweeten", backsweeten},
    {"sweeten.sulfite", sulfite},
    {"boil.volume", boilVolume},
    {"boil.rate", boilRate},
    {"dilute.dilution", dilution},
    {"gravity.adjust", gravityAdjustment},
    {"weight.mass", weightFromVolume},
    {"weight.volume", volumeFromWeight},
    {"carbonation.pressure", carbonationPressure},
    {"carbonation.volumes", carbonationVolumes},
    {"carbonation.priming", carbonationPriming},
    {"convert.temperature", convertExact},
    {"convert.weight", convertExact},
    {"convert.volume", convertExact},
    {"convert.gravity", convertGravity},
    {"convert.pressure", convertPressure},
};

}  // namespace

Output run(const std::string& tool, const Inputs& inputs) {
    const Reader reader(inputs);
    for (const Entry& entry : kTools) {
        if (tool == entry.name) {
            return entry.run(reader);
        }
    }
    throw std::invalid_argument("unknown tool \"" + tool + "\"");
}

}  // namespace kettlelogic::calc::tools
