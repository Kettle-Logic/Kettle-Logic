#include "kettlelogic/calc/journal.hpp"

#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/quantity.hpp"

#include <cmath>
#include <stdexcept>

namespace kettlelogic::calc {

namespace {

constexpr double kLitersPerGallon = 3.785411784;
// Dextrose gives 0.489 g of CO2 per gram, and one liter of CO2 at standard conditions is 1.977 g.
// About 70 % of the CO2 a priming sugar makes stays in the beer. Together these give about 2.8 g of
// corn sugar per liter for each volume of CO2, which matches the common priming charts.
constexpr double kDextroseCo2PerGram = 0.489;
constexpr double kCo2GramsPerLiter = 1.977;
constexpr double kRetainedFraction = 0.70;

// Degrees Plato from gravity, by the usual linear relation.
double plato(double specificGravity) {
    return 259.0 - 259.0 / specificGravity;
}

// Alcohol by volume. Above 1.070 the gravity drop times 131.25 falls short, so Hall's Balling
// form is used (Zymurgy, 1995).
double alcoholByVolume(double originalGravity, double finalGravity) {
    if (originalGravity > 1.070) {
        return 76.08 * (originalGravity - finalGravity) / (1.775 - originalGravity) * (finalGravity / 0.794);
    }
    return (originalGravity - finalGravity) * 131.25;
}

// Calories in a 12 fl oz (355 mL) serving, from the Oxford Companion to Beer form:
// 3.55 × (6.9 × alcohol by weight + 4 × (real extract − 0.1)). Real extract is in degrees Plato,
// from the original and final gravities. Alcohol by weight is 0.79 × ABV / final gravity.
double caloriesPer12Oz(double originalGravity, double finalGravity, double abvPercent) {
    const double realExtract = 0.1808 * plato(originalGravity) + 0.8192 * plato(finalGravity);
    const double alcoholByWeight = 0.79 * abvPercent / finalGravity;
    return 3.55 * (6.9 * alcoholByWeight + 4.0 * (realExtract - 0.1));
}

// The gauge pressure, in psi, that holds `volumes` of CO2 in beer at `celsius`. From a carbonation
// equation published in a patent on beer carbonation (temperature in °F):
// P = C × (0.0013493 T² + 0.094214 T + 4.81904) − 14.7.
double forcePsi(double volumes, double celsius) {
    const double fahrenheit = celsius * 9.0 / 5.0 + 32.0;
    return volumes * (0.0013493 * fahrenheit * fahrenheit + 0.094214 * fahrenheit + 4.81904) - 14.7;
}

}  // namespace

JournalResults planJournal(const model::Recipe& recipe, const JournalInputs& in) {
    JournalResults out;
    const Results results = calculate(recipe);

    const double batchLiters = recipe.batchSize.valueIn(Unit::Liter).toDouble();
    const double batchGallons = batchLiters / kLitersPerGallon;
    const double expectedEfficiency = recipe.efficiency.brewhouse.valueIn(Unit::Percent).toDouble();
    out.expectedBrewhouseEfficiency = expectedEfficiency;

    if (results.originalGravity && expectedEfficiency > 0) {
        const double og = *results.originalGravity;
        // The points the grain gives, at the recipe's efficiency. The potential is the same at 100 %.
        const double points = (og - 1.0) * 1000.0 * batchGallons;
        const double potential = points / (expectedEfficiency / 100.0);
        out.expectedOriginalGravity = og;

        if (results.preBoilLiters && *results.preBoilLiters > 0) {
            const double preBoilGallons = *results.preBoilLiters / kLitersPerGallon;
            out.expectedPreBoilGravity = 1.0 + points / (preBoilGallons * 1000.0);
        }

        // The measured efficiencies: the points measured, against the potential of the grain.
        if (in.originalGravity && potential > 0) {
            const double measuredBatchGallons = in.batchLiters.value_or(batchLiters) / kLitersPerGallon;
            out.measuredBrewhouseEfficiency =
                (*in.originalGravity - 1.0) * 1000.0 * measuredBatchGallons / potential * 100.0;
        }
        if (in.postMashGravity && potential > 0) {
            const double preBoilGallons =
                in.preBoilLiters.value_or(results.preBoilLiters.value_or(batchLiters)) / kLitersPerGallon;
            out.measuredMashEfficiency = (*in.postMashGravity - 1.0) * 1000.0 * preBoilGallons / potential * 100.0;
        }
    }

    if (results.originalGravity && results.finalGravity) {
        const double og = *results.originalGravity;
        const double fg = *results.finalGravity;
        out.expectedFinalGravity = fg;
        out.expectedAbv = alcoholByVolume(og, fg);
        out.expectedAttenuation = (og - fg) / (og - 1.0) * 100.0;
        out.expectedCaloriesPer12Oz = caloriesPer12Oz(og, fg, *out.expectedAbv);
    }

    if (in.originalGravity && in.finalGravity) {
        const double og = *in.originalGravity;
        const double fg = *in.finalGravity;
        if (og > 1.0 && fg <= og) {
            out.measuredAbv = alcoholByVolume(og, fg);
            out.measuredAttenuation = (og - fg) / (og - 1.0) * 100.0;
            out.measuredCaloriesPer12Oz = caloriesPer12Oz(og, fg, *out.measuredAbv);
        }
    }

    if (in.carbonationVolumes && *in.carbonationVolumes >= 0) {
        const double gramsPerLiter = *in.carbonationVolumes * kCo2GramsPerLiter / kDextroseCo2PerGram / kRetainedFraction;
        out.primingSugarGramsPerLiter = gramsPerLiter;
        out.primingSugarGrams = gramsPerLiter * in.bottlingLiters.value_or(batchLiters);
    }
    if (in.carbonationVolumes && in.carbonationTempC) {
        out.forceCarbonationPsi = forcePsi(*in.carbonationVolumes, *in.carbonationTempC);
    }
    return out;
}

}  // namespace kettlelogic::calc
