#include "check.hpp"

#include "kettlelogic/command.hpp"
#include "kettlelogic/json.hpp"
#include "kettlelogic/model/beerjson.hpp"
#include "kettlelogic/model/commands.hpp"
#include "kettlelogic/model/model.hpp"
#include "kettlelogic/model/sidecar.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef KETTLELOGIC_TEST_DATA_DIR
#error "KETTLELOGIC_TEST_DATA_DIR must be defined by the build"
#endif

namespace kettlelogic::test {

namespace {

using kettlelogic::CommandStack;
using kettlelogic::Quantity;
using kettlelogic::Rational;
using kettlelogic::Unit;
using kettlelogic::json::parse;
using kettlelogic::model::Document;
using kettlelogic::model::EntityId;
using kettlelogic::model::Fermentable;
using kettlelogic::model::ReadError;
using kettlelogic::model::readBeerJson;
using kettlelogic::model::Recipe;
using kettlelogic::model::writeBeerJson;

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string sampleText() {
    return readFile(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/sample_recipe.json");
}

// A recipe that passes the schema's required-property rules, with one
// property replaced by `replacement` where a test needs to vary it.
std::string minimalDocument(const std::string& batchSize = R"({"value": 5, "unit": "gal"})",
                            const std::string& extraRecipeFields = "") {
    return R"({"beerjson": {"version": 3, "recipes": [{
        "name": "Minimal", "type": "all grain", "author": "Kim",
        "batch_size": )" + batchSize + R"(,
        "efficiency": {"brewhouse": {"value": 70, "unit": "%"}},
        "ingredients": {"fermentable_additions": []})" +
           extraRecipeFields + R"(}]}})";
}

bool hasKey(const kettlelogic::json::Object& members, const std::string& key) {
    for (const auto& m : members) {
        if (m.key == key) {
            return true;
        }
    }
    return false;
}

// Returns the JSON pointer carried by the ReadError thrown by `fn`, or "<none>".
template <typename Fn>
std::string pointerOf(Fn fn) {
    try {
        fn();
    } catch (const ReadError& e) {
        return e.pointer();
    } catch (const std::exception&) {
        return "<other>";
    }
    return "<none>";
}

void testReadSample() {
    const Document doc = readBeerJson(sampleText());
    CHECK(doc.version == Rational(3));
    CHECK(doc.recipes.size() == 1);
    const Recipe& r = doc.recipes[0];

    CHECK(r.name == "Sample Pale Ale");
    CHECK(r.type == model::RecipeType::AllGrain);
    CHECK(r.batchSize.valueIn(Unit::Gallon) == Rational(5));
    CHECK(r.efficiency.brewhouse.valueIn(Unit::Percent) == Rational(72));

    CHECK(r.fermentables.size() == 1);
    CHECK(r.fermentables[0].name == "Pale Malt (2-Row)");
    CHECK(r.fermentables[0].amount == Quantity::fromUnit(10, Unit::Pound));
    CHECK(r.fermentables[0].color.unit() == "Lovi");
    CHECK(r.fermentables[0].id != 0);

    CHECK(r.hops && r.hops->size() == 1);
    CHECK((*r.hops)[0].alphaAcid.valueIn(Unit::Percent) == Rational::fromDecimal("5.5"));
    CHECK((*r.hops)[0].timing.time->valueIn(Unit::Minute) == Rational(60));
    CHECK((*r.hops)[0].timing.use == "add_to_boil");

    CHECK(r.miscellaneous && r.miscellaneous->size() == 1);
    CHECK(r.cultures && r.cultures->size() == 1);
    CHECK((*r.cultures)[0].form == "liquid");
    CHECK(r.waters && r.waters->size() == 1);
    CHECK((*r.waters)[0].water.calcium.valueIn(Unit::MilligramPerLiter) == Rational(50));
    CHECK((*r.waters)[0].amount->valueIn(Unit::Gallon) == Rational(6));

    CHECK(r.mash.has_value());
    CHECK(r.mash->steps.size() == 1);
    CHECK(r.mash->steps[0].stepTemperature.valueIn(Unit::Celsius) == Rational(67));
    CHECK(r.fermentation.has_value());
    CHECK(r.fermentation->steps[0].stepTime->valueIn(Unit::Day) == Rational(14));
    CHECK(r.boil.has_value());
    CHECK(r.boil->boilTime.valueIn(Unit::Minute) == Rational(60));

    // Root keys the model does not type are kept.
    CHECK(doc.preserved.size() == 2);
    CHECK(hasKey(doc.preserved, "profiles"));
    CHECK(hasKey(doc.preserved, "packaging"));
}

// Writing and re-reading must give the same JSON as the original. JSON
// comparison is structural and ignores key order.
void testRoundTripMatchesOriginal() {
    const std::string written = writeBeerJson(readBeerJson(sampleText()));
    CHECK(parse(written) == parse(sampleText()));
}

void testWriteIsStable() {
    const std::string once = writeBeerJson(readBeerJson(sampleText()));
    const std::string twice = writeBeerJson(readBeerJson(once));
    CHECK(once == twice);
}

void testExactValuesAreWrittenExactly() {
    const std::string written = writeBeerJson(readBeerJson(minimalDocument(R"({"value": 1.1, "unit": "gal"})")));
    CHECK(written.find("\"value\": 1.1") != std::string::npos);
    CHECK(written.find("\"unit\": \"gal\"") != std::string::npos);
}

void testUnknownRecipeKeysArePreserved() {
    const std::string text = minimalDocument(
        R"({"value": 5, "unit": "gal"})",
        R"(, "taste": {"notes": "Crisp", "rating": 40}, "style": {"name": "Pale Ale"})");
    const Document doc = readBeerJson(text);
    const std::string written = writeBeerJson(doc);
    CHECK(parse(written) == parse(text));
    CHECK(doc.recipes[0].preserved.size() == 2);
}

void testSchemaErrorsCarryPointers() {
    CHECK(pointerOf([] { readBeerJson(minimalDocument(R"({"value": 5, "unit": "lb"})")); }) ==
          "/beerjson/recipes/0/batch_size/unit");
    CHECK(pointerOf([] { readBeerJson(minimalDocument(R"({"value": 5, "unit": "stone"})")); }) ==
          "/beerjson/recipes/0/batch_size/unit");
    // An undefined key in a closed object is a warning, not an error (see testUndefinedPropertiesAreForeign).
    CHECK(pointerOf([] { readBeerJson(minimalDocument(R"({"value": 5, "unit": "gal", "extra": 1})")); }) ==
          "<none>");
    CHECK(pointerOf([] { readBeerJson(minimalDocument(R"({"value": "5", "unit": "gal"})")); }) ==
          "/beerjson/recipes/0/batch_size/value");
    CHECK(pointerOf([] { readBeerJson(minimalDocument(R"({"value": 5})")); }) ==
          "/beerjson/recipes/0/batch_size");
    CHECK(pointerOf([] {
        readBeerJson(R"({"beerjson": {"version": 3, "recipes": [{"name": "x", "type": "beer", "author": "a",
            "efficiency": {"brewhouse": {"value": 70, "unit": "%"}}, "batch_size": {"value": 5, "unit": "gal"},
            "ingredients": {"fermentable_additions": []}}]}})");
    }) == "/beerjson/recipes/0/type");
    CHECK(pointerOf([] {
        readBeerJson(R"({"beerjson": {"version": 4, "recipes": []}})");
    }) == "/beerjson/version");
    CHECK(pointerOf([] {
        readBeerJson(R"({"beerjson": {"version": 3, "recipes": [{"name": "x", "type": "all grain"}]}})");
    }) == "/beerjson/recipes/0");  // missing required properties reports the object
    CHECK(pointerOf([] {
        readBeerJson(R"({"beerjson": {"version": 3, "recipes": [{"name": "x", "type": "mead", "author": "a",
            "efficiency": {"brewhouse": {"value": 70, "unit": "%"}}, "batch_size": {"value": 5, "unit": "gal"},
            "ingredients": {"fermentable_additions": []}}], "bogus": 1}})");
    }) == "<none>");  // unknown root keys are preserved, not rejected
    CHECK(pointerOf([] {
        readBeerJson(R"({"beerjson": {"version": 3}})");
    }) == "<none>");
    CHECK(pointerOf([] { readBeerJson(R"({"beerjson": 3})"); }) == "/beerjson");
    CHECK(pointerOf([] { readBeerJson(R"({"nope": {}})"); }) == "");  // a missing root is reported at ""
}

void testMalformedJsonThrowsParseError() {
    CHECK(throws([] { readBeerJson(R"({"beerjson": {"version": 3, "version": 3}})"); }));
    CHECK(throws([] { readBeerJson("not json"); }));
}

void testTimeMustBeInteger() {
    const std::string text = R"({"beerjson": {"version": 3, "recipes": [{
        "name": "x", "type": "all grain", "author": "a",
        "efficiency": {"brewhouse": {"value": 70, "unit": "%"}},
        "batch_size": {"value": 5, "unit": "gal"},
        "ingredients": {"fermentable_additions": []},
        "boil": {"boil_time": {"value": 1.5, "unit": "min"}}}]}})";
    CHECK(pointerOf([&] { readBeerJson(text); }) == "/beerjson/recipes/0/boil/boil_time/value");
}

void testAddAndUndoFermentable() {
    Document doc = readBeerJson(sampleText());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    Fermentable pilsner = doc.recipes[0].fermentables[0];
    pilsner.id = 0;
    pilsner.name = "Pilsner";
    stack.execute(doc, kettlelogic::model::addFermentable(recipe, pilsner));
    CHECK(doc.recipes[0].fermentables.size() == 2);
    const EntityId added = doc.recipes[0].fermentables[1].id;
    CHECK(added != 0);

    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].fermentables.size() == 1);

    // Redo restores the same entity with the same id.
    CHECK(stack.redo(doc));
    CHECK(doc.recipes[0].fermentables.size() == 2);
    CHECK(doc.recipes[0].fermentables[1].id == added);
}

void testRemoveFermentableUndoRestoresPosition() {
    Document doc = readBeerJson(minimalDocument());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    Fermentable base;
    base.name = "A";
    base.type = "grain";
    base.yield.fineGrind = Quantity::fromUnit(80, Unit::Percent);
    base.amount = Quantity::fromUnit(1, Unit::Pound);
    for (const char* name : {"A", "B", "C"}) {
        base.name = name;
        stack.execute(doc, kettlelogic::model::addFermentable(recipe, base));
    }
    const EntityId middle = doc.recipes[0].fermentables[1].id;

    stack.execute(doc, kettlelogic::model::removeFermentable(recipe, middle));
    CHECK(doc.recipes[0].fermentables.size() == 2);
    CHECK(doc.recipes[0].fermentables[1].name == "C");

    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].fermentables.size() == 3);
    CHECK(doc.recipes[0].fermentables[1].name == "B");
    CHECK(doc.recipes[0].fermentables[1].id == middle);
}

void testRenameMerges() {
    Document doc = readBeerJson(minimalDocument());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    stack.execute(doc, kettlelogic::model::setRecipeName(recipe, "P"));
    stack.execute(doc, kettlelogic::model::setRecipeName(recipe, "Pa"));
    stack.execute(doc, kettlelogic::model::setRecipeName(recipe, "Pale"));
    CHECK(doc.recipes[0].name == "Pale");
    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].name == "Minimal");
    CHECK(!stack.canUndo());
}

void testBatchSizeUndo() {
    Document doc = readBeerJson(minimalDocument());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    stack.execute(doc, kettlelogic::model::setBatchSize(recipe, Quantity::fromUnit(10, Unit::Gallon)));
    CHECK(doc.recipes[0].batchSize.valueIn(Unit::Gallon) == Rational(10));
    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].batchSize.valueIn(Unit::Gallon) == Rational(5));
}

// An empty optional list in the file is present (shown as empty) and is written back.
void testEmptyOptionalListsStayPresent() {
    const std::string text = R"({"beerjson": {"version": 3, "recipes": [{"name": "x", "type": "all grain",
        "author": "a", "efficiency": {"brewhouse": {"value": 70, "unit": "%"}},
        "batch_size": {"value": 5, "unit": "gal"},
        "ingredients": {"fermentable_additions": [], "hop_additions": []}}]}})";
    const Document doc = readBeerJson(text);
    CHECK(doc.recipes[0].hops.has_value());
    CHECK(doc.recipes[0].hops->empty());
    CHECK(!doc.recipes[0].miscellaneous.has_value());
    CHECK(parse(writeBeerJson(doc)) == parse(text));
}

void testRemoveAndAddIngredientList() {
    Document doc = readBeerJson(sampleText());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    // A list with items cannot be removed.
    CHECK(throws([&] {
        stack.execute(doc, kettlelogic::model::setIngredientListPresent(
                               recipe, kettlelogic::model::IngredientList::Hops, false));
    }));
    CHECK(doc.recipes[0].hops.has_value());

    // An empty list can be removed, and undo brings it back as empty.
    doc.recipes[0].hops->clear();
    stack.execute(doc, kettlelogic::model::setIngredientListPresent(
                           recipe, kettlelogic::model::IngredientList::Hops, false));
    CHECK(!doc.recipes[0].hops.has_value());
    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].hops.has_value() && doc.recipes[0].hops->empty());

    // Adding a missing list creates it empty, and the writer shows it as [].
    // The minimal document has no optional lists, so it is the right starting point.
    Document bare = readBeerJson(minimalDocument());
    const EntityId bareRecipe = bare.recipes[0].id;
    CommandStack<Document> bareStack;
    bareStack.execute(bare, kettlelogic::model::setIngredientListPresent(
                                bareRecipe, kettlelogic::model::IngredientList::Waters, true));
    CHECK(bare.recipes[0].waters.has_value() && bare.recipes[0].waters->empty());
    CHECK(writeBeerJson(bare).find("\"water_additions\": []") != std::string::npos);
    CHECK(bareStack.undo(bare));
    CHECK(!bare.recipes[0].waters.has_value());
    CHECK(writeBeerJson(bare).find("water_additions") == std::string::npos);
}

// Keys the schema does not define, in a closed object, are foreign: warned
// about, kept on the document, and left out of the written file. Keys the
// schema defines (even untyped ones) and keys in an open object stay inline.
void testUndefinedPropertiesAreForeign() {
    const std::string ions = R"("calcium": {"value": 50, "unit": "mg/l"}, "bicarbonate": {"value": 100, "unit": "mg/l"},
        "sulfate": {"value": 75, "unit": "mg/l"}, "chloride": {"value": 40, "unit": "mg/l"},
        "sodium": {"value": 10, "unit": "mg/l"}, "magnesium": {"value": 5, "unit": "mg/l"})";
    const std::string text = R"({"beerjson": {"version": 3, "recipes": [{
        "name": "x", "type": "all grain", "author": "a", "bogus": 1,
        "efficiency": {"brewhouse": {"value": 70, "unit": "%"}},
        "batch_size": {"value": 5, "unit": "gal"},
        "taste": {"notes": "Crisp", "rating": 40},
        "ingredients": {"fermentable_additions": [], "water_additions": [
            {"name": "Well", )" + ions + R"(, "extra_water": 2}]}}]}})";

    const Document doc = readBeerJson(text);
    CHECK(doc.foreign.size() == 1);
    CHECK(doc.foreign.size() == 1 && doc.foreign[0].pointer() == "/beerjson/recipes/0/bogus");
    CHECK(doc.diagnostics.size() == 1);
    CHECK(doc.diagnostics.size() == 1 && doc.diagnostics[0].pointer == "/beerjson/recipes/0/bogus");

    const std::string written = writeBeerJson(doc);
    CHECK(written.find("bogus") == std::string::npos);
    CHECK(written.find("\"taste\"") != std::string::npos);        // defined by the schema, untyped
    CHECK(written.find("\"extra_water\": 2") != std::string::npos);  // open object: stays inline
}

void testBoilStepsPresence() {
    Document doc = readBeerJson(sampleText());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    // The sample's boil has no steps list. Adding one makes it present and empty.
    CHECK(!doc.recipes[0].boil->steps.has_value());
    stack.execute(doc, kettlelogic::model::setBoilStepsPresent(recipe, true));
    CHECK(doc.recipes[0].boil->steps.has_value() && doc.recipes[0].boil->steps->empty());
    CHECK(writeBeerJson(doc).find("\"boil_steps\": []") != std::string::npos);
    CHECK(stack.undo(doc));
    CHECK(!doc.recipes[0].boil->steps.has_value());

    // A boil with steps cannot lose them to a removal.
    kettlelogic::model::BoilStep step;
    step.name = "Hop stand";
    doc.recipes[0].boil->steps = std::vector<kettlelogic::model::BoilStep>{step};
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::setBoilStepsPresent(recipe, false)); }));
    CHECK(doc.recipes[0].boil->steps->size() == 1);

    // A recipe with no boil procedure cannot have boil steps.
    Document bare = readBeerJson(minimalDocument());
    CommandStack<Document> bareStack;
    CHECK(throws([&] {
        bareStack.execute(bare, kettlelogic::model::setBoilStepsPresent(bare.recipes[0].id, true));
    }));
    CHECK(!bareStack.canUndo());

    // An empty boil-steps array in the file reads as present and empty, and is written back.
    const std::string text = R"({"beerjson": {"version": 3, "recipes": [{"name": "x", "type": "all grain",
        "author": "a", "efficiency": {"brewhouse": {"value": 70, "unit": "%"}},
        "batch_size": {"value": 5, "unit": "gal"}, "ingredients": {"fermentable_additions": []},
        "boil": {"boil_time": {"value": 60, "unit": "min"}, "boil_steps": []}}]}})";
    const Document withEmpty = readBeerJson(text);
    CHECK(withEmpty.recipes[0].boil->steps.has_value() && withEmpty.recipes[0].boil->steps->empty());
    CHECK(parse(writeBeerJson(withEmpty)) == parse(text));
}

// Undefined properties leave the BeerJSON file for the sidecar and come back on open.
void testSidecarRoundTrip() {
    const std::string unknownKeys = readFile(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/unknown_keys.json");
    const Document doc = readBeerJson(unknownKeys);
    CHECK(doc.foreign.size() == 3);

    const kettlelogic::model::Saved saved = kettlelogic::model::saveDocument(doc);
    CHECK(saved.beerJson.find("bogus") == std::string::npos);
    CHECK(saved.beerJson.find("\"extra\"") == std::string::npos);
    CHECK(saved.sidecar.find("bogus_recipe_key") != std::string::npos);

    const Document reopened = kettlelogic::model::openDocument(saved.beerJson, saved.sidecar);
    CHECK(reopened.foreign.size() == 3);
    // Entries come back in the order they were read: each object is finished
    // as the reader leaves it, so the quantity comes first and the recipe last.
    CHECK(reopened.foreign.size() == 3 && reopened.foreign[0].pointer() == "/beerjson/recipes/0/batch_size/extra");
    CHECK(reopened.foreign.size() == 3 &&
          reopened.foreign[1].pointer() == "/beerjson/recipes/0/ingredients/fermentable_additions/0/bogus");
    CHECK(reopened.foreign.size() == 3 && reopened.foreign[2].pointer() == "/beerjson/recipes/0/bogus_recipe_key");
    CHECK(reopened.recipes[0].uuid == doc.recipes[0].uuid);

    // Saving again changes nothing: identity and sidecar are stable.
    const kettlelogic::model::Saved again = kettlelogic::model::saveDocument(reopened);
    CHECK(again.beerJson == saved.beerJson);
    CHECK(again.sidecar == saved.sidecar);
}

// The sidecar is written even with no undefined properties, because it holds entity identity.
void testSidecarAlwaysWritten() {
    const Document doc = readBeerJson(sampleText());
    const kettlelogic::model::Saved saved = kettlelogic::model::saveDocument(doc);
    CHECK(saved.sidecar.find("\"entities\"") != std::string::npos);
    CHECK(saved.sidecar.find("\"foreign\": []") != std::string::npos);
    const Document reopened = kettlelogic::model::openDocument(saved.beerJson, saved.sidecar);
    CHECK(reopened.recipes[0].uuid == doc.recipes[0].uuid);
    CHECK(reopened.recipes[0].fermentables[0].uuid == doc.recipes[0].fermentables[0].uuid);
    CHECK(reopened.recipes[0].hops && (*reopened.recipes[0].hops)[0].uuid == (*doc.recipes[0].hops)[0].uuid);
}

// A rename made in another tool keeps the recipe's identity and its foreign properties.
void testRenamedRecipeKeepsIdentity() {
    const std::string unknownKeys = readFile(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/unknown_keys.json");
    const Document doc = readBeerJson(unknownKeys);
    const kettlelogic::model::Saved saved = kettlelogic::model::saveDocument(doc);

    std::string renamed = saved.beerJson;
    const std::string from = "\"name\": \"Sample Pale Ale\"";
    const std::size_t at = renamed.find(from);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        renamed.replace(at, from.size(), "\"name\": \"Renamed\"");
    }

    const Document reopened = kettlelogic::model::openDocument(renamed, saved.sidecar);
    CHECK(reopened.recipes[0].uuid == doc.recipes[0].uuid);
    CHECK(reopened.foreign.size() == 3);
    int problems = 0;
    for (const auto& d : reopened.diagnostics) {
        if (d.message.find("kept until you check it") != std::string::npos) {
            ++problems;
        }
    }
    CHECK(problems == 0);
}

// A recipe inserted before the others shifts their pointers. Their foreign
// properties follow the entity, not the old pointer.
void testReorderedRecipeKeepsForeignProperties() {
    const std::string unknownKeys = readFile(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/unknown_keys.json");
    const Document doc = readBeerJson(unknownKeys);
    const kettlelogic::model::Saved saved = kettlelogic::model::saveDocument(doc);

    const std::string opener = "\"recipes\": [";
    std::string shifted = saved.beerJson;
    const std::size_t at = shifted.find(opener);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        shifted.insert(at + opener.size(),
                       R"({"name": "Inserted", "type": "all grain", "author": "a",
                           "efficiency": {"brewhouse": {"value": 70, "unit": "%"}},
                           "batch_size": {"value": 5, "unit": "gal"},
                           "ingredients": {"fermentable_additions": []}},)");
    }

    const Document reopened = kettlelogic::model::openDocument(shifted, saved.sidecar);
    CHECK(reopened.recipes.size() == 2);
    CHECK(reopened.recipes[1].uuid == doc.recipes[0].uuid);
    CHECK(reopened.recipes[1].uuid != reopened.recipes[0].uuid);
    CHECK(reopened.foreign.size() == 3);
    CHECK(reopened.foreign.size() == 3 && reopened.foreign[2].pointer() == "/beerjson/recipes/1/bogus_recipe_key");
    CHECK(reopened.foreign.size() == 3 && reopened.foreign[0].pointer() == "/beerjson/recipes/1/batch_size/extra");
}

// If the owning entity is gone, its foreign properties are kept with a diagnostic.
void testRemovedOwnerIsKept() {
    const std::string unknownKeys = readFile(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/unknown_keys.json");
    const kettlelogic::model::Saved saved = kettlelogic::model::saveDocument(readBeerJson(unknownKeys));

    const std::string emptied = R"({"beerjson": {"version": 3, "recipes": []}})";
    const Document reopened = kettlelogic::model::openDocument(emptied, saved.sidecar);
    CHECK(reopened.foreign.size() == 3);
    int orphans = 0;
    for (const auto& d : reopened.diagnostics) {
        if (d.message.find("no longer in the document") != std::string::npos) {
            ++orphans;
        }
    }
    CHECK(orphans == 3);

    const kettlelogic::model::Saved again = kettlelogic::model::saveDocument(reopened);
    CHECK(again.sidecar.find("bogus_recipe_key") != std::string::npos);
}

void testMalformedSidecarIsRejected() {
    const std::string recipe = R"({"beerjson": {"version": 3, "recipes": []}})";
    CHECK(throws([&] { kettlelogic::model::openDocument(recipe, std::string_view(R"({"schema": 7, "entities": [], "foreign": [], "recipeSettings": [], "saltTypes": [], "additiveRoles": []})")); }));
    CHECK(throws([&] { kettlelogic::model::openDocument(recipe, std::string_view(R"({"schema": 6, "entities": [], "foreign": []})")); }));
    CHECK(throws([&] { kettlelogic::model::openDocument(recipe, std::string_view(R"({"schema": 1, "foreign": []})")); }));
    CHECK(throws([&] { kettlelogic::model::openDocument(recipe, std::string_view(R"({"schema": 6, "foreign": [], "recipeSettings": [], "saltTypes": [], "additiveRoles": []})")); }));
    CHECK(throws([&] {
        kettlelogic::model::openDocument(recipe, std::string_view(R"({"schema": 6, "entities": [{"uuid": "nope"}], "foreign": [], "recipeSettings": [], "saltTypes": [], "additiveRoles": []})"));
    }));
    CHECK(throws([&] {
        kettlelogic::model::openDocument(recipe, std::string_view(R"({"schema": 6, "entities": [], "foreign": [{"key": "x"}], "recipeSettings": [], "saltTypes": [], "additiveRoles": []})"));
    }));
    CHECK(throws([&] { kettlelogic::model::openDocument(recipe, std::string_view("not json")); }));
    // An empty sidecar is valid and opens cleanly.
    CHECK(kettlelogic::model::openDocument(recipe, std::string_view(R"({"schema": 6, "entities": [], "foreign": [], "recipeSettings": [], "saltTypes": [], "additiveRoles": []})"))
              .foreign.empty());
}

void testEmptyYieldIsRejected() {
    const std::string text = R"({"beerjson": {"version": 3, "recipes": [{"name": "x", "type": "all grain",
        "author": "a", "efficiency": {"brewhouse": {"value": 70, "unit": "%"}},
        "batch_size": {"value": 5, "unit": "gal"},
        "ingredients": {"fermentable_additions": [{"name": "Malt", "type": "grain", "yield": {},
            "color": {"value": 2, "unit": "Lovi"}, "amount": {"value": 1, "unit": "lb"}}]}}]}})";
    CHECK(pointerOf([&] { readBeerJson(text); }) ==
          "/beerjson/recipes/0/ingredients/fermentable_additions/0/yield");

    // The same rule applies to edits: an invalid fermentable is never recorded.
    Document doc = readBeerJson(minimalDocument());
    CommandStack<Document> stack;
    Fermentable malt;
    malt.name = "Malt";
    malt.type = "grain";
    malt.color = kettlelogic::Measure::fromUnit(Rational(2), "Lovi");
    malt.amount = Quantity::fromUnit(1, Unit::Pound);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addFermentable(doc.recipes[0].id, malt)); }));
    CHECK(!stack.canUndo());
    CHECK(doc.recipes[0].fermentables.empty());
}

void testAddAndUndoRecipe() {
    Document doc = readBeerJson(minimalDocument());
    CommandStack<Document> stack;

    Recipe recipe;
    recipe.name = "Second";
    recipe.author = "a";
    recipe.batchSize = Quantity::fromUnit(5, Unit::Gallon);
    recipe.efficiency.brewhouse = Quantity::fromUnit(70, Unit::Percent);
    stack.execute(doc, kettlelogic::model::addRecipe(recipe));
    CHECK(doc.recipes.size() == 2);
    const EntityId added = doc.recipes[1].id;
    CHECK(added != 0);
    CHECK(!doc.recipes[1].uuid.isNil());

    CHECK(stack.undo(doc));
    CHECK(doc.recipes.size() == 1);

    // Redo restores the same recipe with the same id.
    CHECK(stack.redo(doc));
    CHECK(doc.recipes.size() == 2);
    CHECK(doc.recipes[1].id == added);
    CHECK(doc.recipes[1].name == "Second");
}

void testFermentableTypeAndAmountAreChecked() {
    Document doc = readBeerJson(minimalDocument());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    Fermentable malt;
    malt.name = "Malt";
    malt.yield.fineGrind = Quantity::fromUnit(80, Unit::Percent);
    malt.color = kettlelogic::Measure::fromUnit(Rational(2), "Lovi");
    malt.amount = Quantity::fromUnit(1, Unit::Pound);

    malt.type = "soup";
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addFermentable(recipe, malt)); }));
    malt.type = "grain";
    malt.amount = Quantity::fromUnit(1, Unit::Second);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addFermentable(recipe, malt)); }));
    CHECK(!stack.canUndo());
    CHECK(doc.recipes[0].fermentables.empty());
}

kettlelogic::model::Hop sampleHop() {
    kettlelogic::model::Hop hop;
    hop.name = "Cascade";
    hop.alphaAcid = Quantity::fromUnit(Rational(55, 10), Unit::Percent);
    hop.amount = Quantity::fromUnit(30, Unit::Gram);
    hop.timing.use = "add_to_boil";
    hop.timing.time = Quantity::fromUnit(60, Unit::Minute);
    return hop;
}

kettlelogic::model::Culture sampleCulture() {
    kettlelogic::model::Culture culture;
    culture.name = "US-05";
    culture.type = "ale";
    culture.form = "dry";
    culture.amount = Quantity::fromUnit(1, Unit::CountPackage);
    return culture;
}

kettlelogic::model::WaterAddition sampleWater() {
    kettlelogic::model::WaterAddition addition;
    addition.water.name = "Tap";
    for (Quantity* mineral : {&addition.water.calcium, &addition.water.bicarbonate, &addition.water.sulfate,
                              &addition.water.chloride, &addition.water.sodium, &addition.water.magnesium}) {
        *mineral = Quantity::fromUnit(10, Unit::MilligramPerLiter);
    }
    addition.amount = Quantity::fromUnit(20, Unit::Liter);
    return addition;
}

void testAddAndRemoveHops() {
    Document doc = readBeerJson(minimalDocument());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;
    CHECK(!doc.recipes[0].hops);

    stack.execute(doc, kettlelogic::model::addHop(recipe, sampleHop()));
    CHECK(doc.recipes[0].hops && doc.recipes[0].hops->size() == 1);
    const EntityId added = (*doc.recipes[0].hops)[0].id;
    CHECK(added != 0);
    CHECK(!(*doc.recipes[0].hops)[0].uuid.isNil());

    // What was added survives a write and a read.
    const Document again = readBeerJson(writeBeerJson(doc));
    CHECK(again.recipes[0].hops && again.recipes[0].hops->size() == 1);
    CHECK((*again.recipes[0].hops)[0].name == "Cascade");

    // Undo takes the list away again, since the edit made it. Redo keeps the id.
    CHECK(stack.undo(doc));
    CHECK(!doc.recipes[0].hops);
    CHECK(stack.redo(doc));
    CHECK((*doc.recipes[0].hops)[0].id == added);

    // Removing the last hop leaves an empty list, and undo brings the hop back in place.
    stack.execute(doc, kettlelogic::model::removeHop(recipe, added));
    CHECK(doc.recipes[0].hops && doc.recipes[0].hops->empty());
    CHECK(stack.undo(doc));
    CHECK((*doc.recipes[0].hops)[0].id == added);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::removeHop(recipe, added + 1000)); }));
}

void testAddAndRemoveCulturesAndWaters() {
    Document doc = readBeerJson(minimalDocument());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    stack.execute(doc, kettlelogic::model::addCulture(recipe, sampleCulture()));
    stack.execute(doc, kettlelogic::model::addWater(recipe, sampleWater()));
    CHECK(doc.recipes[0].cultures && doc.recipes[0].cultures->size() == 1);
    CHECK(doc.recipes[0].waters && doc.recipes[0].waters->size() == 1);

    const Document again = readBeerJson(writeBeerJson(doc));
    CHECK(again.recipes[0].cultures && (*again.recipes[0].cultures)[0].name == "US-05");
    CHECK(again.recipes[0].waters && (*again.recipes[0].waters)[0].water.name == "Tap");

    const EntityId culture = (*doc.recipes[0].cultures)[0].id;
    const EntityId water = (*doc.recipes[0].waters)[0].id;
    CHECK(culture != water);
    stack.execute(doc, kettlelogic::model::removeCulture(recipe, culture));
    stack.execute(doc, kettlelogic::model::removeWater(recipe, water));
    CHECK(doc.recipes[0].cultures->empty());
    CHECK(doc.recipes[0].waters->empty());
    CHECK(stack.undo(doc));
    CHECK(stack.undo(doc));
    CHECK((*doc.recipes[0].cultures)[0].id == culture);
    CHECK((*doc.recipes[0].waters)[0].id == water);
}

void testInvalidIngredientsAreNotRecorded() {
    Document doc = readBeerJson(minimalDocument());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;

    kettlelogic::model::Hop hop = sampleHop();
    hop.alphaAcid = Quantity::fromUnit(5, Unit::Gram);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addHop(recipe, hop)); }));
    hop = sampleHop();
    hop.timing.time = Quantity::fromUnit(Rational(3, 2), Unit::Minute);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addHop(recipe, hop)); }));
    hop = sampleHop();
    hop.form = "gummy";
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addHop(recipe, hop)); }));
    hop = sampleHop();
    hop.timing.use = "add_to_moon";
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addHop(recipe, hop)); }));

    kettlelogic::model::Culture culture = sampleCulture();
    culture.type = "soup";
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addCulture(recipe, culture)); }));
    culture = sampleCulture();
    culture.amount = Quantity::fromUnit(1, Unit::Minute);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addCulture(recipe, culture)); }));

    kettlelogic::model::WaterAddition water = sampleWater();
    water.water.calcium = Quantity::fromUnit(10, Unit::Gram);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addWater(recipe, water)); }));
    water = sampleWater();
    water.amount = Quantity::fromUnit(1, Unit::Kilogram);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addWater(recipe, water)); }));

    // Nothing was recorded, and no empty list was left behind.
    CHECK(!stack.canUndo());
    CHECK(!doc.recipes[0].hops && !doc.recipes[0].cultures && !doc.recipes[0].waters);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::addHop(999, sampleHop())); }));
}

void testRemoveRecipeAndUndo() {
    Document doc = readBeerJson(minimalDocument());
    CommandStack<Document> stack;
    Recipe second;
    second.name = "Second";
    second.author = "a";
    second.batchSize = Quantity::fromUnit(5, Unit::Gallon);
    second.efficiency.brewhouse = Quantity::fromUnit(70, Unit::Percent);
    stack.execute(doc, kettlelogic::model::addRecipe(second));
    stack.execute(doc, kettlelogic::model::addRecipe(second));
    const EntityId middle = doc.recipes[1].id;

    stack.execute(doc, kettlelogic::model::removeRecipe(middle));
    CHECK(doc.recipes.size() == 2);
    CHECK(doc.findRecipe(middle) == nullptr);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::removeRecipe(middle)); }));

    CHECK(stack.undo(doc));
    CHECK(doc.recipes.size() == 3);
    CHECK(doc.recipes[1].id == middle);
}

void testTrashRecipeAndUndo() {
    Document doc = readBeerJson(minimalDocument());
    CommandStack<Document> stack;
    Recipe second;
    second.name = "Second";
    second.author = "a";
    second.batchSize = Quantity::fromUnit(5, Unit::Gallon);
    second.efficiency.brewhouse = Quantity::fromUnit(70, Unit::Percent);
    stack.execute(doc, kettlelogic::model::addRecipe(second));
    const EntityId trashed = doc.recipes.back().id;

    // Trashing keeps the recipe, and files it in a new Recently Deleted folder.
    stack.execute(doc, kettlelogic::model::trashRecipe(trashed));
    CHECK(doc.recipes.size() == 2);
    CHECK(doc.folders.size() == 1);
    const model::Folder* trash = doc.trashFolder();
    CHECK(trash != nullptr && trash->trash && trash->name == "Recently Deleted");
    const EntityId trashFolder = trash->id;
    CHECK(doc.findRecipe(trashed)->folder == trashFolder);

    // A deleted recipe is not deleted twice, and the folder cannot be renamed or removed.
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::trashRecipe(trashed)); }));
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::renameFolder(trashFolder, "Bin")); }));
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::removeFolder(trashFolder)); }));

    // Undo files the recipe where it was, and removes the folder the edit made.
    CHECK(stack.undo(doc));
    CHECK(doc.trashFolder() == nullptr);
    CHECK(doc.folders.empty());
    CHECK(!doc.findRecipe(trashed)->folder);

    // Redo makes the folder again with the same id.
    CHECK(stack.redo(doc));
    CHECK(doc.trashFolder() != nullptr && doc.trashFolder()->id == trashFolder);
    CHECK(doc.findRecipe(trashed)->folder == trashFolder);

    // Moving a recipe out of the folder restores it, and removing it deletes it for good.
    stack.execute(doc, kettlelogic::model::moveRecipeToFolder(trashed, std::nullopt));
    CHECK(!doc.findRecipe(trashed)->folder);
    stack.execute(doc, kettlelogic::model::trashRecipe(trashed));
    stack.execute(doc, kettlelogic::model::removeRecipe(trashed));
    CHECK(doc.findRecipe(trashed) == nullptr);
    CHECK(doc.trashFolder() != nullptr);
}

void testRenameRecipeAndUndo() {
    Document doc = readBeerJson(minimalDocument());
    CommandStack<Document> stack;
    const EntityId recipe = doc.recipes[0].id;
    const std::string before = doc.recipes[0].name;
    stack.execute(doc, kettlelogic::model::setRecipeName(recipe, "Renamed Ale"));
    CHECK(doc.recipes[0].name == "Renamed Ale");
    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].name == before);
}

void testReplaceKeepsPositionAndIdentity() {
    Document doc = readBeerJson(minimalDocument());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;
    stack.execute(doc, kettlelogic::model::addHop(recipe, sampleHop()));
    stack.execute(doc, kettlelogic::model::addHop(recipe, sampleHop()));
    const kettlelogic::model::Hop before = (*doc.recipes[0].hops)[0];

    kettlelogic::model::Hop edited = before;
    edited.amount = Quantity::fromUnit(45, Unit::Gram);
    edited.timing.time = Quantity::fromUnit(15, Unit::Minute);
    stack.execute(doc, kettlelogic::model::setHop(recipe, edited));
    CHECK((*doc.recipes[0].hops)[0].id == before.id);
    CHECK((*doc.recipes[0].hops)[0].uuid == before.uuid);
    CHECK((*doc.recipes[0].hops)[0].amount == Quantity::fromUnit(45, Unit::Gram));
    CHECK((*doc.recipes[0].hops)[1].amount == Quantity::fromUnit(30, Unit::Gram));

    CHECK(stack.undo(doc));
    CHECK((*doc.recipes[0].hops)[0].amount == before.amount);

    // An edit that breaks a rule is refused, and the item is unchanged.
    edited.timing.time = Quantity::fromUnit(Rational(3, 2), Unit::Minute);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::setHop(recipe, edited)); }));
    CHECK((*doc.recipes[0].hops)[0].amount == before.amount);

    // An item that is not in the list is refused.
    edited = before;
    edited.id = 9999;
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::setHop(recipe, edited)); }));
}

void testDuplicateRecipeGetsFreshIdentity() {
    Document doc = readBeerJson(sampleText());
    CommandStack<Document> stack;
    const Recipe original = doc.recipes[0];

    stack.execute(doc, kettlelogic::model::duplicateRecipe(original.id));
    CHECK(doc.recipes.size() == 2);
    // The copy goes right after the original, under its own name.
    const Recipe& copy = doc.recipes[1];
    CHECK(copy.name == original.name + " copy");
    CHECK(copy.id != original.id && copy.uuid != original.uuid);
    CHECK(copy.batchSize == original.batchSize);
    CHECK(copy.fermentables.size() == original.fermentables.size());
    CHECK(copy.fermentables[0].name == original.fermentables[0].name);
    CHECK(copy.fermentables[0].id != original.fermentables[0].id);
    CHECK(copy.fermentables[0].uuid != original.fermentables[0].uuid);
    CHECK(copy.hops && (*copy.hops)[0].id != (*original.hops)[0].id);
    CHECK(copy.hops && (*copy.hops)[0].uuid != (*original.hops)[0].uuid);
    CHECK(copy.cultures && (*copy.cultures)[0].uuid != (*original.cultures)[0].uuid);
    CHECK(copy.waters && (*copy.waters)[0].id != (*original.waters)[0].id);
    CHECK(copy.miscellaneous && (*copy.miscellaneous)[0].uuid != (*original.miscellaneous)[0].uuid);

    // What was copied survives a write and a read, as two recipes.
    const Document again = readBeerJson(writeBeerJson(doc));
    CHECK(again.recipes.size() == 2);
    CHECK(again.recipes[1].name == original.name + " copy");

    // Undo removes the copy. Redo brings back the same copy with the same ids.
    const EntityId copyId = copy.id;
    CHECK(stack.undo(doc));
    CHECK(doc.recipes.size() == 1);
    CHECK(stack.redo(doc));
    CHECK(doc.recipes.size() == 2 && doc.recipes[1].id == copyId);

    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::duplicateRecipe(999)); }));
    CHECK(doc.recipes.size() == 2);
}

void testDuplicateIngredientsKeepTheirValues() {
    Document doc = readBeerJson(sampleText());
    const EntityId recipe = doc.recipes[0].id;
    CommandStack<Document> stack;
    const model::Hop hop = (*doc.recipes[0].hops)[0];

    stack.execute(doc, kettlelogic::model::duplicateHop(recipe, hop.id));
    CHECK((*doc.recipes[0].hops).size() == 2);
    const model::Hop& copy = (*doc.recipes[0].hops)[1];
    CHECK(copy.name == hop.name && copy.amount == hop.amount && copy.alphaAcid == hop.alphaAcid);
    CHECK(copy.id != hop.id && copy.uuid != hop.uuid);
    const EntityId copyId = copy.id;
    CHECK(stack.undo(doc));
    CHECK((*doc.recipes[0].hops).size() == 1);
    CHECK(stack.redo(doc));
    CHECK((*doc.recipes[0].hops)[1].id == copyId);

    // A miscellaneous ingredient keeps the salt it is, which the sidecar stores.
    const model::Miscellaneous misc = (*doc.recipes[0].miscellaneous)[0];
    stack.execute(doc, kettlelogic::model::setSaltType(recipe, misc.id, model::SaltKind::Gypsum));
    stack.execute(doc, kettlelogic::model::duplicateMiscellaneous(recipe, misc.id));
    const auto& miscellaneous = *doc.recipes[0].miscellaneous;
    CHECK(miscellaneous.size() == 2);
    CHECK(miscellaneous[1].salt == model::SaltKind::Gypsum);
    CHECK(miscellaneous[1].uuid != misc.uuid);

    // Every list duplicates in its own recipe, and a missing ingredient is refused and not recorded.
    stack.execute(doc, kettlelogic::model::duplicateFermentable(recipe, doc.recipes[0].fermentables[0].id));
    stack.execute(doc, kettlelogic::model::duplicateCulture(recipe, (*doc.recipes[0].cultures)[0].id));
    stack.execute(doc, kettlelogic::model::duplicateWater(recipe, (*doc.recipes[0].waters)[0].id));
    CHECK(doc.recipes[0].fermentables.size() == 2);
    CHECK(doc.recipes[0].cultures && doc.recipes[0].cultures->size() == 2);
    CHECK(doc.recipes[0].waters && doc.recipes[0].waters->size() == 2);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::duplicateHop(recipe, hop.id + 1000)); }));
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::duplicateHop(999, hop.id)); }));
    CHECK((*doc.recipes[0].hops).size() == 2);
}

void testMissingRecipeIsNotRecorded() {
    Document doc = readBeerJson(minimalDocument());
    CommandStack<Document> stack;
    CHECK(throws([&] {
        stack.execute(doc, kettlelogic::model::setRecipeName(999, "Nope"));
    }));
    CHECK(!stack.canUndo());
}

// Salt type and boil-off are Kettle Logic data. Commands set them with undo, and
// the sidecar keeps them by UUID, so they come back on open.
void testSettingsAreStoredAndUndone() {
    Document doc = readBeerJson(sampleText());
    const EntityId recipe = doc.recipes[0].id;
    const EntityId salt = (*doc.recipes[0].miscellaneous)[0].id;
    CommandStack<Document> stack;

    stack.execute(doc, kettlelogic::model::setSaltType(recipe, salt, model::SaltKind::Gypsum));
    CHECK((*doc.recipes[0].miscellaneous)[0].salt == model::SaltKind::Gypsum);
    CHECK(stack.undo(doc));
    CHECK(!(*doc.recipes[0].miscellaneous)[0].salt.has_value());

    model::BoilOff off;
    off.evaporationRate = Quantity::fromUnit(Rational(5), Unit::Percent);
    off.trubLoss = Quantity::fromUnit(Rational(2), Unit::Liter);
    stack.execute(doc, kettlelogic::model::setBoilOff(recipe, off));
    CHECK(doc.recipes[0].boilOff.evaporationRate == off.evaporationRate);
    CHECK(stack.undo(doc));
    CHECK(doc.recipes[0].boilOff.evaporationRate == Quantity::fromUnit(Rational(10), Unit::Percent));

    // Out-of-range values are refused, and nothing is recorded.
    model::BoilOff bad;
    bad.evaporationRate = Quantity::fromUnit(Rational(100), Unit::Percent);
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::setBoilOff(recipe, bad)); }));
    CHECK(doc.recipes[0].boilOff.evaporationRate == Quantity::fromUnit(Rational(10), Unit::Percent));

    // Round trip through the sidecar. The salt type is never written to BeerJSON.
    stack.execute(doc, kettlelogic::model::setSaltType(recipe, salt, model::SaltKind::EpsomSalt));
    stack.execute(doc, kettlelogic::model::setBoilOff(recipe, off));
    const kettlelogic::model::Saved saved = kettlelogic::model::saveDocument(doc);
    CHECK(saved.sidecar.find("\"epsom salt\"") != std::string::npos);
    CHECK(saved.beerJson.find("epsom salt") == std::string::npos);

    const Document reopened = kettlelogic::model::openDocument(saved.beerJson, saved.sidecar);
    CHECK((*reopened.recipes[0].miscellaneous)[0].salt == model::SaltKind::EpsomSalt);
    CHECK(reopened.recipes[0].boilOff.evaporationRate == off.evaporationRate);
    CHECK(reopened.recipes[0].boilOff.trubLoss == off.trubLoss);
    CHECK(kettlelogic::model::saveDocument(reopened).sidecar == saved.sidecar);

    // A stored salt type for an ingredient that is no longer there is reported, not applied.
    Document withoutMisc = doc;
    withoutMisc.recipes[0].miscellaneous.reset();
    const Document stale = kettlelogic::model::openDocument(kettlelogic::model::saveDocument(withoutMisc).beerJson,
                                                            saved.sidecar);
    bool reported = false;
    for (const auto& d : stale.diagnostics) {
        reported = reported || d.message.find("no matching entity") != std::string::npos;
    }
    CHECK(reported);
}

}  // namespace

// A price is Kettle Logic data: undoable, checked against the amount, kept in the sidecar and out of the BeerJSON.
void testPricesAndCurrency() {
    Document doc = readBeerJson(sampleText());
    const EntityId recipe = doc.recipes[0].id;
    const EntityId malt = doc.recipes[0].fermentables[0].id;
    const EntityId hop = (*doc.recipes[0].hops)[0].id;
    const EntityId yeast = (*doc.recipes[0].cultures)[0].id;
    using kettlelogic::model::IngredientKind;
    using kettlelogic::model::Price;
    CommandStack<Document> stack;

    stack.execute(doc, kettlelogic::model::setPrice(recipe, IngredientKind::Fermentable, malt, Price{Rational(5, 2), Unit::Kilogram}));
    CHECK(doc.recipes[0].fermentables[0].price && doc.recipes[0].fermentables[0].price->amount == Rational(5, 2));
    CHECK(stack.undo(doc));
    CHECK(!doc.recipes[0].fermentables[0].price.has_value());
    CHECK(stack.redo(doc));

    // A price that cannot measure the ingredient's amount, or is negative, is refused and not recorded.
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::setPrice(recipe, IngredientKind::Hop, hop, Price{Rational(3), Unit::Liter})); }));
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::setPrice(recipe, IngredientKind::Hop, hop, Price{Rational(-1), Unit::Ounce})); }));
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::setPrice(recipe, IngredientKind::Hop, 9999, Price{Rational(1), Unit::Ounce})); }));
    CHECK(!(*doc.recipes[0].hops)[0].price.has_value());

    stack.execute(doc, kettlelogic::model::setPrice(recipe, IngredientKind::Hop, hop, Price{Rational(33, 20), Unit::Ounce}));
    stack.execute(doc, kettlelogic::model::setPrice(recipe, IngredientKind::Culture, yeast, Price{Rational(7), Unit::CountPackage}));
    stack.execute(doc, kettlelogic::model::setCurrency(recipe, "eur"));
    CHECK(doc.recipes[0].currency == "EUR");
    CHECK(throws([&] { stack.execute(doc, kettlelogic::model::setCurrency(recipe, "euro")); }));
    CHECK(doc.recipes[0].currency == "EUR");

    // The sidecar keeps them exactly, the BeerJSON never sees them, and a second save changes nothing.
    const kettlelogic::model::Saved saved = kettlelogic::model::saveDocument(doc);
    CHECK(saved.beerJson.find("1.65") == std::string::npos);
    CHECK(saved.sidecar.find("\"prices\"") != std::string::npos);
    const Document reopened = kettlelogic::model::openDocument(saved.beerJson, saved.sidecar);
    CHECK(reopened.recipes[0].currency == "EUR");
    CHECK(reopened.recipes[0].fermentables[0].price && reopened.recipes[0].fermentables[0].price->per == Unit::Kilogram);
    CHECK((*reopened.recipes[0].hops)[0].price && (*reopened.recipes[0].hops)[0].price->amount == Rational(33, 20));
    CHECK((*reopened.recipes[0].hops)[0].price && (*reopened.recipes[0].hops)[0].price->per == Unit::Ounce);
    CHECK((*reopened.recipes[0].cultures)[0].price && (*reopened.recipes[0].cultures)[0].price->per == Unit::CountPackage);
    CHECK(!(*reopened.recipes[0].miscellaneous)[0].price.has_value());
    CHECK(kettlelogic::model::saveDocument(reopened).sidecar == saved.sidecar);

    // A sidecar written before prices existed has no prices and the default currency.
    const Document plain = kettlelogic::model::openDocument(sampleText(), std::nullopt);
    CHECK(plain.recipes[0].currency == "USD");
    CHECK(!plain.recipes[0].fermentables[0].price.has_value());

    // A copy of an ingredient keeps its price.
    stack.execute(doc, kettlelogic::model::duplicateHop(recipe, hop));
    CHECK((*doc.recipes[0].hops)[1].price && (*doc.recipes[0].hops)[1].price->amount == Rational(33, 20));
}

void runModelTests() {
    testReadSample();
    testRoundTripMatchesOriginal();
    testWriteIsStable();
    testExactValuesAreWrittenExactly();
    testUnknownRecipeKeysArePreserved();
    testSchemaErrorsCarryPointers();
    testMalformedJsonThrowsParseError();
    testTimeMustBeInteger();
    testAddAndUndoFermentable();
    testRemoveFermentableUndoRestoresPosition();
    testRenameMerges();
    testBatchSizeUndo();
    testEmptyOptionalListsStayPresent();
    testRemoveAndAddIngredientList();
    testUndefinedPropertiesAreForeign();
    testSidecarRoundTrip();
    testSidecarAlwaysWritten();
    testRenamedRecipeKeepsIdentity();
    testReorderedRecipeKeepsForeignProperties();
    testRemovedOwnerIsKept();
    testMalformedSidecarIsRejected();
    testBoilStepsPresence();
    testEmptyYieldIsRejected();
    testMissingRecipeIsNotRecorded();
    testSettingsAreStoredAndUndone();
    testPricesAndCurrency();
    testAddAndUndoRecipe();
    testAddAndRemoveHops();
    testAddAndRemoveCulturesAndWaters();
    testInvalidIngredientsAreNotRecorded();
    testRemoveRecipeAndUndo();
    testTrashRecipeAndUndo();
    testRenameRecipeAndUndo();
    testReplaceKeepsPositionAndIdentity();
    testFermentableTypeAndAmountAreChecked();
    testDuplicateRecipeGetsFreshIdentity();
    testDuplicateIngredientsKeepTheirValues();
}

}  // namespace kettlelogic::test
