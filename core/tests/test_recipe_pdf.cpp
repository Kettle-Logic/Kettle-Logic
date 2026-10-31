#include "check.hpp"

#include "kettlelogic/c/kettlelogic.h"
#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/io/brew_sheet_pdf.hpp"
#include "kettlelogic/io/recipe_pdf.hpp"
#include "kettlelogic/model/beerjson.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace kettlelogic::test {

namespace {

namespace fs = std::filesystem;
using kettlelogic::Quantity;
using kettlelogic::Rational;
using kettlelogic::Unit;

model::Document sampleDocument() {
    const fs::path file = fs::path(KETTLELOGIC_TEST_DATA_DIR) / "sample_recipe.json";
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return model::readBeerJson(text.str());
}

model::Recipe sampleRecipe() {
    return sampleDocument().recipes.at(0);
}

// A recipe with every ingredient kind and a mash, which is the sample recipe's shape.
std::string pdfOf(const model::Recipe& recipe) {
    return io::recipePdf(model::Document{}, recipe, calc::calculate(recipe));
}

// The number of pages the file's /Count says it has.
int pageCount(const std::string& pdf) {
    const std::size_t at = pdf.find("/Type /Pages");
    if (at == std::string::npos) return -1;
    const std::size_t count = pdf.find("/Count ", at);
    return count == std::string::npos ? -1 : std::atoi(pdf.c_str() + count + 7);
}

// Recipes with an empty name or no ingredients still make a PDF, with no sections for what they lack.
void testEmptyRecipe() {
    model::Recipe recipe;
    recipe.name = "Empty";
    recipe.batchSize = Quantity::fromUnit(Rational(20), Unit::Liter);
    CHECK(recipe.efficiency.brewhouse.valueIn(Unit::Percent).toDouble() == 72);  // the default needs no setting
    const std::string pdf = pdfOf(recipe);
    CHECK(pdf.rfind("%PDF-1.4", 0) == 0);
    CHECK(pdf.find("(Estimates)") != std::string::npos);
    CHECK(pdf.find("(Hops)") == std::string::npos);
    CHECK(pdf.find("(Mash)") == std::string::npos);
    CHECK(pageCount(pdf) == 1);
    CHECK(pdf.size() >= 6 && pdf.compare(pdf.size() - 6, 6, "%%EOF\n") == 0);
}

// The sample recipe has its ingredients, its estimates, and its title, and it is not a brew sheet.
void testSampleRecipe() {
    const model::Recipe recipe = sampleRecipe();
    const std::string pdf = pdfOf(recipe);
    CHECK(pdf.rfind("%PDF-1.4", 0) == 0);
    CHECK(pdf.find("/Title (Recipe: " + recipe.name + ")") != std::string::npos);
    CHECK(pdf.find("(Estimates)") != std::string::npos);
    CHECK(pdf.find("(Fermentables)") != std::string::npos);
    CHECK(pdf.find("(Hops)") != std::string::npos);
    CHECK(pdf.find("(Mash)") != std::string::npos);
    CHECK(pdf.find("(ORIGINAL GRAVITY)") != std::string::npos);
    CHECK(pdf.find("(BATCH SIZE)") != std::string::npos);
    CHECK(pdf.find("(Add to boil)") != std::string::npos);
    // The fermentation and the notes run onto a second page.
    CHECK(pageCount(pdf) == 2);
    CHECK(pdf.find("(Fermentation)") != std::string::npos);

    // The brew sheet's boxes and instructions are not in the recipe.
    CHECK(pdf.find("Brew Sheet") == std::string::npos);
    CHECK(pdf.find("(F18)") == std::string::npos);
    CHECK(pdf.find("Write each value in pencil") == std::string::npos);
}

// A recipe with more hops than fit on a page runs onto a second page, which has a running head and its own number.
void testPagination() {
    model::Recipe recipe = sampleRecipe();
    const Quantity alpha = Quantity::fromUnit(Rational(10), Unit::Percent);
    for (int i = 0; i < 120; ++i) {
        model::Hop hop;
        hop.uuid = Uuid::generate();
        hop.name = "Test hop " + std::to_string(i);
        hop.alphaAcid = alpha;
        hop.amount = Quantity::fromUnit(Rational(15), Unit::Gram);
        hop.timing.time = Quantity::fromUnit(Rational(10), Unit::Minute);
        hop.timing.use = "boil";
        if (!recipe.hops) recipe.hops = std::vector<model::Hop>{};
        recipe.hops->push_back(hop);
    }
    const std::string pdf = pdfOf(recipe);
    CHECK(pageCount(pdf) > 1);
    CHECK(pdf.find("(Page 2 of " + std::to_string(pageCount(pdf)) + ")") != std::string::npos);
    CHECK(pdf.find("(Test hop 119)") != std::string::npos);

    // Every page's operators go in its own content stream, so the number of streams is the number of pages.
    std::size_t streams = 0;
    for (std::size_t at = pdf.find("stream\n"); at != std::string::npos; at = pdf.find("stream\n", at + 1)) {
        if (at >= 3 && pdf.compare(at - 3, 3, "end") == 0) continue;
        ++streams;
    }
    CHECK(streams == static_cast<std::size_t>(pageCount(pdf)));
}

// A recipe in US gallons gives its volumes in gallons, and one in liters gives them in liters.
void testUnits() {
    model::Recipe recipe = sampleRecipe();
    recipe.batchSize = Quantity::fromUnit(Rational(5), Unit::Gallon);
    const std::string gallons = pdfOf(recipe);
    CHECK(gallons.find("(5.00 gal)") != std::string::npos);

    recipe.batchSize = Quantity::fromUnit(Rational(20), Unit::Liter);
    const std::string liters = pdfOf(recipe);
    CHECK(liters.find("(20.0 L)") != std::string::npos);
    CHECK(liters.find("(5.00 gal)") == std::string::npos);
}

// Text in the recipe's name is escaped, so it cannot end the PDF string it sits in.
void testEscaping() {
    model::Recipe recipe;
    recipe.name = "Pale (Ale)\\";
    recipe.batchSize = Quantity::fromUnit(Rational(20), Unit::Liter);
    const std::string pdf = pdfOf(recipe);
    CHECK(pdf.find("Pale \\(Ale\\)\\\\") != std::string::npos);
}

// The file is written to disk, and a path that cannot be written throws with a message the user can read.
void testWriteFile() {
    const model::Recipe recipe = sampleRecipe();
    const model::Document document{};
    const fs::path file = fs::temp_directory_path() / "kettlelogic-recipe-pdf-test.pdf";
    io::writeRecipePdf(document, recipe, calc::calculate(recipe), file);
    {
        // Closed before the remove below: Windows will not delete a file that is still open.
        std::ifstream in(file, std::ios::binary);
        std::stringstream bytes;
        bytes << in.rdbuf();
        CHECK(bytes.str() == io::recipePdf(document, recipe, calc::calculate(recipe)));
    }
    fs::remove(file);

    CHECK(throws([&] {
        io::writeRecipePdf(document, recipe, calc::calculate(recipe), "/nonexistent-folder/recipe.pdf");
    }));
}

void testCApi() {
    char* error = nullptr;
    kl_document* doc = kl_document_new(&error);
    CHECK(doc != nullptr);
    if (doc == nullptr) return;

    kl_recipe_id recipe = 0;
    CHECK(kl_add_recipe(doc, &recipe, &error) == 0);

    const fs::path file = fs::temp_directory_path() / "kettlelogic-recipe-pdf-capi-test.pdf";
    CHECK(kl_recipe_write_pdf(doc, recipe, file.string().c_str(), &error) == 0);
    {
        std::ifstream in(file, std::ios::binary);
        std::string head(8, '\0');
        in.read(head.data(), 8);
        CHECK(head == "%PDF-1.4");
    }
    fs::remove(file);

    CHECK(kl_recipe_write_pdf(doc, recipe, "/nonexistent-folder/recipe.pdf", &error) != 0);
    CHECK(error != nullptr);
    kl_string_free(error);
    error = nullptr;

    kl_document_free(doc);
}

}  // namespace

void runRecipePdfTests() {
    testEmptyRecipe();
    testSampleRecipe();
    testPagination();
    testUnits();
    testEscaping();
    testWriteFile();
    testCApi();
}

}  // namespace kettlelogic::test
