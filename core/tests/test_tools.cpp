#include "check.hpp"

#include "kettlelogic/calc/tools.hpp"
#include "kettlelogic/io/recipe_export.hpp"
#include "kettlelogic/model/beerjson.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace kettlelogic::test {
namespace {

using kettlelogic::Quantity;
using kettlelogic::Rational;
using kettlelogic::Unit;
using calc::tools::Inputs;
using calc::tools::run;

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

double value(const Inputs& inputs, const std::string& tool, const std::string& result) {
    return run(tool, inputs).results.at(result);
}

void testWaterAndMash() {
    // Gypsum gives 558.0 mg/L of sulfate per gram per liter: 3.584 g in 20 L is 100 mg/L.
    const Inputs gypsum{{"volume", "20"}, {"gypsum", "3.58415"}};
    CHECK(near(value(gypsum, "water.profile", "sulfate"), 100.0, 0.01));

    // 5 kg at 18 C to 66 C with 3 L/kg: 1.438 qt/lb, so the strike is 66 + (0.2 / 1.438) * 48 = 72.68 C.
    const Inputs infusion{{"grainKilograms", "5"}, {"grainTemperature", "18"}, {"mashTemperature", "66"},
                          {"waterToGrain", "3"}};
    CHECK(near(value(infusion, "water.infusion", "strikeTemperature"), 72.68, 0.01));
    CHECK(near(value(infusion, "water.infusion", "waterLiters"), 15.0, 1e-9));

    // Heat balance: 20 L at 60 C, with 20 L at 75 C added, reaches 66 C with 13.33 L.
    const Inputs adjust{{"mashLiters", "20"}, {"mashTemperature", "60"}, {"targetTemperature", "66"},
                        {"waterTemperature", "75"}};
    CHECK(near(value(adjust, "mash.temperature", "waterLiters"), 20.0 * 6.0 / 9.0, 1e-6));

    // A decoction of 20 L at 63 C, boiled to 100 C, raises the mash to 72 C: 20 * 9 / 37 = 4.865 L.
    const Inputs decoction{{"mashLiters", "20"}, {"currentTemperature", "63"}, {"targetTemperature", "72"}};
    CHECK(near(value(decoction, "mash.decoction", "decoctionLiters"), 20.0 * 9.0 / 37.0, 1e-6));

    // 100 mg/L of bicarbonate and nothing else: 100 * 50 / 61 * (1 + 2e-4) = 81.98 ppm of residual alkalinity.
    const Inputs alkalinity{{"bicarbonate", "100"}, {"calcium", "0"}, {"magnesium", "0"}};
    CHECK(near(value(alkalinity, "mash.alkalinity", "residualAlkalinity"), 100.0 * 50.0 / 61.0 * (1.0 + 2e-4), 1e-6));
}

void testGravityAndAlcohol() {
    // 12 Brix is 1.0483782 (Balling).
    const Inputs brix{{"brix", "12"}};
    CHECK(near(value(brix, "gravity.refractometer", "specificGravity"), 1.0483782, 1e-6));

    // Water at 4 C is 999.97 kg/m^3 and at 20 C is 998.2, so a reading at 20 C corrects to itself.
    const Inputs same{{"reading", "1.050"}, {"sampleTemperature", "20"}, {"calibrationTemperature", "20"}};
    CHECK(near(value(same, "gravity.hydrometer", "correctedGravity"), 1.050, 1e-9));
    // A warm sample reads low, so the corrected gravity is higher.
    const Inputs warm{{"reading", "1.050"}, {"sampleTemperature", "30"}, {"calibrationTemperature", "20"}};
    CHECK(value(warm, "gravity.hydrometer", "correctedGravity") > 1.0515);
    CHECK(value(warm, "gravity.hydrometer", "correctedGravity") < 1.0535);

    const Inputs alcohol{{"originalGravity", "1.050"}, {"finalGravity", "1.010"}};
    CHECK(near(value(alcohol, "alcohol.gravities", "standardAbv"), 5.25, 1e-9));
    CHECK(value(alcohol, "alcohol.gravities", "advancedAbv") > 5.0);

    // Plato 12 is 1.0483782 by the same relation, and the round trip returns the number.
    const Inputs plato{{"value", "12"}, {"from", "plato"}, {"to", "sg"}};
    CHECK(near(value(plato, "convert.gravity", "value"), 1.0483782, 1e-6));
    const Inputs back{{"value", "1.0483782"}, {"from", "sg"}, {"to", "brix"}};
    CHECK(near(value(back, "convert.gravity", "value"), 12.0, 1e-3));
}

void testHopsAndSweetening() {
    // Tinseth: 10 % alpha, 28.35 g, 18.93 L, 60 minutes, at 1.050. Utilization is 0.228, so about 34 IBU.
    const Inputs hop{{"alphaPercent", "10"}, {"grams", "28.35"}, {"postBoilLiters", "18.93"},
                     {"boilMinutes", "60"}, {"gravity", "1.050"}};
    const double ibu = value(hop, "hop.bitterness", "ibu");
    CHECK(ibu > 33.0 && ibu < 35.0);

    // At 0 months of age nothing is lost.
    const Inputs fresh{{"alphaPercent", "10"}, {"months", "0"}, {"storageTemperature", "20"}, {"annualLossPercent", "15"}};
    CHECK(near(value(fresh, "hop.age", "remainingAlphaPercent"), 10.0, 1e-9));
    // One year at 20 C with 15 % annual loss keeps 85 %.
    const Inputs year{{"alphaPercent", "10"}, {"months", "12"}, {"storageTemperature", "20"}, {"annualLossPercent", "15"}};
    CHECK(near(value(year, "hop.age", "remainingAlphaPercent"), 8.5, 1e-9));

    // At pH 3.5, 0.8 mg/L molecular SO2 needs about 40 mg/L free SO2.
    const Inputs sulfite{{"volumeLiters", "20"}, {"molecularSo2", "0.8"}, {"pH", "3.5"}};
    CHECK(near(value(sulfite, "sweeten.sulfite", "freeSo2Ppm"), 40.0, 0.1));

    // Ten gravity points in 20 L is 10 / 0.38567 = 25.93 g/L of sucrose, so 518.7 g.
    const Inputs sweeten{{"volumeLiters", "20"}, {"currentGravity", "1.010"}, {"targetGravity", "1.020"},
                         {"sugarYield", "1"}};
    CHECK(near(value(sweeten, "sweeten.backsweeten", "sugarGrams"), 20.0 * 10.0 / 0.38567, 0.5));

    const Inputs mead{{"volumeLiters", "10"}, {"targetYan", "250"}, {"currentYan", "50"}};
    CHECK(near(value(mead, "mead.nutrient", "dapGrams"), 200.0 * 10.0 / 210.0, 1e-6));
}

void testBoilDilutionGravity() {
    const Inputs boil{{"preBoilLiters", "25"}, {"boilMinutes", "60"}, {"evaporationPercentPerHour", "10"}};
    CHECK(near(value(boil, "boil.volume", "postBoilLiters"), 22.5, 1e-9));
    CHECK(near(value(boil, "boil.volume", "evaporatedLiters"), 2.5, 1e-9));

    const Inputs rate{{"preBoilLiters", "25"}, {"postBoilLiters", "22.5"}, {"boilMinutes", "60"}};
    CHECK(near(value(rate, "boil.rate", "evaporationPercentPerHour"), 10.0, 1e-9));

    const Inputs dilute{{"volumeLiters", "10"}, {"currentValue", "8"}, {"targetValue", "4"}};
    CHECK(near(value(dilute, "dilute.dilution", "waterLiters"), 10.0, 1e-9));
    CHECK(near(value(dilute, "dilute.dilution", "finalLiters"), 20.0, 1e-9));

    const Inputs raise{{"volumeLiters", "20"}, {"currentGravity", "1.040"}, {"targetGravity", "1.050"},
                       {"sugarYield", "1"}};
    CHECK(near(value(raise, "gravity.adjust", "sugarGrams"), 10.0 * 20.0 / 0.38567, 0.5));
    CHECK(near(value(raise, "gravity.adjust", "waterLiters"), 0.0, 1e-12));

    const Inputs lower{{"volumeLiters", "20"}, {"currentGravity", "1.050"}, {"targetGravity", "1.040"},
                       {"sugarYield", "1"}};
    CHECK(near(value(lower, "gravity.adjust", "waterLiters"), 5.0, 1e-9));

    const Inputs weight{{"density", "1.5"}, {"milliliters", "100"}};
    CHECK(near(value(weight, "weight.mass", "grams"), 150.0, 1e-9));
    const Inputs volume{{"density", "1.5"}, {"grams", "150"}};
    CHECK(near(value(volume, "weight.volume", "milliliters"), 100.0, 1e-9));
}

void testCarbonationAndUnits() {
    // 2.5 volumes at 4 C needs 10.02 psi of gauge pressure, by Henry's law for water.
    const Inputs pressure{{"volumes", "2.5"}, {"temperature", "4"}};
    const double psi = value(pressure, "carbonation.pressure", "gaugePsi");
    CHECK(near(psi, 10.02, 0.05));

    // And back: that pressure at 4 C gives 2.5 volumes.
    const Inputs volumes{{"gaugePsi", std::to_string(psi)}, {"temperature", "4"}};
    CHECK(near(value(volumes, "carbonation.volumes", "volumes"), 2.5, 1e-3));

    // One volume is about 4.02 g/L of dextrose and 3.82 g/L of sucrose, so 2.5 volumes in 20 L is 201 g.
    const Inputs priming{{"volumeLiters", "20"}, {"targetVolumes", "2.5"}, {"residualVolumes", "0"}};
    CHECK(near(value(priming, "carbonation.priming", "dextroseGrams"), 201.0, 1.0));
    CHECK(near(value(priming, "carbonation.priming", "sucroseGrams"), 191.0, 1.0));

    const Inputs f{{"value", "212"}, {"from", "F"}, {"to", "C"}};
    CHECK(near(value(f, "convert.temperature", "value"), 100.0, 1e-9));
    const Inputs gal{{"value", "1"}, {"from", "gal"}, {"to", "floz"}};
    CHECK(near(value(gal, "convert.volume", "value"), 128.0, 1e-9));
    const Inputs lb{{"value", "1"}, {"from", "lb"}, {"to", "oz"}};
    CHECK(near(value(lb, "convert.weight", "value"), 16.0, 1e-9));
    const Inputs atm{{"value", "1"}, {"from", "atm"}, {"to", "kPa"}};
    CHECK(near(value(atm, "convert.pressure", "value"), 101.325, 1e-9));
    const Inputs psiToAtm{{"value", "14.695948775"}, {"from", "psi"}, {"to", "atm"}};
    CHECK(near(value(psiToAtm, "convert.pressure", "value"), 1.0, 1e-9));
}

void testErrors() {
    CHECK(throws([] { run("no.such.tool", {}); }));
    CHECK(throws([] { run("convert.volume", {{"value", "1"}, {"from", "gal"}}); }));
    CHECK(throws([] { run("convert.volume", {{"value", "1"}, {"from", "gal"}, {"to", "furlong"}}); }));
    CHECK(throws([] { run("convert.volume", {{"value", "1"}, {"from", "gal"}, {"to", "lb"}}); }));
    CHECK(throws([] { run("alcohol.gravities", {{"originalGravity", "1.010"}, {"finalGravity", "1.050"}}); }));
    CHECK(throws([] { run("dilute.dilution", {{"volumeLiters", "10"}, {"currentValue", "4"}, {"targetValue", "8"}}); }));
    CHECK(throws([] { run("hop.age", {{"alphaPercent", "10"}, {"months", "1"}, {"storageTemperature", "20"}, {"annualLossPercent", "not a number"}}); }));
}

void testExports() {
    std::ifstream file(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/sample_recipe.json");
    std::stringstream text;
    text << file.rdbuf();
    const model::Document document = model::readBeerJson(text.str());
    const model::Recipe& recipe = document.recipes.front();
    const calc::Results results = calc::calculate(recipe);

    const std::string xml = io::exportBeerXml(recipe, results);
    CHECK(xml.find("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") == 0);
    CHECK(xml.find("<RECIPES>") != std::string::npos);
    CHECK(xml.find("<FERMENTABLE>") != std::string::npos);
    CHECK(xml.find("<HOP>") != std::string::npos);
    CHECK(xml.find("</RECIPES>") != std::string::npos);
    CHECK(xml.find(recipe.name) != std::string::npos);

    const std::string summary = io::exportText(recipe, results);
    CHECK(summary.find(recipe.name) == 0);
    CHECK(summary.find("Fermentables") != std::string::npos);

    // Beta acid and cost appear only when the recipe has them, and the cost is the one calculate() works out.
    CHECK(xml.find("<BETA>") == std::string::npos);
    CHECK(summary.find("Cost") == std::string::npos);
    model::Recipe costed = recipe;
    (*costed.hops)[0].betaAcid = Quantity::fromUnit(Rational(45, 10), Unit::Percent);
    costed.fermentables[0].price = model::Price{Rational(2), Unit::Pound};
    const calc::Results costedResults = calc::calculate(costed);
    CHECK(io::exportBeerXml(costed, costedResults).find("<BETA>4.5</BETA>") != std::string::npos);
    const std::string costedSummary = io::exportText(costed, costedResults);
    CHECK(costedSummary.find("4.5 % beta") != std::string::npos);
    CHECK(costedSummary.find("Ingredients: 20.00 USD") != std::string::npos);
    CHECK(costedSummary.find("not included") != std::string::npos);

    const std::string json = io::exportBeerJson(recipe);
    CHECK(json.find("\"beerjson\"") != std::string::npos);
    CHECK(json.find("\"recipes\"") != std::string::npos);
}

}  // namespace

void runToolTests() {
    testWaterAndMash();
    testGravityAndAlcohol();
    testHopsAndSweetening();
    testBoilDilutionGravity();
    testCarbonationAndUnits();
    testErrors();
    testExports();
}

}  // namespace kettlelogic::test
