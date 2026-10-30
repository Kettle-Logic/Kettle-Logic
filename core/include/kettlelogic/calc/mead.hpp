#pragma once

// Mead: the honey that gives a must its gravity, and the alcohol it gives.
namespace kettlelogic::calc::mead {

// Honey's yield relative to sucrose. Honey is about 80 % sugar, so it gives
// about 80 % of sucrose's points. This is a typical value; measure your honey.
inline constexpr double kHoneyYield = 0.80;

// Honey, in grams, that gives `liters` of must the gravity `targetSg`.
double honeyGramsForGravity(double targetSg, double liters, double yield = kHoneyYield);

// The original gravity of `liters` of must made with `honeyGrams` of honey.
double originalGravity(double honeyGrams, double liters, double yield = kHoneyYield);

// Honey, in grams, that gives `targetAbv` percent by volume with the given
// attenuation (a fraction). Solved by bisection, so the alcohol and the
// gravity agree. Throws if the target cannot be reached.
double honeyGramsForAbv(double targetAbv, double liters, double attenuation, double yield = kHoneyYield);

}  // namespace kettlelogic::calc::mead
