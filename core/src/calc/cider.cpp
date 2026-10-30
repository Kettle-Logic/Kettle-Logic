#include "kettlelogic/calc/cider.hpp"

#include "kettlelogic/calc/fermented.hpp"

#include <stdexcept>

namespace kettlelogic::calc::cider {

double sucroseGramsToReachGravity(double currentSg, double targetSg, double liters) {
    if (!(liters > 0)) {
        throw std::invalid_argument("the juice volume must be positive");
    }
    if (targetSg < currentSg) {
        throw std::invalid_argument("the target gravity is below the juice's gravity; sugar cannot lower it");
    }
    const double points = (targetSg - currentSg) * 1000.0;
    return points / fermented::kSucrosePointsPerGramPerLiter * liters;
}

double gravityAfterSucrose(double currentSg, double sucroseGrams, double liters) {
    if (!(liters > 0)) {
        throw std::invalid_argument("the juice volume must be positive");
    }
    if (sucroseGrams < 0) {
        throw std::invalid_argument("the sugar must not be negative");
    }
    const double points = sucroseGrams / liters * fermented::kSucrosePointsPerGramPerLiter;
    return currentSg + points / 1000.0;
}

}  // namespace kettlelogic::calc::cider
