#include "check.hpp"

#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/calc/water.hpp"
#include "kettlelogic/model/beerjson.hpp"

#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace kettlelogic::test {

namespace {

using kettlelogic::Quantity;
using kettlelogic::Rational;
using kettlelogic::Unit;

std::string readSample(const char* name) {
    std::ifstream in(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// Expected values are worked by hand from the formulas in docs/calc.md, for the sample recipe:
//   OG: 10 lb at 80 % fine-grind (36.97 points/lb/gal), 72 % efficiency, 5 gal
//       points = 10 * 36.97 * 0.72 = 266.2; OG = 1 + 266.2 / 5000 = 1.05324
//   FG: final gravity 1.012 is given, so ABV = (1.05324 - 1.012) * 131.25 = 5.41
//   SRM: 2 Lovibond * 10 lb / 5 gal = 4 MCU; SRM = 1.4922 * 4^0.6859 = 3.86
//   Boil: 6.5 gal pre-boil, 60 min, 10 %/h evaporation: post-boil = 24.61 * 0.9 = 22.14 L
//   IBU: Tinseth, 60 min, 5.5 % alpha, 1 oz (28.35 g), 22.14 L post-boil, OG 1.05324
//        utilization = 1.65 * 0.000125^0.05324 * (1 - e^-2.4) / 4.15 = 0.2240
//        IBU = 0.2240 * 0.055 * 28.35 * 1000 / 22.14 = 15.78
void testSampleRecipe() {
    const model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    const calc::Results r = calc::calculate(doc.recipes[0]);

    CHECK(r.originalGravity && near(*r.originalGravity, 1.05324, 0.0001));
    CHECK(r.finalGravity && near(*r.finalGravity, 1.012, 0.000001));
    CHECK(r.abv && near(*r.abv, 5.41, 0.02));
    CHECK(r.srm && near(*r.srm, 3.86, 0.01));
    CHECK(r.ibu && near(*r.ibu, 15.775, 0.02));
    CHECK(r.ibuByHop.size() == 1 && near(r.ibuByHop[0].ibu, 15.775, 0.02));
    CHECK(r.notes.empty());
    CHECK(r.preBoilLiters && near(*r.preBoilLiters, 24.605, 0.01));
    CHECK(r.postBoilLiters && near(*r.postBoilLiters, 22.145, 0.01));
}

// Without a stated pre-boil size, the pre-boil volume is worked back from the
// batch: (18.93 L + 1 L trub) / 0.9 = 22.14 L.
void testBoilVolumesFromBatch() {
    model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    doc.recipes[0].boil->preBoilSize.reset();
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.preBoilLiters && near(*r.preBoilLiters, 22.141, 0.01));
    CHECK(r.postBoilLiters && near(*r.postBoilLiters, 19.927, 0.01));
}

// Water: the sample's 6 gal of water:
//   RA = 100 * 50/61 * (1 + 2e-4) - (0.7143 * 50 + 0.5879 * 5) = 43.33 ppm as CaCO3
// Targeting sulfate 150 mg/L adds gypsum: 75 mg/L * 22.71 L / (0.5579 * 1000) = 3.053 g,
// which lifts calcium from 50 to 50 + 3.053 * 0.2328 * 1000 / 22.71 = 81.29 mg/L.
void testWaterChemistry() {
    const model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.water.waterLiters && near(*r.water.waterLiters, 22.71, 0.01));
    CHECK(near(r.water.source.calcium, 50, 1e-9));
    CHECK(r.water.residualAlkalinity && near(*r.water.residualAlkalinity, 43.33, 0.02));
    CHECK(r.water.salts.empty());

    calc::Ions target;
    target.sulfate = 150;
    target.chloride = 40;
    target.magnesium = 5;
    target.bicarbonate = 100;
    const calc::WaterChemistry chem = calc::calculateWater(doc.recipes[0], target);
    CHECK(chem.salts.size() == 1);
    CHECK(chem.salts.size() == 1 && chem.salts[0].name == "gypsum");
    CHECK(chem.salts.size() == 1 && near(chem.salts[0].grams, 3.053, 0.01));
    CHECK(near(chem.mash.sulfate, 150, 0.01));
    CHECK(near(chem.mash.calcium, 81.29, 0.05));
}

// A salt on the miscellaneous list counts as an addition, and a target adds
// only the shortfall. 5 g gypsum in 22.71 L adds 5 * 0.5579 * 1000 / 22.71 = 122.8 mg/L
// of sulfate, so the source 75 mg/L becomes 197.8 mg/L.
void testListedSaltCountsAsAddition() {
    model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    model::Miscellaneous gypsum = (*doc.recipes[0].miscellaneous)[0];
    gypsum.name = "Gypsum";
    gypsum.amount = Quantity::fromUnit(5, Unit::Gram);
    gypsum.salt = model::SaltKind::Gypsum;
    doc.recipes[0].miscellaneous->push_back(gypsum);

    const calc::WaterChemistry chem = calc::calculateWater(doc.recipes[0]);
    CHECK(chem.salts.size() == 1 && chem.salts[0].name == "Gypsum");
    CHECK(near(chem.mash.sulfate, 197.8, 0.1));
}

// The name is never used: an ingredient called Gypsum with no salt type is a
// water agent that is not counted, and a note says so.
void testSaltNeedsASaltType() {
    model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    model::Miscellaneous gypsum = (*doc.recipes[0].miscellaneous)[0];
    gypsum.name = "Gypsum";
    gypsum.type = "water agent";
    gypsum.amount = Quantity::fromUnit(5, Unit::Gram);
    gypsum.salt.reset();
    doc.recipes[0].miscellaneous->push_back(gypsum);

    const calc::WaterChemistry chem = calc::calculateWater(doc.recipes[0]);
    CHECK(chem.salts.empty());
    CHECK(near(chem.mash.sulfate, 75, 0.1));
    CHECK(chem.notes.size() == 1);
}

// The boil-off settings drive the volumes. With no evaporation, the post-boil
// volume is the stated pre-boil size: 6.5 gal = 24.605 L.
void testBoilOffSettingsDriveVolumes() {
    model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    CHECK(near(doc.recipes[0].boilOff.evaporationRate.valueIn(Unit::Percent).toDouble(), 10, 1e-9));
    CHECK(near(doc.recipes[0].boilOff.trubLoss.valueIn(Unit::Liter).toDouble(), 1, 1e-9));

    doc.recipes[0].boilOff.evaporationRate = Quantity::fromUnit(Rational(0), Unit::Percent);
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.postBoilLiters && near(*r.postBoilLiters, 24.605, 0.01));
}

void testNoWaterAdditionsIsAnUnknown() {
    model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    doc.recipes[0].waters.reset();
    const calc::WaterChemistry chem = calc::calculateWater(doc.recipes[0]);
    CHECK(!chem.waterLiters.has_value());
    CHECK(!chem.notes.empty());
}

// Strike water: 67 C mash, 20 C grain, 1.5 qt/lb:
//   strike = 67 + (0.2 / 1.5) * (67 - 20) = 73.27 C
// The same ratio given as 3.65 L/kg is 1.749 qt/lb, so 67 + (0.2 / 1.749) * 47 = 72.38 C.
void testStrikeWater() {
    const model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.strikeWater.size() == 1);
    CHECK(r.strikeWater.size() == 1 && near(r.strikeWater[0].celsius, 73.27, 0.01));

    model::Document metric = doc;
    metric.recipes[0].mash->steps[0].waterGrainRatio = Measure::fromUnit(Rational::fromDecimal("3.65"), "l/kg");
    const calc::Results m = calc::calculate(metric.recipes[0]);
    CHECK(m.strikeWater.size() == 1 && near(m.strikeWater[0].celsius, 72.38, 0.05));
}

// No final gravity, culture attenuation, or apparent attenuation: the final
// gravity is unknown, so ABV is left out, and a note says why.
void testMissingInputsAreNotes() {
    model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    model::Recipe& recipe = doc.recipes[0];
    recipe.finalGravity.reset();
    recipe.cultures.reset();
    recipe.apparentAttenuation.reset();
    const calc::Results r = calc::calculate(recipe);
    CHECK(r.originalGravity.has_value());
    CHECK(!r.finalGravity.has_value());
    CHECK(!r.abv.has_value());
    CHECK(!r.notes.empty());
}

// A boil hop with no time is not an addition to the boil: it adds no bitterness.
void testWhirlpoolHopAddsNoBitterness() {
    model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    model::Recipe& recipe = doc.recipes[0];
    (*recipe.hops)[0].timing.use = "add_to_mash";
    const calc::Results r = calc::calculate(recipe);
    CHECK(r.ibu && near(*r.ibu, 0.0, 1e-9));
}

// Prices are per unit of any size, and the cost reads the amount in that unit. 10 lb at 2.50/kg is
// 10 * 0.45359237 * 2.5, and 1 oz of hops at 30/lb is 30/16. The yeast and the moss have no price.
void testCostFromPrices() {
    model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    model::Recipe& recipe = doc.recipes[0];
    CHECK(!calc::calculate(recipe).cost.has_value());

    recipe.fermentables[0].price = model::Price{Rational(5, 2), Unit::Kilogram};
    (*recipe.hops)[0].price = model::Price{Rational(30), Unit::Pound};
    const calc::Results r = calc::calculate(recipe);
    CHECK(r.cost.has_value());
    if (!r.cost) {
        return;
    }
    const double malt = 10 * 0.45359237 * 2.5;
    CHECK(near(r.cost->total, malt + 30.0 / 16.0, 1e-9));
    CHECK(r.cost->byIngredient.size() == 2);
    CHECK(r.cost->unpriced == 2);
    CHECK(r.cost->currency == "USD");
    // The batch is 5 US gallons.
    CHECK(r.cost->perGallon && near(*r.cost->perGallon, r.cost->total / 5.0, 1e-9));
    CHECK(r.cost->perLiter && near(*r.cost->perLiter, r.cost->total / (5 * 3.785411784), 1e-9));
    bool noted = false;
    for (const std::string& note : r.notes) {
        noted = noted || note.find("2 ingredients have no usable price") != std::string::npos;
    }
    CHECK(noted);

    // An amount changed to a unit the price cannot measure is left out, not guessed.
    recipe.fermentables[0].amount = Quantity::fromUnit(Rational(10), Unit::Liter);
    CHECK(calc::calculate(recipe).cost->byIngredient.size() == 1);
}

}  // namespace

void runCalcTests() {
    testSampleRecipe();
    testBoilVolumesFromBatch();
    testWaterChemistry();
    testListedSaltCountsAsAddition();
    testSaltNeedsASaltType();
    testBoilOffSettingsDriveVolumes();
    testNoWaterAdditionsIsAnUnknown();
    testStrikeWater();
    testMissingInputsAreNotes();
    testWhirlpoolHopAddsNoBitterness();
    testCostFromPrices();
}

}  // namespace kettlelogic::test
