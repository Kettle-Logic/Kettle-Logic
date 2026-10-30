#include "kettlelogic/calc/brew_water.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kettlelogic::calc::brew_water {
namespace {

constexpr double kPoundsPerKilogram = 1.0 / 0.45359237;

// Carbonic acid's dissociation constants at 25 °C
constexpr double kPk1Carbonic = 6.38;
constexpr double kPk2Carbonic = 10.33;

// The pH of the alkalinity end point, which the sparge titration works from.
constexpr double kAlkalinityEndPh = 4.3;

// The mash pH line: pH = 5.76 − 0.17 × (acid per liter − RA × q / 75).
constexpr double kMashPhIntercept = 5.76;
constexpr double kMashPhSlope = 0.17;
constexpr double kRaQuartsPerLbDivisor = 75.0;

// 50 mg/L as CaCO3 is one milliequivalent per liter.
constexpr double kCaCo3MgPerLiterPerMeq = 50.0;

double tenTo(double exponent) {
    return std::pow(10.0, exponent);
}

// The carbonate fractions at a pH: f1 is bicarbonate's share and f3 carbonate's, of the total.
struct CarbonateFractions {
    double f1;
    double f3;
};

CarbonateFractions carbonateFractions(double pH) {
    const double r1 = tenTo(pH - kPk1Carbonic);
    const double r2 = tenTo(pH - kPk2Carbonic);
    const double d = 1.0 + r1 + r1 * r2;
    return {1.0 / d, r1 * r2 / d};
}

// The sparge density polynomials, in g/L of the solution at `s` percent.
double polynomialDensity(const std::string& name, double s) {
    if (name == "acetic acid") {
        return 1000.0 * (-7.575e-10 * std::pow(s, 4) + 7.01e-8 * std::pow(s, 3) - 9.254e-6 * s * s + 1.575e-3 * s + 0.9979);
    }
    if (name == "hydrochloric acid") {
        return 1000.0 * (-3.1666e-7 * std::pow(s, 3) + 1.8499e-5 * s * s + 4.7666e-3 * s + 0.998);
    }
    if (name == "lactic acid") {
        return 1000.0 * (-5.6193e-10 * std::pow(s, 4) + 5.115e-8 * std::pow(s, 3) - 1.1408e-6 * s * s + 2.4529e-3 * s + 0.998);
    }
    if (name == "phosphoric acid") {
        return 1000.0 * (-3.9523e-9 * std::pow(s, 4) + 6.8571e-7 * std::pow(s, 3) + 5.4475e-7 * s * s + 5.51368e-3 * s + 0.99778);
    }
    if (name == "sulfuric acid") {
        return 1000.0 * (-2.0911e-8 * std::pow(s, 4) + 3.4312e-6 * std::pow(s, 3) - 1.3954e-4 * s * s + 9.0885e-3 * s + 0.9947);
    }
    throw std::invalid_argument("no density is known for \"" + name + "\"");
}

}  // namespace

ReportBalance balanceReport(const Ions& ions, const OtherIons& other) {
    ReportBalance b;
    b.cationsMeq = ions.calcium / 20.05 + ions.magnesium / 12.15 + ions.sodium / 23.0 + other.potassium / 39.1 +
                   other.iron / 28.0;
    b.anionsMeq = ions.bicarbonate / 61.0 + ions.carbonate / 30.0 + ions.sulfate / 48.0 + ions.chloride / 35.45 +
                  other.nitrate / 62.0 + other.nitrite / 46.0 + other.fluoride / 19.0;
    b.balanced = std::fabs(b.cationsMeq - b.anionsMeq) <= 0.5;
    return b;
}

Speciation speciesFromAlkalinity(double alkalinityCaCO3, double pH) {
    const double x = tenTo(pH - kPk2Carbonic);
    const double ratio = (alkalinityCaCO3 / 50.0) / (1.0 + 2.0 * x);
    return {ratio * 61.0, ratio * x * 60.0};
}

double lovibondFromEbc(double ebc) {
    // EBC to SRM is EBC / 1.97, and SRM to Lovibond is (SRM + 0.6) / 1.3.
    return (ebc / 1.97 + 0.6) / 1.3;
}

double maltAcidityMilliequivalents(const std::vector<Grain>& grist) {
    double total = 0;
    for (const Grain& grain : grist) {
        const double pounds = grain.kilograms * kPoundsPerKilogram;
        double perPound = 0;
        switch (grain.category) {
            case MaltCategory::Base: perPound = 0.28 * grain.lovibond; break;
            case MaltCategory::Crystal: perPound = 0.21 * grain.lovibond + 2.5; break;
            case MaltCategory::Roast: perPound = 38.0; break;
            case MaltCategory::Acid: perPound = 95.0; break;
        }
        total += pounds * perPound;
    }
    return total;
}

double mashPh(double maltAcidityMilliequivalents, double residualAlkalinity, double qtPerLb, double mashLiters) {
    if (!(mashLiters > 0)) {
        throw std::invalid_argument("the mash water must be above zero to find the mash pH");
    }
    const double acidPerLiter = maltAcidityMilliequivalents / mashLiters;
    const double value = kMashPhIntercept - kMashPhSlope * (acidPerLiter - residualAlkalinity * qtPerLb / kRaQuartsPerLbDivisor);
    // 0.1 for a result below zero, which no real mash reaches.
    return value < 0 ? 0.1 : value;
}

double residualAlkalinityForMashPh(double targetPh, double maltAcidityMilliequivalents, double qtPerLb,
                                   double mashLiters) {
    if (!(qtPerLb > 0) || !(mashLiters > 0)) {
        throw std::invalid_argument("the mash needs water and grain to find the residual alkalinity for a pH");
    }
    const double acidPerLiterAtTarget = (kMashPhIntercept - targetPh) / kMashPhSlope;
    return kRaQuartsPerLbDivisor * (maltAcidityMilliequivalents / mashLiters - acidPerLiterAtTarget) / qtPerLb;
}

Ions ionsPerGramOfSalt(const std::string& kind) {
    // Per gram per US gallon. A gallon is 3.785 liters, so milligrams per gram of salt
    // are the value times 3.785. Each salt row is (calcium, magnesium, sodium, sulfate, chloride, bicarbonate).
    constexpr double kLitersPerGallon = 3.785;
    Ions per;
    if (kind == "gypsum") {
        per.calcium = 61.5;
        per.sulfate = 147.43;
    } else if (kind == "calcium chloride") {
        per.calcium = 95.4;
        per.chloride = 168.79;
    } else if (kind == "epsom salt") {
        per.magnesium = 26.05;
        per.sulfate = 102.96;
    } else if (kind == "magnesium chloride") {
        per.magnesium = 31.57;
        per.chloride = 92.12;
    } else if (kind == "table salt") {
        per.sodium = 103.94;
        per.chloride = 160.26;
    } else if (kind == "baking soda") {
        per.sodium = 72.31;
        per.bicarbonate = 191.88;
    } else if (kind == "chalk") {
        per.calcium = 105.8;
        per.bicarbonate = 322.27;
    } else if (kind == "pickling lime") {
        per.calcium = 142.93;
        per.bicarbonate = 434.75;
    } else {
        throw std::invalid_argument("\"" + kind + "\" is not a water salt");
    }
    per.calcium *= kLitersPerGallon;
    per.magnesium *= kLitersPerGallon;
    per.sodium *= kLitersPerGallon;
    per.sulfate *= kLitersPerGallon;
    per.chloride *= kLitersPerGallon;
    per.bicarbonate *= kLitersPerGallon;
    return per;
}

Ions finishWater(const Ions& base, double roPercent, const Ions& ro, const std::vector<SaltGrams>& salts,
                 double liters, double bicarbonateFactor) {
    if (!(roPercent >= 0 && roPercent <= 100)) {
        throw std::invalid_argument("the RO share must be between 0 and 100 percent");
    }
    const double r = roPercent / 100.0;
    Ions ions;
    ions.calcium = base.calcium * (1 - r) + ro.calcium * r;
    ions.magnesium = base.magnesium * (1 - r) + ro.magnesium * r;
    ions.sodium = base.sodium * (1 - r) + ro.sodium * r;
    ions.sulfate = base.sulfate * (1 - r) + ro.sulfate * r;
    ions.chloride = base.chloride * (1 - r) + ro.chloride * r;
    ions.bicarbonate = base.bicarbonate * (1 - r) + ro.bicarbonate * r;
    ions.carbonate = base.carbonate * (1 - r) + ro.carbonate * r;
    for (const SaltGrams& salt : salts) {
        if (salt.grams == 0) {
            continue;
        }
        if (!(liters > 0)) {
            throw std::invalid_argument("the water volume must be above zero to add salts");
        }
        Ions per = ionsPerGramOfSalt(salt.kind);
        if (salt.kind == "baking soda") {
            per.bicarbonate *= bicarbonateFactor;
        }
        ions.calcium += per.calcium * salt.grams / liters;
        ions.magnesium += per.magnesium * salt.grams / liters;
        ions.sodium += per.sodium * salt.grams / liters;
        ions.sulfate += per.sulfate * salt.grams / liters;
        ions.chloride += per.chloride * salt.grams / liters;
        ions.bicarbonate += per.bicarbonate * salt.grams / liters;
    }
    return ions;
}

double bakingSodaBicarbonateFactor(double mashPh) {
    const double p = mashPh;
    const double fit = 0.0349 * p * p * p * p - 0.5697 * p * p * p + 3.4089 * p * p - 8.7207 * p + 8.8231;
    return 2.0 - fit;
}

const Acid* findAcid(const std::string& name) {
    // pKs and molar masses. The pK of 20 marks a dissociation that does not happen in the mash.
    static const Acid kAcids[] = {
        {"acetic acid", 4.75, 20, 20, 60.05, true},
        {"hydrochloric acid", -7, 20, 20, 36.46, true},
        {"lactic acid", 3.86, 20, 20, 90.08, true},
        {"phosphoric acid", 2.12, 7.2, 12.44, 98.0, true},
        {"sulfuric acid", -1, 1.92, 20, 98.07, true},
        {"citric acid", 3.14, 4.77, 6.39, 192.13, false},
        {"tartaric acid", 2.98, 4.34, 20, 150.09, false},
        {"malic acid", 3.4, 5.2, 20, 134.09, false},
    };
    for (const Acid& acid : kAcids) {
        if (name == acid.name) {
            return &acid;
        }
    }
    return nullptr;
}

double protonsPerMolecule(const Acid& acid, double pH) {
    const double r1 = tenTo(pH - acid.pK1);
    const double r2 = tenTo(pH - acid.pK2);
    const double r3 = tenTo(pH - acid.pK3);
    const double d = 1.0 + r1 + r1 * r2 + r1 * r2 * r3;
    return (r1 + 2.0 * r1 * r2 + 3.0 * r1 * r2 * r3) / d;
}

double solutionDensityGramsPerLiter(const Acid& acid, double strengthPercent) {
    if (!acid.liquid) {
        throw std::invalid_argument(std::string(acid.name) + " is a solid, so it is sold by weight");
    }
    return polynomialDensity(acid.name, strengthPercent);
}

double alkalinityRemovedByAcid(const Acid& acid, double grams, double strengthPercent, double liters, double pH) {
    if (!(liters > 0)) {
        throw std::invalid_argument("the water volume must be above zero");
    }
    const double pureGrams = grams * strengthPercent / 100.0;
    const double millimoles = pureGrams / acid.molarMass * 1000.0;
    const double milliequivalents = millimoles * protonsPerMolecule(acid, pH);
    return milliequivalents * kCaCo3MgPerLiterPerMeq / liters;
}

double acidGramsToRemoveAlkalinity(const Acid& acid, double alkalinityPpm, double strengthPercent, double liters,
                                   double pH) {
    if (!(strengthPercent > 0)) {
        throw std::invalid_argument("the acid strength must be above zero");
    }
    const double milliequivalents = alkalinityPpm * liters / kCaCo3MgPerLiterPerMeq;
    const double millimoles = milliequivalents / protonsPerMolecule(acid, pH);
    const double pureGrams = millimoles * acid.molarMass / 1000.0;
    return pureGrams / (strengthPercent / 100.0);
}

SpargeAcid spargeAcidification(double alkalinityCaCO3, double startPh, double targetPh, double liters,
                               const Acid& acid, double strengthPercent) {
    if (!(liters > 0)) {
        throw std::invalid_argument("the sparge volume must be above zero");
    }
    if (!(strengthPercent > 0)) {
        throw std::invalid_argument("the acid strength must be above zero");
    }

    // The carbonate of the water: total from the alkalinity between the start pH and the end point.
    const CarbonateFractions start = carbonateFractions(startPh);
    const CarbonateFractions end = carbonateFractions(kAlkalinityEndPh);
    const double total = (alkalinityCaCO3 / 50.0) / ((end.f1 - start.f1) + (start.f3 - end.f3));

    // The carbonate left at the target pH, which the acid must reach.
    const CarbonateFractions target = carbonateFractions(targetPh);
    const double required = total * ((target.f1 - start.f1) + (start.f3 - target.f3)) +
                            tenTo(-targetPh) - tenTo(-startPh) + kSpargeRequirementOffsetMmol;

    SpargeAcid result;
    result.acidMmolPerLiter = std::max(0.0, required / protonsPerMolecule(acid, targetPh));
    result.pureGrams = result.acidMmolPerLiter * liters * acid.molarMass / 1000.0;
    if (acid.liquid) {
        result.solutionGrams = result.pureGrams / (strengthPercent / 100.0);
        const double density = solutionDensityGramsPerLiter(acid, strengthPercent);
        result.milliliters = *result.solutionGrams / (density / 1000.0);
    }
    return result;
}

}  // namespace kettlelogic::calc::brew_water
