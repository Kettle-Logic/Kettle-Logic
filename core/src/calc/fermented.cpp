#include "kettlelogic/calc/fermented.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace kettlelogic::calc::fermented {

namespace {

void requireFraction(double value, const char* what) {
    if (!(value > 0 && value <= 1)) {
        throw std::invalid_argument(std::string(what) + " must be a fraction above 0 and up to 1");
    }
}

}  // namespace

double gravityFromPoints(double points) {
    return 1.0 + points / 1000.0;
}

double pointsFromGramsPerLiter(double gramsPerLiter, double yield) {
    requireFraction(yield, "the sugar's yield");
    return gramsPerLiter * kSucrosePointsPerGramPerLiter * yield;
}

double finalGravity(double originalGravity, double attenuation) {
    requireFraction(attenuation, "the attenuation");
    return originalGravity - (originalGravity - 1.0) * attenuation;
}

double abvFromGravities(double originalGravity, double finalGravity) {
    if (!(originalGravity > finalGravity) || !(originalGravity < 1.775) || !(finalGravity > 0)) {
        throw std::invalid_argument("the gravities must satisfy 0 < FG < OG < 1.775");
    }
    return 76.08 * (originalGravity - finalGravity) / (1.775 - originalGravity) * (finalGravity / 0.794);
}

double abvFromHexose(double hexoseGramsPerLiter, double efficiency) {
    if (hexoseGramsPerLiter < 0) {
        throw std::invalid_argument("the sugar must not be negative");
    }
    requireFraction(efficiency, "the fermentation efficiency");
    // g/L of ethanol, then mL/L (divide by its density), then percent by volume (divide by 10).
    return hexoseGramsPerLiter * efficiency * kEthanolGramsPerHexoseGram / kEthanolDensity / 10.0;
}

double hexoseGramsPerLiterForAbv(double abv, double efficiency) {
    if (abv < 0) {
        throw std::invalid_argument("the alcohol must not be negative");
    }
    requireFraction(efficiency, "the fermentation efficiency");
    return abv * kEthanolDensity * 10.0 / (efficiency * kEthanolGramsPerHexoseGram);
}

double metabisulfiteGramsPerLiterForSo2(double ppm) {
    if (ppm < 0) {
        throw std::invalid_argument("the SO2 must not be negative");
    }
    return ppm / kSo2MgPerLiterPerMetabisulfiteGramPerLiter;
}

double molecularSo2Fraction(double ph) {
    if (!(ph > 0 && ph < 14)) {
        throw std::invalid_argument("the pH must be between 0 and 14");
    }
    return 1.0 / (1.0 + std::pow(10.0, ph - kSo2Pka));
}

}  // namespace kettlelogic::calc::fermented
