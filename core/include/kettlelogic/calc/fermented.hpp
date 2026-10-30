#pragma once

// Conversions shared by the cider, mead, and wine engines: sugar to gravity,
// sugar to alcohol, sulfite, and molecular SO2. The inputs are plain numbers in
// the units named on each function. Formulas and sources are in docs/calc.md.
// Functions throw std::invalid_argument for an input outside its range.
namespace kettlelogic::calc::fermented {

// The points of gravity that 1 g of sucrose per liter adds (46.214 points per
// pound per gallon, converted to grams per liter).
inline constexpr double kSucrosePointsPerGramPerLiter = 46.214 * 0.001 * 2.20462262 * 3.785411784;

// Ethanol per gram of hexose sugar, from C6H12O6 -> 2 C2H5OH + 2 CO2 (molar masses 180.156 and 46.069).
inline constexpr double kEthanolGramsPerHexoseGram = 2.0 * 46.069 / 180.156;

// Ethanol's density at 20 C, in g/mL.
inline constexpr double kEthanolDensity = 0.789;

// Sucrose gives 1.0526 g of hexose per gram, so 0.95 g of sucrose per gram of hexose.
inline constexpr double kSucroseGramsPerHexoseGram = 0.95;

// Potassium metabisulfite is 57.6 % SO2 by mass, so 1 g/L gives 576 mg/L of SO2.
inline constexpr double kSo2MgPerLiterPerMetabisulfiteGramPerLiter = 576.0;

// The pKa of SO2 at 20 C, for the molecular fraction.
inline constexpr double kSo2Pka = 1.81;

// The original gravity from points: 1 + points / 1000.
double gravityFromPoints(double points);

// Points from a sugar mass per liter of wort. `yield` is the sugar's yield
// relative to sucrose (1.0 for sucrose).
double pointsFromGramsPerLiter(double gramsPerLiter, double yield);

// The final gravity from the original gravity and the attenuation (a fraction).
double finalGravity(double originalGravity, double attenuation);

// Alcohol by volume, in percent, from the original and final gravity, by the
// advanced formula (76.08 * (OG - FG) / (1.775 - OG)) * (FG / 0.794).
double abvFromGravities(double originalGravity, double finalGravity);

// Alcohol by volume, in percent, from hexose sugar (glucose or fructose) per
// liter. `efficiency` is the fraction of the sugar that becomes ethanol; 1.0
// is the stoichiometric ceiling, which real fermentations do not reach.
double abvFromHexose(double hexoseGramsPerLiter, double efficiency);

// The inverse: the hexose per liter that gives `abv` percent by volume.
double hexoseGramsPerLiterForAbv(double abv, double efficiency);

// Potassium metabisulfite, in grams per liter, to add `ppm` of SO2. The sulfite
// binds to the wine, so this is the theoretical amount.
double metabisulfiteGramsPerLiterForSo2(double ppm);

// The fraction of free SO2 that is molecular, which is the active form, at `ph`.
double molecularSo2Fraction(double ph);

}  // namespace kettlelogic::calc::fermented
