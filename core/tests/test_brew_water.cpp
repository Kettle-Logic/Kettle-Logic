#include "check.hpp"

#include "kettlelogic/c/kettlelogic.h"
#include "kettlelogic/calc/brew_water.hpp"
#include "kettlelogic/calc/water.hpp"
#include "kettlelogic/json.hpp"

#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace kettlelogic::test {
namespace {

using calc::brew_water::Acid;
using calc::brew_water::Grain;
using calc::brew_water::MaltCategory;

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

const Acid& acid(const char* name) {
    const Acid* found = calc::brew_water::findAcid(name);
    if (found == nullptr) {
        throw std::logic_error(std::string("missing acid ") + name);
    }
    return *found;
}

void testReportAndSpeciation() {
    // Calcium 100, magnesium 20, bicarbonate 200: the cations are 100/20.05 + 20/12.15, and the anions 200/61.
    calc::Ions ions;
    ions.calcium = 100;
    ions.magnesium = 20;
    ions.bicarbonate = 200;
    const calc::brew_water::ReportBalance b = calc::brew_water::balanceReport(ions, calc::brew_water::OtherIons{});
    CHECK(near(b.cationsMeq, 6.633621707, 1e-6));
    CHECK(near(b.anionsMeq, 3.278688525, 1e-6));
    CHECK(!b.balanced);

    // 100 mg/L as CaCO3 at pH 7 is 121.89 mg/L bicarbonate and 0.056 carbonate.
    const calc::brew_water::Speciation s = calc::brew_water::speciesFromAlkalinity(100, 7);
    CHECK(near(s.bicarbonate, 121.8859793, 1e-5));
    CHECK(near(s.carbonate, 0.05607576, 1e-5));

    // Residual alkalinity for these ions is 80.78
    CHECK(near(calc::residualAlkalinity(ions), 80.7792, 0.001));
}

void testMaltAndMashPh() {
    // 10 lb of base malt at 1.7 °L is 4.76 milliequivalents. 1 lb of crystal at 40 °L is 10.9.
    const std::vector<Grain> grist{{10 * 0.45359237, 1.7, MaltCategory::Base},
                                   {0.45359237, 40, MaltCategory::Crystal}};
    CHECK(near(calc::brew_water::maltAcidityMilliequivalents(grist), 4.76 + 10.9, 1e-6));

    // Mash for this grist, with 19.58 milliequivalents over 14.19 L and 80.78 ppm of
    // residual alkalinity at 1.1538 qt/lb, gives pH 5.7368 (its cell F24).
    const double ph = calc::brew_water::mashPh(19.58, 80.7792, 1.153846, 3.75 * 3.785);
    CHECK(near(ph, 5.73676, 0.0005));

    // The mash pH and the residual alkalinity that reaches it are inverses.
    const double ra = calc::brew_water::residualAlkalinityForMashPh(5.4, 6.172943, 1.437917, 15);
    CHECK(near(calc::brew_water::mashPh(6.172943, ra, 1.437917, 15), 5.4, 1e-9));

    // Baking soda keeps 73 % of its bicarbonate at the mash pH of 5.8176.
    CHECK(near(calc::brew_water::bakingSodaBicarbonateFactor(5.81758544682301), 0.73218, 1e-4));
}

void testAcids() {
    // 2.883 g of 88 % lactic acid in 15 L at pH 5.4 gives 0.97197 protons per molecule, and removes 91.26 ppm.
    const Acid& lactic = acid("lactic acid");
    CHECK(near(calc::brew_water::protonsPerMolecule(lactic, 5.4), 0.971968, 1e-5));
    CHECK(near(calc::brew_water::alkalinityRemovedByAcid(lactic, 2.883314, 88, 15, 5.4), 91.26, 0.02));
    CHECK(near(calc::brew_water::acidGramsToRemoveAlkalinity(lactic, 91.26, 88, 15, 5.4), 2.883314, 0.001));
    // 88 % lactic acid is 1206 g/L, from the computed density polynomial.
    CHECK(near(calc::brew_water::solutionDensityGramsPerLiter(lactic, 88), 1206.18, 0.05));
}

void testSparge() {
    // 1 gal of sparge water with 163.93 ppm alkalinity, from pH 7 to 5.4 with 88 % lactic acid: assume
    // 3.01806 mmol/L of acid, 1.02901 g of pure acid, and 0.96945 mL of the 88 % solution.
    const calc::brew_water::SpargeAcid s =
        calc::brew_water::spargeAcidification(163.934426, 7.0, 5.4, 3.785, acid("lactic acid"), 88);
    CHECK(near(s.acidMmolPerLiter, 3.018055, 2e-4));
    CHECK(near(s.pureGrams, 1.029014, 2e-4));
    CHECK(s.milliliters && near(*s.milliliters, 0.969453, 2e-4));

    // Phosphoric acid at 85 % gives 0.69235 mL at pH 5.6 from pH 7, with the same alkalinity
    const calc::brew_water::SpargeAcid p =
        calc::brew_water::spargeAcidification(163.934426, 7.0, 5.6, 3.785, acid("phosphoric acid"), 85);
    CHECK(p.milliliters && near(*p.milliliters, 0.692354, 2e-4));
}

// The planner's acid dose for the test scenario lands the mash on the target pH:
// 2.883 g of 88 % lactic acid brings the mash from 5.685 to 5.40
void testPlannerAcidReachesTarget() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_add_fermentable(doc, 1, "Pilsner malt", "grain", "5", "kg", "1.037", "sg", "2", "SRM", nullptr) == 0);
    const std::string base =
        R"({"mashRatioLPerKg": 3, "salts": [{"kind": "gypsum", "grams": 3}, {"kind": "baking soda", "grams": 1}])";

    char* out = kl_brewing_plan_json(doc, 1, (base + "}").c_str(), nullptr);
    CHECK(out != nullptr);
    if (out == nullptr) {
        kl_document_free(doc);
        return;
    }
    const json::Value first = json::parse(out);
    kl_string_free(out);
    const double grams = first.find("acidGramsForTarget")->asRational().toDouble();
    CHECK(near(grams, 2.8833, 0.005));
    CHECK(near(first.find("estimatedMashPh")->asRational().toDouble(), 5.6852, 0.0005));

    // Adding that dose lands the mash on 5.40.
    const std::string withAcid = base + R"(, "acids": [{"kind": "lactic acid", "grams": )" +
                                 std::to_string(grams) + "}]}";
    out = kl_brewing_plan_json(doc, 1, withAcid.c_str(), nullptr);
    CHECK(out != nullptr);
    if (out != nullptr) {
        const json::Value second = json::parse(out);
        kl_string_free(out);
        CHECK(near(second.find("estimatedMashPh")->asRational().toDouble(), 5.40, 0.001));
    }
    kl_document_free(doc);
}

// 20 L of a 25 % RO blend with eight salts (its mash profile cells).
void testWaterProfileMatchesWorkbook() {
    const char* inputs = R"({"volume":"20","roPercent":"25","calcium":"50","magnesium":"5","sodium":"10","sulfate":"30",
        "chloride":"20","bicarbonate":"100","gypsum":"2","calciumChloride":"1","epsomSalt":"0.5","magnesiumChloride":"0.3",
        "tableSalt":"0.3","chalk":"0.2","picklingLime":"0.1"})";
    char* out = kl_tool_json("water.profile", inputs, nullptr);
    CHECK(out != nullptr);
    if (out != nullptr) {
        const json::Value root = json::parse(out);
        kl_string_free(out);
        const json::Value& r = *root.find("results");
        CHECK(near(r.find("calcium")->asRational().toDouble(), 85.54168, 1e-4));
        CHECK(near(r.find("magnesium")->asRational().toDouble(), 8.00737, 1e-4));
        CHECK(near(r.find("sodium")->asRational().toDouble(), 13.40119, 1e-4));
        CHECK(near(r.find("sulfate")->asRational().toDouble(), 88.04485, 1e-4));
        CHECK(near(r.find("chloride")->asRational().toDouble(), 61.27238, 1e-4));
        CHECK(near(r.find("bicarbonate")->asRational().toDouble(), 95.42556, 1e-4));
        CHECK(near(r.find("residualAlkalinity")->asRational().toDouble(), 12.42336, 1e-4));
    }
}

void testFacadeReport() {
    const char* inputs = R"({"water": {"bicarbonate": 200, "calcium": 100, "magnesium": 20},
        "sparge": {"liters": 3.785, "startPh": 7, "targetPh": 5.4, "acid": "lactic acid", "strength": 88}})";
    char* error = nullptr;
    char* out = kl_brew_water_json(inputs, &error);
    CHECK(out != nullptr);
    if (out != nullptr) {
        const json::Value root = json::parse(out);
        kl_string_free(out);
        CHECK(near(root.find("report")->find("residualAlkalinity")->asRational().toDouble(), 80.7792, 0.001));
        CHECK(near(root.find("sparge")->find("milliliters")->asRational().toDouble(), 0.969453, 2e-4));
    }
    kl_string_free(error);
}

}  // namespace

void runBrewWaterTests() {
    testReportAndSpeciation();
    testMaltAndMashPh();
    testAcids();
    testSparge();
    testPlannerAcidReachesTarget();
    testFacadeReport();
    testWaterProfileMatchesWorkbook();
}

}  // namespace kettlelogic::test
