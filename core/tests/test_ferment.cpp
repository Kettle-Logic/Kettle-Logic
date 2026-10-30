#include "check.hpp"

#include "kettlelogic/calc/cider.hpp"
#include "kettlelogic/calc/fermented.hpp"
#include "kettlelogic/calc/mead.hpp"
#include "kettlelogic/calc/wine.hpp"

#include <cmath>

namespace kettlelogic::test {

namespace {

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// Expected values are from an independent Python computation of the same formulas.

// 1 % ABV needs 15.43 g/L of hexose at full efficiency, so 12 % needs 185.13 g/L.
void testHexoseAndAlcohol() {
    using namespace calc::fermented;
    CHECK(near(hexoseGramsPerLiterForAbv(12, 1.0), 185.126, 0.01));
    CHECK(near(abvFromHexose(185.126, 1.0), 12.0, 1e-3));
    CHECK(near(abvFromHexose(264, 1.0), 17.1126, 0.001));
    CHECK(near(abvFromHexose(264, 0.92), 15.7436, 0.001));
}

// Advanced ABV: OG 1.100, FG 1.000 gives 14.195 % v/v.
void testAdvancedAbv() {
    CHECK(near(calc::fermented::abvFromGravities(1.100, 1.000), 14.1954, 0.001));
    CHECK(near(calc::fermented::finalGravity(1.100, 0.75), 1.025, 1e-12));
}

// Sulfite: 50 mg/L of SO2 is 50 / 576 = 0.0868 g/L of potassium metabisulfite.
// At pH 3.5 the molecular fraction is 1 / (1 + 10^(3.5 - 1.81)) = 2.0 %.
void testSulfite() {
    CHECK(near(calc::fermented::metabisulfiteGramsPerLiterForSo2(50), 0.086806, 1e-5));
    CHECK(near(calc::fermented::molecularSo2Fraction(3.5), 0.020009, 1e-5));
}

// Cider: 100 points of sugar in 1 L is 100 / 0.38567 = 259.29 g of sucrose.
void testCiderSugar() {
    CHECK(near(calc::cider::sucroseGramsToReachGravity(1.000, 1.100, 1.0), 259.286, 0.01));
    CHECK(near(calc::cider::sucroseGramsToReachGravity(1.040, 1.100, 2.0), 2 * 259.286 * 0.6, 0.02));
    const double back = calc::cider::gravityAfterSucrose(1.000, 259.286, 1.0);
    CHECK(near(back, 1.100, 1e-5));
}

// Mead: 1 L at 1.100 needs 100 / (0.38567 * 0.80) = 324.11 g of honey.
void testMeadHoney() {
    CHECK(near(calc::mead::honeyGramsForGravity(1.100, 1.0), 324.107, 0.01));
    CHECK(near(calc::mead::originalGravity(324.107, 1.0), 1.100, 1e-5));
}

// Mead by alcohol: 12 % at attenuation 0.75 needs OG 1.10838, which is 351.26 g of honey per liter.
// The gravity the honey gives must, in turn, give the target alcohol.
void testMeadByAlcohol() {
    const double honey = calc::mead::honeyGramsForAbv(12.0, 1.0, 0.75);
    CHECK(near(honey, 351.26, 0.05));
    const double og = calc::mead::originalGravity(honey, 1.0);
    const double fg = calc::fermented::finalGravity(og, 0.75);
    CHECK(near(calc::fermented::abvFromGravities(og, fg), 12.0, 1e-4));
}

// Wine: a must of 24 Brix and SG 1.10 holds 264 g/L of sugar. At full efficiency it
// is 17.11 % ABV, and at 92 % efficiency, 15.74 %.
void testWinePotentialAlcohol() {
    CHECK(near(calc::wine::sugarGramsPerLiter(24, 1.10), 264.0, 1e-9));
    CHECK(near(calc::wine::potentialAbv(24, 1.10, 1.0), 17.1126, 0.001));
    CHECK(near(calc::wine::potentialAbv(24, 1.10, 0.92), 15.7436, 0.001));
}

// Chaptalization: 1 % ABV is 15.43 g/L of hexose, so 14.66 g/L of sucrose, and 1 L needs 14.66 g.
void testChaptalization() {
    CHECK(near(calc::wine::sucroseGramsForAbv(1.0, 1.0, 1.0), 14.6558, 0.001));
}

// Acidity: raising TA from 5.0 to 6.5 g/L in 1 L takes 1.5 g of acid.
void testAcidity() {
    CHECK(near(calc::wine::acidGramsToRaise(5.0, 6.5, 1.0), 1.5, 1e-9));
}

// Bad inputs are refused, not turned into numbers.
void testBadInputsAreRefused() {
    CHECK(throws([] { calc::fermented::abvFromHexose(10, 0); }));
    CHECK(throws([] { calc::fermented::finalGravity(1.05, 1.5); }));
    CHECK(throws([] { calc::fermented::molecularSo2Fraction(0); }));
    CHECK(throws([] { calc::cider::sucroseGramsToReachGravity(1.10, 1.05, 1.0); }));
    CHECK(throws([] { calc::mead::honeyGramsForGravity(1.0, 1.0); }));
    CHECK(throws([] { calc::mead::honeyGramsForAbv(90.0, 1.0, 0.8); }));
    CHECK(throws([] { calc::wine::acidGramsToRaise(6.0, 5.0, 1.0); }));
    CHECK(throws([] { calc::wine::sugarGramsPerLiter(-1, 1.0); }));
}

}  // namespace

void runFermentTests() {
    testHexoseAndAlcohol();
    testAdvancedAbv();
    testSulfite();
    testCiderSugar();
    testMeadHoney();
    testMeadByAlcohol();
    testWinePotentialAlcohol();
    testChaptalization();
    testAcidity();
    testBadInputsAreRefused();
}

}  // namespace kettlelogic::test
