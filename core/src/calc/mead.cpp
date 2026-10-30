#include "kettlelogic/calc/mead.hpp"

#include "kettlelogic/calc/fermented.hpp"

#include <stdexcept>

namespace kettlelogic::calc::mead {

namespace {

// The gravity range the search covers. Above 1.2 a must is beyond any honey
// mead, and the alcohol formula is only defined below 1.775.
constexpr double kLowestGravity = 1.000001;
constexpr double kHighestGravity = 1.2;

}  // namespace

double honeyGramsForGravity(double targetSg, double liters, double yield) {
    if (!(liters > 0)) {
        throw std::invalid_argument("the must volume must be positive");
    }
    if (!(targetSg > 1.0)) {
        throw std::invalid_argument("the target gravity must be above 1");
    }
    const double points = (targetSg - 1.0) * 1000.0;
    return points / fermented::pointsFromGramsPerLiter(1.0, yield) * liters;
}

double originalGravity(double honeyGrams, double liters, double yield) {
    if (!(liters > 0)) {
        throw std::invalid_argument("the must volume must be positive");
    }
    if (honeyGrams < 0) {
        throw std::invalid_argument("the honey must not be negative");
    }
    return fermented::gravityFromPoints(fermented::pointsFromGramsPerLiter(honeyGrams / liters, yield));
}

double honeyGramsForAbv(double targetAbv, double liters, double attenuation, double yield) {
    if (!(targetAbv > 0)) {
        throw std::invalid_argument("the target alcohol must be above 0");
    }
    // The alcohol rises with the original gravity, so bisect on it.
    const auto abvAt = [&](double og) {
        return fermented::abvFromGravities(og, fermented::finalGravity(og, attenuation));
    };
    if (targetAbv > abvAt(kHighestGravity)) {
        throw std::invalid_argument("the target alcohol is more than this honey can make");
    }
    double low = kLowestGravity;
    double high = kHighestGravity;
    for (int i = 0; i < 200; ++i) {
        const double middle = (low + high) / 2;
        (abvAt(middle) < targetAbv ? low : high) = middle;
    }
    return honeyGramsForGravity((low + high) / 2, liters, yield);
}

}  // namespace kettlelogic::calc::mead
