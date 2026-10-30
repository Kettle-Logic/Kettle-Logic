#include "kettlelogic/calc/wine.hpp"

#include "kettlelogic/calc/fermented.hpp"

#include <stdexcept>

namespace kettlelogic::calc::wine {

double sugarGramsPerLiter(double brix, double mustSg) {
    if (brix < 0 || !(mustSg > 0)) {
        throw std::invalid_argument("the Brix must not be negative, and the specific gravity must be positive");
    }
    return 10.0 * brix * mustSg;
}

double potentialAbv(double brix, double mustSg, double efficiency) {
    return fermented::abvFromHexose(sugarGramsPerLiter(brix, mustSg), efficiency);
}

double sucroseGramsForAbv(double targetAbv, double liters, double efficiency) {
    if (!(liters > 0)) {
        throw std::invalid_argument("the must volume must be positive");
    }
    return fermented::hexoseGramsPerLiterForAbv(targetAbv, efficiency) * fermented::kSucroseGramsPerHexoseGram * liters;
}

double acidGramsToRaise(double currentTa, double targetTa, double liters) {
    if (!(liters > 0)) {
        throw std::invalid_argument("the wine volume must be positive");
    }
    if (targetTa < currentTa) {
        throw std::invalid_argument("the target acidity is below the current acidity; use deacidification instead");
    }
    return (targetTa - currentTa) * liters;
}

}  // namespace kettlelogic::calc::wine
