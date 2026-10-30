#pragma once

#include <string>
#include <vector>

namespace kettlelogic::catalog {

// A step of a fermentation temperature profile: how many days, at what temperature.
struct FermentationStep {
    double days = 0;
    double tempC = 0;
};

// A fermentation temperature profile the user can pick, with its steps in order.
struct FermentationPreset {
    std::string name;
    std::vector<FermentationStep> steps;
};

const std::vector<FermentationPreset>& fermentationPresets();

}  // namespace kettlelogic::catalog
