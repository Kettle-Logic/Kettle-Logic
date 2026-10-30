#include "check.hpp"

#include "kettlelogic/calc/drink.hpp"
#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/command.hpp"
#include "kettlelogic/model/commands.hpp"
#include "kettlelogic/model/model.hpp"
#include "kettlelogic/model/sidecar.hpp"

#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace kettlelogic::test {

namespace {

using kettlelogic::CommandStack;
using model::Additive;
using model::Document;
using model::EntityId;
using model::Fermentable;
using model::Miscellaneous;
using model::Recipe;
using model::RecipeType;

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

Fermentable fermentable(const std::string& name, const std::string& type, Quantity amount) {
    Fermentable f;
    f.name = name;
    f.type = type;
    f.color = Measure::fromUnit(Rational(0), "Lovi");
    f.amount = std::move(amount);
    return f;
}

// A juice fermentable: a volume, with its gravity as the yield.
Fermentable juice(const std::string& sg, double gallons) {
    Fermentable f = fermentable("Apple juice", "juice", Quantity::fromUnit(Rational(gallons), Unit::Gallon));
    f.yield.potential = Measure::fromUnit(Rational::fromDecimal(sg), "sg");
    return f;
}

// A cider with 5 gal of juice at SG 1.050, and 75 % apparent attenuation.
Document ciderDocument() {
    Document doc;
    Recipe recipe;
    recipe.id = doc.allocateId();
    recipe.name = "Test Cider";
    recipe.type = RecipeType::Cider;
    recipe.author = "Kim";
    recipe.batchSize = Quantity::fromUnit(Rational(5), Unit::Gallon);
    recipe.efficiency.brewhouse = Quantity::fromUnit(Rational(72), Unit::Percent);
    recipe.apparentAttenuation = Quantity::fromUnit(Rational(75), Unit::Percent);
    recipe.fermentables.push_back(juice("1.050", 5));
    doc.recipes.push_back(recipe);
    return doc;
}

// Cider: 5 gal of juice at 1.050 in a 5 gal batch gives OG 1.050 (no brewhouse efficiency),
// FG 1.0125 at 75 %, and ABV 5.0185 by the advanced formula.
void testCiderGravityAndAlcohol() {
    Document doc = ciderDocument();
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.originalGravity && near(*r.originalGravity, 1.050, 1e-9));
    CHECK(r.finalGravity && near(*r.finalGravity, 1.0125, 1e-9));
    CHECK(r.abv && near(*r.abv, 5.0185, 0.001));
    CHECK(r.notes.empty());
}

// Mead: 3 lb of honey at 80 % in a 1 gal batch gives OG 1.11091, and at 80 % attenuation,
// ABV 13.0867 by the advanced formula.
void testMeadHoney() {
    Document doc;
    Recipe recipe;
    recipe.id = doc.allocateId();
    recipe.name = "Test Mead";
    recipe.type = RecipeType::Mead;
    recipe.author = "Kim";
    recipe.batchSize = Quantity::fromUnit(Rational(1), Unit::Gallon);
    recipe.apparentAttenuation = Quantity::fromUnit(Rational(80), Unit::Percent);
    Fermentable honey = fermentable("Wildflower honey", "honey", Quantity::fromUnit(Rational(3), Unit::Pound));
    honey.yield.fineGrind = Quantity::fromUnit(Rational(80), Unit::Percent);
    recipe.fermentables.push_back(honey);
    doc.recipes.push_back(recipe);

    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.originalGravity && near(*r.originalGravity, 1.1109136, 1e-6));
    CHECK(r.abv && near(*r.abv, 13.0867, 0.001));
}

void testCommandsAndSidecar() {
    Document doc = ciderDocument();
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    // Sulfite: 0.5 g of metabisulfite in 5 gal (18.927 L) adds 0.5 / 18.927 * 576 = 15.22 mg/L of SO2.
    Miscellaneous sulfite;
    sulfite.name = "Potassium metabisulfite";
    sulfite.type = "other";
    sulfite.amount = Quantity::fromUnit(Rational::fromDecimal("0.5"), Unit::Gram);
    stack.execute(doc, model::addMiscellaneous(recipe, sulfite));
    CHECK(doc.recipes[0].miscellaneous && doc.recipes[0].miscellaneous->size() == 1);
    const EntityId added = (*doc.recipes[0].miscellaneous)[0].id;
    CHECK(added != 0 && !(*doc.recipes[0].miscellaneous)[0].uuid.isNil());

    stack.execute(doc, model::setAdditive(recipe, added, Additive::Sulfite));
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.sulfiteSo2Ppm && near(*r.sulfiteSo2Ppm, 15.216, 0.001));
    CHECK(stack.undo(doc));
    CHECK(!(*doc.recipes[0].miscellaneous)[0].additive.has_value());

    // A sulfite is a mass: a volume is refused and nothing is recorded.
    Miscellaneous syrup = sulfite;
    syrup.name = "Syrup";
    syrup.amount = Quantity::fromUnit(Rational(1), Unit::Liter);
    stack.execute(doc, model::addMiscellaneous(recipe, syrup));
    const EntityId syrupId = (*doc.recipes[0].miscellaneous)[1].id;
    CHECK(throws([&] { stack.execute(doc, model::setAdditive(recipe, syrupId, Additive::Sulfite)); }));
    CHECK(!(*doc.recipes[0].miscellaneous)[1].additive.has_value());

    // Removing the list's only item and undoing restores it at the same place.
    stack.execute(doc, model::removeMiscellaneous(recipe, syrupId));
    CHECK(doc.recipes[0].miscellaneous->size() == 1);
    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].miscellaneous->size() == 2 && (*doc.recipes[0].miscellaneous)[1].id == syrupId);

    // The recipe type is a command, so it can be undone.
    stack.execute(doc, model::setRecipeType(recipe, RecipeType::Wine));
    CHECK(doc.recipes[0].type == RecipeType::Wine);
    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].type == RecipeType::Cider);

    // Additive roles come back from the sidecar, and so does the list.
    stack.execute(doc, model::setAdditive(recipe, added, Additive::Sulfite));
    stack.execute(doc, model::setAdditive(recipe, syrupId, Additive::TartaricAcid));
    const model::Saved saved = model::saveDocument(doc);
    CHECK(saved.sidecar.find("\"tartaric acid\"") != std::string::npos);
    const Document reopened = model::openDocument(saved.beerJson, saved.sidecar);
    CHECK((*reopened.recipes[0].miscellaneous)[0].additive == Additive::Sulfite);
    CHECK((*reopened.recipes[0].miscellaneous)[1].additive == Additive::TartaricAcid);
    CHECK(reopened.recipes[0].type == RecipeType::Cider);
}

// The cider fixture reads as a cider, with its juice by volume, and its gravity is worked from the juice alone.
void testCiderFixture() {
    std::ifstream in(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/cider_recipe.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const Document doc = model::readBeerJson(text);
    CHECK(doc.recipes[0].type == RecipeType::Cider);
    CHECK(doc.recipes[0].fermentables.size() == 1 && doc.recipes[0].fermentables[0].type == "juice");
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.originalGravity && near(*r.originalGravity, 1.05, 1e-9));
    // The fixture gives no attenuation, so there is no final gravity, and ABV waits on it.
    CHECK(!r.finalGravity && !r.abv);
}

// A juice given by volume but with no gravity yield adds no gravity, and a note says so.
void testVolumeWithoutGravityIsNoted() {
    Document doc = ciderDocument();
    Fermentable bare = fermentable("Mystery juice", "juice", Quantity::fromUnit(Rational(1), Unit::Gallon));
    bare.yield.fineGrind = Quantity::fromUnit(Rational(50), Unit::Percent);
    doc.recipes[0].fermentables.push_back(bare);
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.originalGravity && near(*r.originalGravity, 1.050, 1e-9));
    CHECK(!r.notes.empty());
}

// A drink's batch is not a beer: no boil is assumed, so no boil note is written.
void testDrinkHasNoBoilNote() {
    const calc::Results r = calc::calculate(ciderDocument().recipes[0]);
    for (const std::string& note : r.notes) {
        CHECK(note.find("boil") == std::string::npos);
    }
}

// Planning the cider to its targets: 1.060 from 1.050 needs 10 points, which is
// 10 / 0.385675 * 18.927 L = 490.75 g of sucrose. Its 8 % alcohol target, from 5.01809 %
// (advanced formula), at the default 92 % efficiency needs 899.09 g.
void testPlanCider() {
    Document doc = ciderDocument();
    doc.recipes[0].originalGravity = Measure::fromUnit(Rational::fromDecimal("1.060"), "sg");
    doc.recipes[0].alcoholByVolume = Quantity::fromUnit(Rational(8), Unit::Percent);
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.drink.has_value());
    CHECK(r.drink && r.drink->gravityGrams && near(*r.drink->gravityGrams, 490.752, 0.01));
    CHECK(r.drink && r.drink->alcoholGrams && near(*r.drink->alcoholGrams, 899.085, 0.01));
    CHECK(r.drink && r.drink->notes.empty());
}

// Planning the mead to 1.120 from 1.11091 adds the honey between the two gravities:
// 111.48 g per liter-gallon of must, using the honey yield of 0.80.
void testPlanMead() {
    Document doc;
    Recipe recipe;
    recipe.id = doc.allocateId();
    recipe.name = "Test Mead";
    recipe.type = RecipeType::Mead;
    recipe.author = "Kim";
    recipe.batchSize = Quantity::fromUnit(Rational(1), Unit::Gallon);
    recipe.apparentAttenuation = Quantity::fromUnit(Rational(80), Unit::Percent);
    Fermentable honey = fermentable("Wildflower honey", "honey", Quantity::fromUnit(Rational(3), Unit::Pound));
    honey.yield.fineGrind = Quantity::fromUnit(Rational(80), Unit::Percent);
    recipe.fermentables.push_back(honey);
    recipe.originalGravity = Measure::fromUnit(Rational::fromDecimal("1.120"), "sg");
    recipe.alcoholByVolume = Quantity::fromUnit(Rational(14), Unit::Percent);
    doc.recipes.push_back(recipe);

    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.drink && r.drink->gravityGrams && near(*r.drink->gravityGrams, 111.479, 0.01));
    // Mead is planned by gravity only, and a note says so.
    CHECK(r.drink && !r.drink->alcoholGrams.has_value());
    CHECK(r.drink && !r.drink->notes.empty());
}

// A target below the drink's own gravity or alcohol cannot be reached by adding sugar,
// so nothing is planned for it, and a note says why.
void testUnreachableTargetsAreNoted() {
    Document doc = ciderDocument();
    doc.recipes[0].originalGravity = Measure::fromUnit(Rational::fromDecimal("1.000"), "sg");
    doc.recipes[0].alcoholByVolume = Quantity::fromUnit(Rational(2), Unit::Percent);
    const calc::Results r = calc::calculate(doc.recipes[0]);
    CHECK(r.drink && !r.drink->gravityGrams.has_value());
    CHECK(r.drink && !r.drink->alcoholGrams.has_value());
    CHECK(r.drink && r.drink->notes.size() == 2);
}

// Beer has no drink plan at all.
void testBeerHasNoPlan() {
    const std::string text = R"({"beerjson": {"version": 3, "recipes": [{"name": "x", "type": "all grain", "author": "a",
        "batch_size": {"value": 5, "unit": "gal"}, "efficiency": {"brewhouse": {"value": 72, "unit": "%"}},
        "ingredients": {"fermentable_additions": []}}]}})";
    const Document doc = model::readBeerJson(text);
    CHECK(!calc::calculate(doc.recipes[0]).drink.has_value());
}

// The fermentation efficiency is a setting: it is checked, undoable, and kept in the sidecar.
void testFermentationEfficiency() {
    Document doc = ciderDocument();
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;
    CHECK(throws([&] { stack.execute(doc, model::setFermentationEfficiency(recipe, Quantity::fromUnit(Rational(0), Unit::Percent))); }));
    CHECK(throws([&] { stack.execute(doc, model::setFermentationEfficiency(recipe, Quantity::fromUnit(Rational(101), Unit::Percent))); }));
    CHECK(throws([&] { stack.execute(doc, model::setFermentationEfficiency(recipe, Quantity::fromUnit(Rational(5), Unit::Gallon))); }));
    CHECK(doc.recipes[0].fermentationEfficiency == Quantity::fromUnit(Rational(92), Unit::Percent));

    stack.execute(doc, model::setFermentationEfficiency(recipe, Quantity::fromUnit(Rational(80), Unit::Percent)));
    doc.recipes[0].originalGravity = Measure::fromUnit(Rational::fromDecimal("1.060"), "sg");
    doc.recipes[0].alcoholByVolume = Quantity::fromUnit(Rational(8), Unit::Percent);
    const calc::Results low = calc::calculate(doc.recipes[0]);
    CHECK(low.drink && low.drink->alcoholGrams && near(*low.drink->alcoholGrams, 899.085 * 92 / 80, 0.05));

    const model::Saved saved = model::saveDocument(doc);
    const Document reopened = model::openDocument(saved.beerJson, saved.sidecar);
    CHECK(reopened.recipes[0].fermentationEfficiency == Quantity::fromUnit(Rational(80), Unit::Percent));
    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].fermentationEfficiency == Quantity::fromUnit(Rational(92), Unit::Percent));
}

}  // namespace

void runDrinkTests() {
    testPlanCider();
    testPlanMead();
    testUnreachableTargetsAreNoted();
    testBeerHasNoPlan();
    testFermentationEfficiency();
    testCiderGravityAndAlcohol();
    testMeadHoney();
    testCommandsAndSidecar();
    testVolumeWithoutGravityIsNoted();
    testDrinkHasNoBoilNote();
    testCiderFixture();
}

}  // namespace kettlelogic::test
