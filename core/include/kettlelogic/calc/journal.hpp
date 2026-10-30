#pragma once

#include "kettlelogic/model/model.hpp"

#include <optional>

// The brew journal's calculations: what a brew session expected, and what it measured.
// Each measured value is optional, so a session can be filled in as the brew goes.
//
// The expected values come from the recipe's calculations. The measured efficiencies
// are the potential extract of the recipe, scaled by the volume and gravity measured.
// Constants and their sources are in docs/calc.md, "Brew journal".
namespace kettlelogic::calc {

struct JournalInputs {
    std::optional<double> postMashGravity;      // measured, specific gravity
    std::optional<double> preBoilLiters;        // measured
    std::optional<double> preBoilGravity;       // measured
    std::optional<double> batchLiters;          // measured, into the fermenter
    std::optional<double> originalGravity;      // measured
    std::optional<double> finalGravity;         // measured
    std::optional<double> bottlingLiters;       // measured
    std::optional<double> carbonationVolumes;   // the target CO2, in volumes
    std::optional<double> carbonationTempC;     // the temperature the beer is held at, for force carbonation
};

struct JournalResults {
    // What the recipe expects.
    std::optional<double> expectedBrewhouseEfficiency;  // percent, from the recipe
    std::optional<double> expectedPreBoilGravity;       // specific gravity, from the batch and efficiency
    std::optional<double> expectedOriginalGravity;
    std::optional<double> expectedFinalGravity;
    std::optional<double> expectedAbv;                  // percent by volume
    std::optional<double> expectedAttenuation;          // percent
    std::optional<double> expectedCaloriesPer12Oz;

    // What the session measured, worked out from the measurements.
    std::optional<double> measuredMashEfficiency;       // percent, from the post-mash gravity
    std::optional<double> measuredBrewhouseEfficiency;  // percent, from the original gravity and batch
    std::optional<double> measuredAbv;
    std::optional<double> measuredAttenuation;
    std::optional<double> measuredCaloriesPer12Oz;

    // Carbonation. Priming sugar is dextrose, or corn sugar.
    std::optional<double> primingSugarGramsPerLiter;
    std::optional<double> primingSugarGrams;            // for the bottling volume, measured if given

    // Force carbonation: the gauge pressure, in psi, that holds the target volumes at the temperature.
    std::optional<double> forceCarbonationPsi;
};

JournalResults planJournal(const model::Recipe& recipe, const JournalInputs& inputs);

}  // namespace kettlelogic::calc
