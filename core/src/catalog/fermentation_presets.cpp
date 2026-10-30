#include "kettlelogic/catalog/fermentation_presets.hpp"

namespace kettlelogic::catalog {

const std::vector<FermentationPreset>& fermentationPresets() {
    static const std::vector<FermentationPreset> presets = {
        {"Ale, one temperature", {{14, 18}}},
        {"Ale, with a crash", {{3, 18}, {7, 20}, {2, 2}}},
        {"Lager", {{14, 10}, {28, 4}, {3, 1}}},
        {"Saison, rising", {{1, 22}, {3, 26}, {4, 30}}},
        {"Kveik, warm", {{3, 35}, {4, 30}}},
    };
    return presets;
}

}  // namespace kettlelogic::catalog
