#include "check.hpp"

#include "kettlelogic/c/kettlelogic.h"
#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/io/beerxml_import.hpp"
#include "kettlelogic/io/recipe_export.hpp"
#include "kettlelogic/model/beerjson.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace kettlelogic::test {

namespace {

using model::Document;

std::string readSample(const char* name) {
    std::ifstream in(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The message of the BeerXmlError that importing `text` throws, or "" when none is thrown.
std::string errorOf(const std::string& text) {
    try {
        io::importBeerXml(text);
    } catch (const io::BeerXmlError& e) {
        return e.what();
    }
    return "";
}

// True when a diagnostic's pointer or message contains `fragment`.
bool warned(const Document& document, const std::string& fragment) {
    for (const model::Diagnostic& d : document.diagnostics) {
        if (d.pointer.find(fragment) != std::string::npos || d.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void testSampleRecipe() {
    const Document document = io::importBeerXml(readSample("sample_beerxml.xml"));
    CHECK(document.recipes.size() == 1);
    const model::Recipe& r = document.recipes.front();

    CHECK(r.name == "Sample Pale Ale");
    CHECK(r.type == model::RecipeType::AllGrain);
    CHECK(r.author == "Sample Brewer");
    CHECK(r.batchSize.valueIn(Unit::Liter) == Rational(23));
    CHECK(r.efficiency.brewhouse.valueIn(Unit::Percent) == Rational(72));
    CHECK(r.notes == std::optional<std::string>("Brewed & tested, <twice>."));
    CHECK(r.originalGravity && r.originalGravity->unit() == "sg");
    CHECK(r.originalGravity && r.originalGravity->value() == Rational::fromDecimal("1.052"));
    CHECK(r.boil && r.boil->boilTime.valueIn(Unit::Minute) == Rational(60));
    CHECK(r.boil && r.boil->preBoilSize && r.boil->preBoilSize->valueIn(Unit::Liter) == Rational(28));

    // Amounts are exact: 0.1 kg is 100 g with no rounding.
    CHECK(r.fermentables.size() == 3);
    CHECK(r.fermentables[0].amount.valueIn(Unit::Kilogram) == Rational::fromDecimal("4.5"));
    CHECK(r.fermentables[1].amount.valueIn(Unit::Gram) == Rational(100));
    CHECK(r.fermentables[0].producer == std::optional<std::string>("Briess"));
    CHECK(r.fermentables[0].yield.fineGrind && r.fermentables[0].yield.fineGrind->valueIn(Unit::Percent) == Rational(80));
    // Lovibond for grain, SRM for a liquid extract.
    CHECK(r.fermentables[0].color.unit() == "Lovi");
    CHECK(r.fermentables[2].type == "extract");
    CHECK(r.fermentables[2].color.unit() == "SRM");
    CHECK(r.fermentables[2].color.value() == Rational(7));

    CHECK(r.hops && r.hops->size() == 2);
    CHECK((*r.hops)[0].timing.use == std::optional<std::string>("add_to_boil"));
    CHECK((*r.hops)[0].form == std::optional<std::string>("pellet"));
    CHECK((*r.hops)[1].timing.use == std::optional<std::string>("add_to_fermentation"));
    CHECK((*r.hops)[1].timing.time && (*r.hops)[1].timing.time->valueIn(Unit::Minute) == Rational(10080));
    CHECK((*r.hops)[1].betaAcid && (*r.hops)[1].betaAcid->valueIn(Unit::Percent) == Rational::fromDecimal("4.5"));

    CHECK(r.miscellaneous && r.miscellaneous->size() == 1);
    CHECK((*r.miscellaneous)[0].amount.dimension() == Dimension::Mass);
    CHECK((*r.miscellaneous)[0].timing && (*r.miscellaneous)[0].timing->use == std::optional<std::string>("add_to_boil"));

    CHECK(r.cultures && r.cultures->size() == 1);
    CHECK((*r.cultures)[0].type == "ale");
    CHECK((*r.cultures)[0].form == "liquid");
    CHECK((*r.cultures)[0].producer == std::optional<std::string>("Wyeast"));
    CHECK((*r.cultures)[0].amount.dimension() == Dimension::Volume);

    CHECK(r.waters && r.waters->size() == 1);
    CHECK((*r.waters)[0].water.sulfate.valueIn(Unit::MilligramPerLiter) == Rational(350));

    // A mash step of 2.5 minutes is not a whole number of minutes, so it is written in seconds.
    CHECK(r.mash && r.mash->steps.size() == 2);
    CHECK(r.mash && r.mash->steps[0].type == "infusion");
    CHECK(r.mash && r.mash->steps[0].amount && r.mash->steps[0].amount->valueIn(Unit::Liter) == Rational(18));
    CHECK(r.mash && r.mash->steps[1].stepTime.unit() == Unit::Second);
    CHECK(r.mash && r.mash->steps[1].stepTime.valueIn(Unit::Second) == Rational(150));
    CHECK(r.mash && r.mash->steps[1].rampTime && r.mash->steps[1].rampTime->valueIn(Unit::Minute) == Rational(5));

    // Tags the engine does not read are noted, and the read goes on.
    CHECK(warned(document, "STYLE"));
    CHECK(warned(document, "ADD_AFTER_BOIL"));
    CHECK(warned(document, "PH"));
    CHECK(!warned(document, "BETA"));

    // The result converts to BeerJSON and reads back.
    const std::string json = model::writeBeerJson(document);
    CHECK(model::readBeerJson(json).recipes.size() == 1);
}

void testSingleRecipeRoot() {
    const Document document = io::importBeerXml(
        "<RECIPE><NAME>Bare</NAME><BATCH_SIZE>20</BATCH_SIZE></RECIPE>");
    CHECK(document.recipes.size() == 1);
    CHECK(document.recipes.front().name == "Bare");
    CHECK(document.recipes.front().type == model::RecipeType::AllGrain);
    CHECK(document.recipes.front().efficiency.brewhouse.valueIn(Unit::Percent) == Rational(75));
    CHECK(warned(document, "TYPE"));
    CHECK(warned(document, "EFFICIENCY"));
}

void testMissingOptionalFieldsAreDefaulted() {
    // A fermentable without COLOR or YIELD gets a default and a note, and still imports.
    const Document document = io::importBeerXml(
        "<RECIPES><RECIPE><NAME>x</NAME><BATCH_SIZE>20</BATCH_SIZE>"
        "<FERMENTABLES><FERMENTABLE><NAME>Dextrose</NAME><TYPE>Sugar</TYPE><AMOUNT>1</AMOUNT>"
        "</FERMENTABLE></FERMENTABLES></RECIPE></RECIPES>");
    const model::Fermentable& f = document.recipes.front().fermentables.front();
    CHECK(f.yield.fineGrind && f.yield.fineGrind->valueIn(Unit::Percent) == Rational(100));
    CHECK(f.color.value() == Rational(0));
    CHECK(warned(document, "YIELD"));
    CHECK(warned(document, "COLOR"));
}

void testErrors() {
    // Malformed XML names the line.
    CHECK(errorOf("<RECIPES>\n<RECIPE></RECIPES>").find("line 2") == 0);
    CHECK(errorOf("<RECIPES>\n<RECIPE></RECIPES>").find("</RECIPES> closes <RECIPE>") != std::string::npos);
    CHECK(!errorOf("<RECIPES><RECIPE>").empty());
    CHECK(!errorOf("").empty());
    CHECK(!errorOf("not xml at all").empty());
    CHECK(!errorOf("<RECIPES/> trailing").empty());

    // A required field that is missing fails the import, and the message names it.
    CHECK(errorOf("<RECIPES><RECIPE><BATCH_SIZE>20</BATCH_SIZE></RECIPE></RECIPES>").find("has no <NAME>") != std::string::npos);
    CHECK(errorOf("<RECIPES><RECIPE><NAME>x</NAME></RECIPE></RECIPES>").find("has no <BATCH_SIZE>") != std::string::npos);
    CHECK(errorOf("<RECIPES><RECIPE><NAME>x</NAME><BATCH_SIZE>20</BATCH_SIZE><HOPS><HOP><NAME>h</NAME>"
                  "<AMOUNT>0.02</AMOUNT><USE>Boil</USE><TIME>60</TIME></HOP></HOPS></RECIPE></RECIPES>")
              .find("has no <ALPHA>") != std::string::npos);

    // A value that is not a number fails the import.
    CHECK(errorOf("<RECIPES><RECIPE><NAME>x</NAME><BATCH_SIZE>lots</BATCH_SIZE></RECIPE></RECIPES>")
              .find("\"lots\" is not a number") != std::string::npos);

    // Other roots and encodings are refused.
    CHECK(errorOf("<BEERJSON/>").find("must be <RECIPES> or <RECIPE>") != std::string::npos);
    CHECK(errorOf("<?xml version=\"1.0\" encoding=\"Shift_JIS\"?><RECIPES/>").find("unsupported encoding") !=
          std::string::npos);
    CHECK(errorOf("<RECIPES><RECIPE><NAME>&nbsp;</NAME></RECIPE></RECIPES>").find("unknown entity") !=
          std::string::npos);
}

void testLatin1IsConverted() {
    // "café" with the é as the single byte 0xE9, declared as ISO-8859-1.
    const std::string text =
        "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><RECIPE><NAME>caf\xE9</NAME><BATCH_SIZE>20</BATCH_SIZE></RECIPE>";
    const Document document = io::importBeerXml(text);
    CHECK(document.recipes.front().name == "caf\xC3\xA9");
}

// Export writes a grain's COLOR in degrees Lovibond and a liquid extract's in SRM, as BeerXML gives them.
void testExportedColorUnits() {
    model::Recipe recipe = io::importBeerXml(readSample("sample_beerxml.xml")).recipes.front();
    recipe.fermentables[0].color = Measure::fromUnit(Rational::fromDecimal("5.9"), "SRM");  // 5 Lovibond
    const std::string xml = io::exportBeerXml(recipe, calc::calculate(recipe));
    CHECK(xml.find("<COLOR>5</COLOR>") != std::string::npos);  // the grain, in Lovibond
    CHECK(xml.find("<COLOR>7</COLOR>") != std::string::npos);  // the liquid extract, still SRM

    // Read back, each color has its unit and value again.
    const Document again = io::importBeerXml(xml);
    const std::vector<model::Fermentable>& fermentables = again.recipes.front().fermentables;
    CHECK(fermentables[0].color.unit() == "Lovi");
    CHECK(fermentables[0].color.value() == Rational(5));
    CHECK(fermentables[2].color.unit() == "SRM");
    CHECK(fermentables[2].color.value() == Rational(7));
}

void testCApi() {
    const std::string xml = readSample("sample_beerxml.xml");
    char* error = nullptr;
    char* json = kl_beerxml_to_beerjson(xml.c_str(), &error);
    CHECK(error == nullptr);
    CHECK(json != nullptr);
    if (json != nullptr) {
        const std::string text(json);
        CHECK(text.find("{\"beerjson\":") == 0);
        CHECK(text.find("\"Sample Pale Ale\"") != std::string::npos);
        CHECK(text.find("\"warnings\"") != std::string::npos);
        kl_string_free(json);
    }

    char* failed = kl_beerxml_to_beerjson("<RECIPES><RECIPE>", &error);
    CHECK(failed == nullptr);
    CHECK(error != nullptr);
    kl_string_free(error);
}

}  // namespace

void runBeerXmlTests() {
    testSampleRecipe();
    testSingleRecipeRoot();
    testMissingOptionalFieldsAreDefaulted();
    testErrors();
    testLatin1IsConverted();
    testExportedColorUnits();
    testCApi();
}

}  // namespace kettlelogic::test
