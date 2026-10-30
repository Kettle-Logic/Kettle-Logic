#pragma once

// Cider: the sugar that brings juice to a target gravity, and the gravity that
// sugar gives. Sulfite and molecular SO2 are in fermented.hpp.
namespace kettlelogic::calc::cider {

// Sucrose, in grams, that raises `liters` of juice from `currentSg` to `targetSg`.
// Throws if the target is below the current gravity.
double sucroseGramsToReachGravity(double currentSg, double targetSg, double liters);

// The gravity of `liters` of juice after `sucroseGrams` of sucrose is dissolved.
double gravityAfterSucrose(double currentSg, double sucroseGrams, double liters);

}  // namespace kettlelogic::calc::cider
