#pragma once

// Wine: the sugar in a must, the alcohol it gives, chaptalization (adding
// sugar to raise the alcohol), and titratable acid.
namespace kettlelogic::calc::wine {

// Sugar in grams per liter of must, from its Brix (grams of sucrose per 100 g)
// and its specific gravity: a must of Brix B and specific gravity S holds
// 10 * B * S grams of sugar per liter.
double sugarGramsPerLiter(double brix, double mustSg);

// Alcohol, in percent by volume, that a must can give from its Brix, at the
// given fermentation efficiency (1.0 is the theoretical ceiling).
double potentialAbv(double brix, double mustSg, double efficiency);

// Sucrose, in grams, that adds `targetAbv` percent by volume to `liters` of
// must at the given efficiency. Chaptalization.
double sucroseGramsForAbv(double targetAbv, double liters, double efficiency);

// Acid, in grams, that raises titratable acidity from `currentTa` to `targetTa`
// (both in g/L as tartaric acid) in `liters`. Throws if the target is lower.
double acidGramsToRaise(double currentTa, double targetTa, double liters);

}  // namespace kettlelogic::calc::wine
