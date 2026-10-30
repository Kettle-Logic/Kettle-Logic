#include "check.hpp"

#include "kettlelogic/c/kettlelogic.h"
#include "kettlelogic/calc/brew_sheet.hpp"
#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/io/brew_sheet_pdf.hpp"
#include "kettlelogic/model/beerjson.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace kettlelogic::test {

namespace {

namespace fs = std::filesystem;
using calc::BoxKind;
using calc::SheetUnits;

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

model::Recipe sampleRecipe() {
    const fs::path file = fs::path(KETTLELOGIC_TEST_DATA_DIR) / "sample_recipe.json";
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return model::readBeerJson(text.str()).recipes.at(0);
}

calc::BrewSheet sheetFor(SheetUnits units) {
    const model::Recipe recipe = sampleRecipe();
    return calc::brewSheet(recipe, calc::calculate(recipe), units);
}

std::map<std::string, std::string> typed(std::initializer_list<std::pair<const char*, const char*>> entries) {
    std::map<std::string, std::string> out;
    for (const auto& [code, text] : entries) out[code] = text;
    return out;
}

// The sheet has six sections and 28 boxes, and every code is unique.
void testLayout() {
    const calc::BrewSheet sheet = sheetFor(SheetUnits::Metric);
    CHECK(sheet.sections.size() == 6);
    std::size_t boxes = 0;
    for (const auto& section : sheet.sections) boxes += section.boxes.size();
    CHECK(boxes == 28);

    std::map<std::string, int> seen;
    for (const auto& section : sheet.sections) {
        for (const auto& b : section.boxes) seen[b.code] += 1;
    }
    CHECK(seen.size() == boxes);
    for (const auto& [code, count] : seen) CHECK(count == 1);

    // Reading row k is codes 3k-2 (date), 3k-1 (temperature), and 3k (gravity).
    CHECK(sheet.find("A1")->kind == BoxKind::Date);
    CHECK(sheet.find("F16")->kind == BoxKind::Date);
    CHECK(sheet.find("F17")->kind == BoxKind::Temperature);
    CHECK(sheet.find("F18")->kind == BoxKind::Gravity);
    CHECK(sheet.find("F19") == nullptr);
    CHECK(sheet.find("B1")->kind == BoxKind::Ph);
    CHECK(sheet.find("nope") == nullptr);
}

// The recipe's figures appear beside the boxes, and the units follow the sheet's system.
void testExpectedFiguresAndUnits() {
    const calc::BrewSheet metric = sheetFor(SheetUnits::Metric);
    const calc::BrewSheet imperial = sheetFor(SheetUnits::Imperial);

    CHECK(metric.find("C1")->unit == "L");
    CHECK(imperial.find("C1")->unit == "gal");
    CHECK(imperial.find("F17")->unit == "\xC2\xB0" "F");
    CHECK(metric.find("F17")->unit == "\xC2\xB0" "C");

    const auto& liters = metric.find("D1")->expected;
    const auto& gallons = imperial.find("D1")->expected;
    CHECK(liters && gallons);
    if (liters && gallons) CHECK(near(*gallons * 3.785411784, *liters, 1e-9));

    // The measured boxes expect nothing, and the boxes the recipe predicts do.
    CHECK(!metric.find("B1")->expected);
    CHECK(!metric.find("F1")->expected);
    CHECK(metric.find("D2")->expected.has_value());
    CHECK(!metric.find("D2")->expectedText.empty());
}

// Decimal commas read as decimal points. Gallons and Fahrenheit come back as liters and Celsius.
void testReadingValues() {
    const calc::BrewSheet metric = sheetFor(SheetUnits::Metric);
    calc::BrewSheetValues read = calc::readBrewSheet(metric, typed({{"B1", "5,42"}, {"C2", "1.052"}, {"D2", " 1.048 "}}));
    CHECK(read.issues.empty());
    CHECK(near(read.numbers.at("B1"), 5.42, 1e-12));
    CHECK(near(read.numbers.at("C2"), 1.052, 1e-12));
    CHECK(near(read.numbers.at("D2"), 1.048, 1e-12));

    const calc::BrewSheet imperial = sheetFor(SheetUnits::Imperial);
    read = calc::readBrewSheet(imperial, typed({{"C1", "5"}, {"F17", "68"}}));
    CHECK(read.issues.empty());
    CHECK(near(read.numbers.at("C1"), 5 * 3.785411784, 1e-9));
    CHECK(near(read.numbers.at("F17"), 20, 1e-9));
}

// Empty boxes are left out; dates are checked and kept as text.
void testEmptyAndDates() {
    const calc::BrewSheet metric = sheetFor(SheetUnits::Metric);
    calc::BrewSheetValues read = calc::readBrewSheet(metric, typed({{"B1", "   "}, {"B2", ""}}));
    CHECK(read.numbers.empty() && read.issues.empty());

    read = calc::readBrewSheet(metric, typed({{"A1", "2026-10-30"}}));
    CHECK(read.issues.empty());
    CHECK(read.dates.at("A1") == "2026-10-30");

    read = calc::readBrewSheet(metric, typed({{"A1", "2026-13-01"}, {"F16", "30/10/2026"}}));
    CHECK(read.issues.size() == 2);
    CHECK(read.dates.empty());
}

// Text the sheet cannot take becomes an issue, and the other boxes are still read.
void testIssues() {
    const calc::BrewSheet metric = sheetFor(SheetUnits::Metric);
    const calc::BrewSheetValues read = calc::readBrewSheet(metric, typed({
        {"B1", "abc"},           // not a number
        {"B2", "5.4 pH"},        // trailing text
        {"D2", "1050"},          // a gravity typed without its decimal point
        {"Z9", "1"},             // no such box
        {"C2", "1.052"},         // fine
    }));
    CHECK(read.issues.size() == 4);
    CHECK(read.numbers.size() == 1);
    CHECK(read.numbers.count("C2") == 1);
    for (const auto& issue : read.issues) CHECK(!issue.message.empty());
}

// The PDF is one page, uses the built-in Helvetica faces, and its cross-reference table points at each object.
void testPdf() {
    const calc::BrewSheet metric = sheetFor(SheetUnits::Metric);
    const std::string pdf = io::brewSheetPdf(metric);
    CHECK(pdf.rfind("%PDF-1.4", 0) == 0);
    CHECK(pdf.find("/Count 1") != std::string::npos);
    CHECK(pdf.find("/BaseFont /Helvetica ") != std::string::npos);
    CHECK(pdf.find("(Brew Sheet)") != std::string::npos);
    CHECK(pdf.find("(F18)") != std::string::npos);
    CHECK(pdf.size() >= 5 && pdf.compare(pdf.size() - 6, 6, "%%EOF\n") == 0);

    // Each object's offset in the table must land on "n 0 obj".
    // "startxref" also ends in "xref\n", so the table is the one that starts its own line.
    const std::size_t table = pdf.rfind("\nxref\n");
    CHECK(table != std::string::npos);
    const std::size_t xref = table + 1;
    std::size_t entry = pdf.find("\n", xref + 5) + 1;  // skip "0 8"
    entry = pdf.find("\n", entry) + 1;                   // skip the free entry
    for (int object = 1; object <= 7; ++object) {
        const std::size_t offset = std::strtoull(pdf.c_str() + entry, nullptr, 10);
        const std::string header = std::to_string(object) + " 0 obj";
        CHECK(pdf.compare(offset, header.size(), header) == 0);
        entry = pdf.find("\n", entry) + 1;
    }

    const fs::path file = fs::temp_directory_path() / "kettlelogic-brew-sheet-test.pdf";
    io::writeBrewSheetPdf(metric, file);
    CHECK(fs::file_size(file) == pdf.size());
    fs::remove(file);
}

// The facade gives the same layout as the engine, and reads text back through it.
void testCApi() {
    char* error = nullptr;
    kl_document* doc = kl_document_new(&error);
    CHECK(doc != nullptr);
    if (doc == nullptr) return;

    kl_recipe_id recipe = 0;
    CHECK(kl_add_recipe(doc, &recipe, &error) == 0);

    char* json = kl_brew_sheet_json(doc, recipe, 1, &error);
    CHECK(json != nullptr);
    if (json != nullptr) {
        const std::string text = json;
        CHECK(text.find("\"units\":\"imperial\"") != std::string::npos);
        CHECK(text.find("\"code\":\"F18\"") != std::string::npos);
        kl_string_free(json);
    }

    char* read = kl_brew_sheet_read_json(doc, recipe, 0, "{\"C2\":\"1,052\",\"D2\":\"1050\",\"Z9\":\"1\"}", &error);
    CHECK(read != nullptr);
    if (read != nullptr) {
        const std::string text = read;
        CHECK(text.find("\"C2\":1.052") != std::string::npos);
        CHECK(text.find("\"issues\":[") != std::string::npos);
        kl_string_free(read);
    }

    CHECK(kl_brew_sheet_read_json(doc, recipe, 0, "[1]", &error) == nullptr);
    kl_string_free(error);
    error = nullptr;

    const fs::path file = fs::temp_directory_path() / "kettlelogic-brew-sheet-capi-test.pdf";
    CHECK(kl_brew_sheet_write_pdf(doc, recipe, 0, file.string().c_str(), &error) == 0);
    CHECK(fs::exists(file));
    fs::remove(file);
    kl_document_free(doc);
}

}  // namespace

void runBrewSheetTests() {
    testLayout();
    testExpectedFiguresAndUnits();
    testReadingValues();
    testEmptyAndDates();
    testIssues();
    testPdf();
    testCApi();
}

}  // namespace kettlelogic::test
