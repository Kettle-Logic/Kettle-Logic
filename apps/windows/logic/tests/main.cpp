// Tests for the Windows app's portable logic: the engine adapter, the app's data, and its state. They run on any
// platform, so the logic is checked without a Windows machine.
#include "kettlelogic/app/app_updates.hpp"
#include "kettlelogic/app/brew_sheet.hpp"
#include "kettlelogic/app/catalog.hpp"
#include "kettlelogic/app/document_store.hpp"
#include "kettlelogic/app/engine.hpp"
#include "kettlelogic/app/equipment.hpp"
#include "kettlelogic/app/brew_day.hpp"
#include "kettlelogic/app/glossary.hpp"
#include "kettlelogic/app/help_markdown.hpp"
#include "kettlelogic/app/ingredient_draft.hpp"
#include "kettlelogic/app/ingredient_search.hpp"
#include "kettlelogic/app/planning.hpp"
#include "kettlelogic/app/readings_csv.hpp"
#include "kettlelogic/app/recipe_builder.hpp"
#include "kettlelogic/app/recipe_library.hpp"
#include "kettlelogic/app/recipe_text.hpp"
#include "kettlelogic/app/templates.hpp"
#include "kettlelogic/app/text.hpp"
#include "kettlelogic/app/tool_specs.hpp"
#include "kettlelogic/app/water_profiles.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace kettlelogic::app;
using kettlelogic::Quantity;
namespace calc = kettlelogic::calc;
namespace model = kettlelogic::model;
namespace fs = std::filesystem;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char* expression, const char* file, int line) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << file << ":" << line << ": check failed: " << expression << "\n";
    }
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

template <typename Fn>
bool throwsEngineError(Fn&& fn) {
    try {
        fn();
    } catch (const EngineError&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

fs::path scratchFolder(const char* name) {
    const fs::path folder = fs::temp_directory_path() / (std::string("kettlelogic-app-tests-") + name);
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

EntityId firstRecipe(const RecipeDocument& doc) {
    return doc.recipes().front().id;
}

// MARK: Ingredient search

void testIngredientSearch() {
    const std::vector<MenuEntry> hops = catalogEntries(IngredientKind::hop, UnitSystem::metric);
    CHECK(!hops.empty());
    const kettlelogic::catalog::IngredientEntry hop = searchEntry(hops.front());
    CHECK(hop.primary && hop.secondary);
    CHECK(!hop.custom && searchKindName(IngredientKind::hop) == "hop");

    MenuEntry lovi{"custom-1", "Custom", "House", IngredientDraft(IngredientKind::fermentable, UnitSystem::metric)};
    lovi.draft.colorValue = "10";
    lovi.draft.colorUnit = "Lovi";
    lovi.draft.yieldValue = "1.5";
    lovi.draft.yieldUnit = "plato";
    const kettlelogic::catalog::IngredientEntry malt = searchEntry(lovi);
    CHECK(malt.custom && malt.primary && std::abs(*malt.primary - 12.4) < 1e-9);
    CHECK(!malt.secondary);  // a yield in plato is not a gravity, so it is not sorted with them

    lovi.draft.colorUnit = "SRM";
    lovi.draft.colorValue = "";
    CHECK(!searchEntry(lovi).primary);
}

// MARK: Text

void testText() {
    CHECK(trimmed("  a b \t") == "a b");
    CHECK(parseNumber(" 5,25 ").value() == 5.25);
    CHECK(!parseNumber("5x").has_value());
    CHECK(!parseNumber("").has_value());
    CHECK(fixed(1.0005, 3) == "1.000" || fixed(1.0005, 3) == "1.001");
    CHECK(fixed(-0.0001, 2) == "0.00");
    CHECK(number(66.0, 0, 1) == "66");
    CHECK(number(66.25, 0, 1) == "66.3" || number(66.25, 0, 1) == "66.2");
    CHECK(number(5, 2, 2) == "5.00");
    CHECK(significant(0.0123456789) == "0.0123457");
    CHECK(significant(1234.5678) == "1234.57");
    CHECK(money(4.5, "USD") == "$4.50");
    CHECK(money(4.5, "CHF") == "4.50 CHF");
    CHECK(plural(1, "pack") == "1 pack" && plural(2, "pack") == "2 packs");

    const Units us{UnitSystem::us};
    CHECK(us.temperatureText(66) == "150.8 \xC2\xB0" "F");
    CHECK(Units{}.temperatureText(66) == "66 \xC2\xB0" "C");
    CHECK(us.volumeTextFromLiters("19") == "5.02");
    CHECK(us.litersTextFromVolume("5") == "18.927");
    CHECK(Units{}.litersTextFromVolume("5") == "5");
}

// MARK: Engine

void testEngineEditing() {
    auto doc = RecipeDocument::makeNew();
    CHECK(doc->recipes().size() == 1);
    const EntityId id = firstRecipe(*doc);
    CHECK(doc->isUnsaved(id));  // a new document has no saved copy
    CHECK(!doc->canUndo());

    doc->rename(id, "Pale Ale");
    CHECK(doc->recipe(id)->name == "Pale Ale");
    CHECK(doc->canUndo() && doc->undoLabel() != "");

    doc->addFermentable(id, {"2-Row", "grain", "4.5", "kg", "1.037", "sg", "2", "SRM"});
    doc->addHop(id, {"Cascade", "5.5", std::nullopt, "28", "g", std::string("pellet"), std::string("add_to_boil"),
                     std::string("60"), std::string("min")});
    doc->addCulture(id, {"US-05", "ale", "dry", "1", "pkg", std::string("78")});
    const auto& results = doc->results(id);
    CHECK(results.originalGravity && *results.originalGravity > 1.04);
    CHECK(results.ibu && *results.ibu > 5);
    CHECK(results.abv && *results.abv > 3);

    // A value the engine refuses is reported with its message, and changes nothing.
    const std::size_t hopsBefore = doc->recipe(id)->hops->size();
    CHECK(throwsEngineError([&] { doc->addHop(id, {"Bad", "x", std::nullopt, "1", "g", std::nullopt, std::nullopt, std::nullopt, std::nullopt}); }));
    CHECK(throwsEngineError([&] { doc->setBatchSize(id, "5", "furlongs"); }));
    CHECK(doc->recipe(id)->hops->size() == hopsBefore);

    // Editing in place keeps the item, and undo and redo walk back and forth.
    const EntityId hop = doc->recipe(id)->hops->front().id;
    doc->setHop(id, hop, {"Cascade", "6", std::nullopt, "30", "g", std::nullopt, std::string("add_to_boil"),
                          std::string("30"), std::string("min")});
    CHECK(describe(doc->recipe(id)->hops->front().alphaAcid).display() == "6 %");
    CHECK(doc->undo());
    CHECK(describe(doc->recipe(id)->hops->front().alphaAcid).display() == "5.5 %");
    CHECK(doc->redo());
    CHECK(describe(doc->recipe(id)->hops->front().alphaAcid).display() == "6 %");

    // Prices.
    doc->setPrice(id, IngredientKind::hop, hop, std::string("4.5"), std::string("oz"));
    CHECK(doc->results(id).cost.has_value());
    CHECK(throwsEngineError([&] { doc->setPrice(id, IngredientKind::hop, hop, std::string("4.5"), std::string("l")); }));
    doc->setPrice(id, IngredientKind::hop, hop, std::nullopt, std::nullopt);
    CHECK(!doc->results(id).cost.has_value());

    // Style, and the comparison against it.
    CHECK(!doc->styleComparison(id).has_value());
    doc->setStyle(id, std::string("18B"));
    const auto comparison = doc->styleComparison(id);
    CHECK(comparison && comparison->checks.size() == 5);
    CHECK(throwsEngineError([&] { doc->setStyle(id, std::string("99Z")); }));
    CHECK(bjcpStyles().size() > 50);

    // A user style is chosen like a guideline style, and removing it clears the recipe's choice.
    kettlelogic::styles::UserStyle house;
    house.name = "House Pale";
    house.ranges = {{1.040, 1.050}, {1.008, 1.012}, {20, 30}, {4, 7}, {4, 5.5}};
    const std::string houseId = doc->addUserStyle(house);
    CHECK(kettlelogic::styles::isUserStyleId(houseId));
    CHECK(doc->styleChoices().back().custom && doc->styleChoices().back().title == "House Pale");
    doc->setStyle(id, houseId);
    const auto own = doc->styleComparison(id);
    CHECK(own && own->name == "House Pale" && own->id == houseId && own->checks.size() == 5);
    house.id = houseId;
    house.name = "House Ale";
    doc->updateUserStyle(house);
    CHECK(doc->styleComparison(id)->name == "House Ale");
    doc->removeUserStyle(houseId);
    CHECK(!doc->recipe(id)->bjcpStyle && !doc->styleComparison(id).has_value());
    CHECK(throwsEngineError([&] { doc->setStyle(id, houseId); }));
    CHECK(throwsEngineError([&] { doc->addUserStyle(kettlelogic::styles::UserStyle{"", "  ", house.ranges}); }));

    // Duplicating a recipe puts the copy next to it.
    const EntityId copy = doc->duplicateRecipe(id);
    CHECK(doc->recipes().size() == 2 && doc->recipes()[1].id == copy);
    CHECK(doc->recipe(copy)->name == "Pale Ale copy");
    doc->removeRecipe(copy);
    CHECK(doc->recipes().size() == 1);
    CHECK(doc->undo() && doc->recipes().size() == 2);

    // Folders.
    const EntityId folder = doc->addFolder("IPAs");
    doc->moveRecipe(id, folder);
    CHECK(doc->recipe(id)->folder == folder);
    doc->removeFolder(folder);
    CHECK(!doc->recipe(id)->folder.has_value());
    CHECK(throwsEngineError([&] { doc->addFolder("  "); }));
}

void testGroupedEdits() {
    auto doc = RecipeDocument::makeNew();
    const EntityId id = firstRecipe(*doc);
    doc->grouped("Add Two", [&] {
        doc->addFermentable(id, {"A", "grain", "1", "kg", "1.037", "sg", "2", "SRM"});
        doc->addFermentable(id, {"B", "grain", "1", "kg", "1.037", "sg", "2", "SRM"});
    });
    CHECK(doc->recipe(id)->fermentables.size() == 2);
    CHECK(doc->undoLabel() == "Add Two");
    CHECK(doc->undo());
    CHECK(doc->recipe(id)->fermentables.empty());  // one undo took back both

    // A group that fails takes back what it did and leaves no redo.
    bool threw = false;
    try {
        doc->grouped("Add Bad", [&] {
            doc->addFermentable(id, {"A", "grain", "1", "kg", "1.037", "sg", "2", "SRM"});
            doc->addFermentable(id, {"B", "grain", "oops", "kg", "1.037", "sg", "2", "SRM"});
        });
    } catch (const EngineError&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(doc->recipe(id)->fermentables.empty());
    CHECK(!doc->canRedo());
    // A new group can start after a failed one.
    doc->grouped("Add One", [&] { doc->addFermentable(id, {"A", "grain", "1", "kg", "1.037", "sg", "2", "SRM"}); });
    CHECK(doc->recipe(id)->fermentables.size() == 1);
}

void testSaveAndSavedState() {
    const fs::path folder = scratchFolder("save");
    auto doc = RecipeDocument::makeNew();
    const EntityId id = firstRecipe(*doc);
    doc->rename(id, "Saved Ale");
    doc->addFermentable(id, {"2-Row", "grain", "4.5", "kg", "1.037", "sg", "2", "SRM"});
    doc->save(folder / "Saved Ale.json");
    CHECK(doc->file().has_value() && doc->displayName() == "Saved Ale");
    CHECK(!doc->isUnsaved(id));

    doc->rename(id, "Saved Ale 2");
    CHECK(doc->isUnsaved(id));
    CHECK(doc->undo());
    CHECK(!doc->isUnsaved(id));  // back to the saved text, so it is saved again

    // The planning text is part of the saved state, and survives a reopen with the sidecar.
    RecipePlanning planning;
    planning.notes = "Hello";
    planning.water.salts.push_back(SaltInput{});
    doc->setPlanning(id, planning.text());
    CHECK(doc->isUnsaved(id));
    doc->save(folder / "Saved Ale.json");

    auto reopened = RecipeDocument::open(folder / "Saved Ale.json");
    CHECK(reopened->source() == "current");
    CHECK(reopened->recipes().size() == 1);
    CHECK(!reopened->isUnsaved(firstRecipe(*reopened)));
    const RecipePlanning back = RecipePlanning::read(reopened->recipes().front().planning);
    CHECK(back.notes == "Hello" && back.water.salts.size() == 1);
    CHECK(reopened->results(firstRecipe(*reopened)).originalGravity.has_value());
    CHECK(throwsEngineError([&] { RecipeDocument::open(folder / "missing.json"); }));

    const std::string beerxml = reopened->exportRecipe(firstRecipe(*reopened), "beerxml");
    CHECK(beerxml.find("<") != std::string::npos);
    CHECK(reopened->exportRecipe(firstRecipe(*reopened), "text").find("Saved Ale") != std::string::npos);
    CHECK(reopened->exportRecipe(firstRecipe(*reopened), "beerjson").find("beerjson") != std::string::npos);
    CHECK(throwsEngineError([&] { reopened->exportRecipe(firstRecipe(*reopened), "pdf"); }));
    fs::remove_all(folder);
}

void testCalculations() {
    auto doc = RecipeDocument::makeNew();
    const EntityId id = firstRecipe(*doc);
    doc->addFermentable(id, {"2-Row", "grain", "4.5", "kg", "1.037", "sg", "2", "SRM"});
    doc->addCulture(id, {"US-05", "ale", "dry", "1", "pkg", std::string("78")});
    doc->addHop(id, {"Cascade", "5.5", std::nullopt, "28", "g", std::nullopt, std::string("add_to_boil"),
                     std::string("60"), std::string("min")});

    const auto steps = doc->procedure(id, 60, "all in one");
    CHECK(!steps.empty());
    CHECK(throwsEngineError([&] { doc->procedure(id, 0, "all in one"); }));
    CHECK(throwsEngineError([&] { doc->procedure(id, 60, "bucket"); }));

    const RecipePlanning planning;
    const auto plan = doc->brewingPlan(id, brewingInputs(planning.water, planning.mash));
    CHECK(plan.grainKg > 4 && plan.strikeLiters > 10);

    BrewSession session;
    session.originalGravity = 1.050;
    session.batchLiters = 19;
    const auto journal = doc->journalPlan(id, journalInputs(session));
    CHECK(journal.expectedOriginalGravity.has_value());
    CHECK(journal.measuredBrewhouseEfficiency.has_value());

    StarterSettings starter;
    const auto yeast = doc->starterPlan(id, starter);
    CHECK(yeast.cellsNeededBillion > 100);
    starter.packCells = "lots";
    CHECK(throwsEngineError([&] { doc->starterPlan(id, starter); }));
}

// MARK: Planning

void testPlanningJson() {
    RecipePlanning planning;
    planning.notes = "Mash \"low\"\nand slow";
    planning.starter.lager = true;
    planning.starter.starterLiters = "1.5";
    planning.water.baseName = "Custom";
    planning.water.base.calcium = 12.5;
    planning.water.useTarget = true;
    planning.mash.steps.push_back(MashStepSetting{});
    planning.mash.steps.back().name = "Mash out";
    planning.mash.steps.back().tempC = 76;
    BrewSession session;
    session.mashPh = 5.4;
    session.originalGravity = 1.052;
    session.servingTempC = std::nullopt;
    session.profileName = "Lager";
    session.profileSteps.push_back(ProfileStep{});
    FermentationReading reading;
    reading.temperatureC = 18.5;
    session.readings.push_back(reading);
    planning.sessions.push_back(session);

    const RecipePlanning back = RecipePlanning::read(planning.text());
    CHECK(back.notes == planning.notes);
    CHECK(back.starter == planning.starter);
    CHECK(back.water == planning.water);
    CHECK(back.mash == planning.mash);
    CHECK(back.sessions.size() == 1 && back.sessions[0] == session);
    CHECK(!back.sessions[0].servingTempC.has_value());

    // Text that does not read back gives the defaults, and so does the empty text.
    CHECK(RecipePlanning::read("not json").notes.empty());
    CHECK(RecipePlanning::read("").mash.steps.size() == 1);
    CHECK(RecipePlanning::read("[1,2]").mash.steps[0].name == "Saccharification");

    // Planning the macOS app wrote: a partial object keeps the defaults for what is missing.
    const RecipePlanning mac = RecipePlanning::read(
        R"({"notes":"from the Mac","mash":{"spargeTempC":80},"water":{"roPercent":50},)"
        R"("sessions":[{"id":"7C0F2A64-5B3D-4C58-9E4E-3C5A0B1D2E3F","name":"Session 1","brewDate":782000000.5,)"
        R"("carbonationMethod":"None","profileSteps":[],"readings":[{"id":"A","date":782100000,"gravity":1.01}]}]})");
    CHECK(mac.notes == "from the Mac");
    CHECK(mac.mash.spargeTempC == 80 && mac.mash.mashRatioLPerKg == 3);
    CHECK(mac.water.roPercent == 50);
    CHECK(mac.sessions.size() == 1 && mac.sessions[0].brewDate == 782000000.5);
    CHECK(mac.sessions[0].readings.size() == 1 && mac.sessions[0].readings[0].gravity == 1.01);
    CHECK(!mac.sessions[0].readings[0].temperatureC.has_value());

    CHECK(newUuidText().size() == 36);
    CHECK(newUuidText() != newUuidText());

    const CivilDate day{2026, 3, 14};
    CHECK(civilDateOf(referenceSecondsAtMidday(day)).day == 14);
    CHECK(daysBetween(referenceSecondsAtMidday({2026, 3, 1}), referenceSecondsAtMidday({2026, 3, 15})) == 14);
    CHECK(daysBetween(referenceSecondsAtMidday({2026, 3, 15}), referenceSecondsAtMidday({2026, 3, 1})) == -14);
}

void testReadingsCsv() {
    const double brew = referenceSecondsAtMidday({2026, 5, 1});
    std::vector<FermentationReading> readings;
    FermentationReading late;
    late.date = referenceSecondsAtMidday({2026, 5, 6});
    late.temperatureC = 19.5;
    late.gravity = 1.012;
    readings.push_back(late);
    FermentationReading early;
    early.date = referenceSecondsAtMidday({2026, 5, 2});
    early.gravity = 1.05;
    readings.push_back(early);

    const std::string csv = ReadingsCsv::exportReadings(readings, brew);
    CHECK(csv == "date,day,temperature_c,gravity_sg\n2026-05-02,1,,1.05\n2026-05-06,5,19.5,1.012\n");

    const auto parsed = ReadingsCsv::parse(csv + "junk,line\n2026-13-01\r\n\n");
    CHECK(parsed.readings.size() == 2);
    CHECK(parsed.skipped == 2);
    CHECK(parsed.readings[1].temperatureC == 19.5 && parsed.readings[1].gravity == 1.012);
    CHECK(!parsed.readings[0].temperatureC.has_value());
    CHECK(civilDateOf(parsed.readings[0].date).day == 2);
}

// MARK: Data

void testCatalogAndTemplates() {
    CHECK(Catalog::fermentables().size() > 90);
    CHECK(Catalog::hops().size() > 40);
    CHECK(Catalog::cultures().size() > 120);
    CHECK(Catalog::waters().size() == 8);
    CHECK(Catalog::miscellaneous().size() > 50);

    const auto& malts = Catalog::fermentables();
    CHECK(malts[0].name == "2-Row (Briess)" && malts[0].group == "Base malts" && malts[0].type == "grain");
    CHECK(Catalog::waters()[0].name == "Distilled or reverse osmosis");

    // The catalog's types and units are all ones the engine accepts.
    for (const auto& f : malts) {
        CHECK(std::find(fermentableTypes().begin(), fermentableTypes().end(), f.type) != fermentableTypes().end());
    }
    for (const auto& c : Catalog::cultures()) {
        CHECK(std::find(cultureTypes().begin(), cultureTypes().end(), c.type) != cultureTypes().end());
        CHECK(std::find(cultureForms().begin(), cultureForms().end(), c.form) != cultureForms().end());
    }
    for (const auto& m : Catalog::miscellaneous()) {
        CHECK(std::find(miscellaneousTypes().begin(), miscellaneousTypes().end(), m.type) != miscellaneousTypes().end());
        CHECK(std::find(miscellaneousUnits().begin(), miscellaneousUnits().end(), m.unit) != miscellaneousUnits().end());
    }

    CHECK(RecipeTemplate::all().size() >= 10);
    for (const RecipeTemplate& t : RecipeTemplate::all()) {
        const auto problems = t.problems();
        for (const std::string& p : problems) std::cerr << t.name << ": " << p << "\n";
        CHECK(problems.empty());
    }
    const auto tips = tipsIn(RecipeTemplate::all()[0], UnitSystem::us);
    CHECK(tips[0].find("{") == std::string::npos);
    CHECK(tips[0].find("\xC2\xB0" "F") != std::string::npos);

    CHECK(Equipment::builtIn().size() == 14);
    CHECK(Equipment::builtIn()[0].name == "Igloo cooler and stovetop kettle");
    CHECK(!explanation(Term::abv).empty() && explanation(Term::fermentationEfficiency).find("yeast") != std::string::npos);
    CHECK(explanation(Term::originalGravity).find("sugar") != std::string::npos);
}

void testToolSpecs() {
    std::set<std::string> names;
    for (const auto* list : {&calculators(), &converters()}) {
        for (const ToolKind kind : *list) {
            CHECK(names.insert(toolKindName(kind)).second);
            CHECK(toolKindFromName(toolKindName(kind)) == kind);
            const ToolSpec spec = toolSpec(kind);
            CHECK(!spec.title.empty() && !spec.sections.empty());

            // With its defaults, every tool runs, and every readout it shows has a value.
            for (const ToolSection& section : spec.sections) {
                if (section.readouts.empty()) {
                    continue;
                }
                const auto own = toolInputs(spec, section, {});
                try {
                    const auto output = runTool(section.tool, own);
                    for (const ToolReadout& readout : section.readouts) {
                        if (!readout.enabledBy.empty()) continue;
                        // Without chloride there is no sulfate to chloride ratio, and the form shows a dash.
                        const bool has = output.results.count(readout.key) != 0 || readout.key == "sulfateToChloride";
                        if (!has) std::cerr << spec.title << ": no result \"" << readout.key << "\" from " << section.tool << "\n";
                        CHECK(has);
                    }
                } catch (const std::exception& e) {
                    std::cerr << spec.title << " / " << section.title << ": " << e.what() << "\n";
                    CHECK(false);
                }
            }
        }
    }
    CHECK(names.size() == 25);

    const auto out = runTool("convert.temperature", {{"value", "100"}, {"from", "C"}, {"to", "F"}});
    CHECK(near(out.results.at("value"), 212, 1e-9));
    CHECK(throwsEngineError([] { runTool("no.such.tool", {}); }));
}

// MARK: Libraries

void testEquipmentLibrary() {
    const fs::path folder = scratchFolder("equipment");
    const fs::path file = folder / "custom-equipment.json";
    {
        EquipmentLibrary library(file);
        CHECK(library.custom().empty());
        EquipmentProfile mine = Equipment::builtIn()[0];
        mine.name = "  My Kettle ";
        mine.note = "note";
        library.save(mine, std::nullopt);
        CHECK(library.custom().size() == 1 && library.custom()[0].name == "My Kettle");
        CHECK(throwsEngineError([&] { library.save(mine, std::nullopt); }));            // the name is taken
        EquipmentProfile builtInName = mine;
        builtInName.name = Equipment::builtIn()[1].name;
        CHECK(throwsEngineError([&] { library.save(builtInName, std::nullopt); }));     // so is a built-in name
        mine.name = "Renamed";
        library.save(mine, std::string("My Kettle"));
        CHECK(library.custom().size() == 1 && library.custom()[0].name == "Renamed");
        library.duplicate("Renamed");
        library.duplicate("Renamed");
        CHECK(library.custom().size() == 3);
        CHECK(library.custom()[1].name == "Renamed copy" && library.custom()[2].name == "Renamed copy 2");
        CHECK(library.profile("nothing").name == Equipment::builtIn()[0].name);
        CHECK(library.all().size() == Equipment::builtIn().size() + 3);
        EquipmentProfile blank = mine;
        blank.name = " ";
        CHECK(throwsEngineError([&] { library.save(blank, std::nullopt); }));
    }
    EquipmentLibrary again(file);
    CHECK(again.custom().size() == 3 && again.custom()[0].note == "note");
    again.remove("Renamed copy");
    CHECK(again.custom().size() == 2);

    std::ofstream(file) << "not json";
    CHECK(EquipmentLibrary(file).custom().empty());
    fs::remove_all(folder);
}

void testCustomIngredients() {
    const fs::path folder = scratchFolder("ingredients");
    const fs::path file = folder / "custom-ingredients.json";
    {
        CustomIngredientStore store(file);
        IngredientDraft hop(IngredientKind::hop, UnitSystem::metric);
        hop.name = "  My Hop ";
        hop.alpha = "9.5";
        hop.price = "4";
        store.save(hop, std::nullopt);
        CHECK(store.ingredients().size() == 1 && store.ingredients()[0].draft.name == "My Hop");
        store.duplicate(store.ingredients()[0].id);
        CHECK(store.ingredients().size() == 2 && store.ingredients()[1].draft.name == "My Hop copy");
    }
    CustomIngredientStore again(file);
    CHECK(again.ingredients().size() == 2);
    CHECK(again.ingredients()[0].draft.kind == IngredientKind::hop && again.ingredients()[0].draft.alpha == "9.5");
    CHECK(again.ingredients()[0].draft.price == "4");
    const auto menu = menuEntries(IngredientKind::hop, again.ingredients(), UnitSystem::metric);
    CHECK(menu.size() == Catalog::hops().size() + 2 && menu.back().group == "Custom");
    again.remove(again.ingredients()[0].id);
    CHECK(again.ingredients().size() == 1);
    fs::remove_all(folder);
}

void testDraftsAndTiming() {
    // A draft round trips through the engine: add, read back, edit.
    auto doc = RecipeDocument::makeNew();
    const EntityId id = firstRecipe(*doc);
    IngredientDraft hop(IngredientKind::hop, UnitSystem::metric);
    hop.name = "Citra";
    hop.alpha = "12";
    hop.amount = "30";
    hop.price = "5";
    hop.priceUnit = "oz";
    CHECK(hop.isComplete(true));
    hop.commit(*doc, id, std::nullopt);
    const auto& added = doc->recipe(id)->hops->front();
    const IngredientDraft read = IngredientDraft::from(added, UnitSystem::metric);
    CHECK(read.name == "Citra" && read.alpha == "12" && read.amount == "30" && read.unit == "g");
    CHECK(read.use == "add_to_boil" && read.time == "60" && read.timeUnit == "min");
    CHECK(read.price == "5" && read.priceUnit == "oz");
    CHECK(doc->results(id).cost.has_value());

    IngredientDraft edited = read;
    edited.price = "";
    edited.amount = "40";
    edited.commit(*doc, id, added.id);
    CHECK(doc->recipe(id)->hops->size() == 1);
    CHECK(!doc->recipe(id)->hops->front().price.has_value());
    CHECK(describe(doc->recipe(id)->hops->front().amount).display() == "40 g");

    IngredientDraft empty(IngredientKind::fermentable, UnitSystem::us);
    CHECK(!empty.isComplete(true));
    CHECK(empty.unit == "lb" && empty.yieldValue == "1.036");
    CHECK(IngredientDraft(IngredientKind::water, UnitSystem::metric).isComplete(false) == false);  // no name yet

    // Picking a catalog entry keeps the amounts already typed.
    IngredientDraft fermentable(IngredientKind::fermentable, UnitSystem::metric);
    fermentable.amount = "3";
    const IngredientDraft adopted = fermentable.adopting(IngredientDraft::from(Catalog::fermentables()[3], UnitSystem::metric));
    CHECK(adopted.name == Catalog::fermentables()[3].name && adopted.amount == "3");
    CHECK(adopted.summary().find("yield") != std::string::npos);

    // Hop timing.
    IngredientDraft timing(IngredientKind::hop, UnitSystem::metric);
    CHECK(timingOf(timing) == HopTiming::start);
    chooseHopTiming(timing, HopTiming::flameout);
    CHECK(timingOf(timing) == HopTiming::flameout && timing.time == "0");
    chooseHopTiming(timing, HopTiming::dryHop);
    CHECK(timing.use == "add_to_fermentation" && timing.time == "3" && timing.timeUnit == "day");
    timing.time = "5";
    chooseHopTiming(timing, HopTiming::dryHop);
    CHECK(timing.time == "5");  // moving to a dry hop from a dry hop keeps its days
    chooseHopTiming(timing, HopTiming::fifteen);
    CHECK(timingOf(timing) == HopTiming::fifteen);
    timing.time = "17";
    CHECK(timingOf(timing) == HopTiming::custom);
    CHECK(!hopTimingSetting(HopTiming::custom).has_value());

    CHECK(nudgedAmount("28", "g", 1) == "33");
    CHECK(nudgedAmount("1", "oz", -1) == "0.75");
    CHECK(nudgedAmount("0", "lb", -1) == "0");
    CHECK(priceUnits("lb").size() == 4 && priceUnits("pkg")[0] == "pkg" && priceUnits("weird")[0] == "weird");
    CHECK(options({"a", "b"}, "c")[0] == "c" && options({"a", "b"}, "b").size() == 2);
}

void testWaterProfiles() {
    const fs::path folder = scratchFolder("water");
    const std::map<std::string, std::string> inputs = {
        {"volume", "20"}, {"calcium", "50"}, {"bicarbonate", "100"}, {"roPercent", "25"}, {"gypsum", "4"},
        {"bakingSoda", "2"}, {"useTarget", "1"}, {"targetCalcium", "140"}, {"targetSulfate", "300"}};
    auto profile = SavedWaterProfile::fromTool("Pale", inputs);
    CHECK(profile.has_value());
    CHECK(profile->salts.size() == 2 && near(profile->salts[0].gramsPerLiter, 0.2, 1e-12));
    CHECK(profile->target && profile->target->calcium == 140);
    CHECK(!SavedWaterProfile::fromTool("Zero", {{"volume", "0"}}).has_value());

    WaterSettings water;
    profile->apply(water, 15);
    CHECK(water.baseName == "Custom" && water.base.calcium == 50 && water.roPercent == 25);
    CHECK(water.salts.size() == 2 && near(water.salts[0].grams, 3, 1e-9) && water.salts[0].kind == "gypsum");
    CHECK(water.useTarget && water.target.sulfate == 300);

    {
        WaterProfileLibrary library(folder / "water-profiles.json");
        library.save(*profile);
        SavedWaterProfile replaced = *profile;
        replaced.id = "ignored";
        replaced.roPercent = 40;
        library.save(replaced);
        CHECK(library.profiles().size() == 1 && library.profiles()[0].roPercent == 40);
        CHECK(library.profiles()[0].id == profile->id);  // saving again keeps the saved profile's identity
    }
    WaterProfileLibrary again(folder / "water-profiles.json");
    CHECK(again.profiles().size() == 1 && again.profiles()[0].target.has_value());
    again.remove(again.profiles()[0].id);
    CHECK(again.profiles().empty());
    fs::remove_all(folder);
}

void testRecipeLibrary() {
    const fs::path home = scratchFolder("library");
    Preferences preferences;
    RecipeLibrary library(preferences, home);
    CHECK(library.folder() == home / "KettleLogic" / "Recipes");
    CHECK(library.recipeFiles().empty());  // a missing folder has no files, and is not created
    CHECK(!fs::exists(library.folder()));

    CHECK(RecipeLibrary::safeFileName("a/b:c\\d") == "a-b-c-d");
    CHECK(RecipeLibrary::safeFileName("  ") == "Recipe");
    CHECK(RecipeLibrary::safeFileName("What? A \"beer\"") == "What- A -beer-");
    CHECK(RecipeLibrary::safeFileName("Pale Ale...") == "Pale Ale");

    const fs::path first = library.fileFor("Pale Ale");
    CHECK(first.filename() == "Pale Ale.json" && fs::exists(library.folder()));
    std::ofstream(first) << "{}";
    CHECK(library.fileFor("Pale Ale").filename() == "Pale Ale 2.json");
    CHECK(library.fileFor("Pale Ale", first) == first);
    std::ofstream(library.folder() / "Pale Ale.kettlelogic.json") << "{}";
    std::ofstream(library.folder() / "Pale Ale.previous.json") << "{}";
    std::ofstream(library.folder() / "Pale Ale.autosave.json") << "{}";
    std::ofstream(library.folder() / "Pale Ale 10.json") << "{}";
    std::ofstream(library.folder() / "Pale Ale 9.json") << "{}";
    std::ofstream(library.folder() / "notes.txt") << "x";
    const auto files = library.recipeFiles();
    CHECK(files.size() == 3);
    CHECK(files[0].filename() == "Pale Ale.json" && files[1].filename() == "Pale Ale 9.json" &&
          files[2].filename() == "Pale Ale 10.json");  // natural order, so 9 comes before 10
    CHECK(library.contains(first));
    CHECK(!library.contains(home / "elsewhere" / "x.json"));
    RecipeLibrary::removeFiles(first);
    CHECK(!fs::exists(first) && !fs::exists(library.folder() / "Pale Ale.kettlelogic.json"));
    CHECK(naturalLess("a2", "a10") && !naturalLess("a10", "a2") && naturalLess("A", "b"));
    fs::remove_all(home);
}

// MARK: Builder

void testRecipeBuilder() {
    const EquipmentProfile profile = Equipment::builtIn()[0];
    for (const UnitSystem units : {UnitSystem::metric, UnitSystem::us}) {
        for (const RecipeTemplate& t : RecipeTemplate::all()) {
            auto doc = RecipeDocument::makeNew();
            RecipeBuildRequest request{t, units == UnitSystem::metric ? "20" : "5", profile, units};
            const EntityId id = RecipeBuilder::add(request, *doc);
            CHECK(doc->recipes().size() == 2);
            const auto& r = doc->results(id);
            CHECK(r.originalGravity.has_value());
            if (r.originalGravity) {
                // Amounts are rounded to what a brewer can measure, so the gravity lands close, not exact.
                if (!near(*r.originalGravity, t.targetOG, 0.003)) {
                    std::cerr << t.name << " OG " << *r.originalGravity << " vs " << t.targetOG << "\n";
                }
                CHECK(near(*r.originalGravity, t.targetOG, 0.003));
            }
            CHECK(r.ibu.has_value());
            if (r.ibu) {
                if (!near(*r.ibu, t.targetIBU, std::max(3.0, t.targetIBU * 0.12))) {
                    std::cerr << t.name << " IBU " << *r.ibu << " vs " << t.targetIBU << "\n";
                }
                CHECK(near(*r.ibu, t.targetIBU, std::max(3.0, t.targetIBU * 0.12)));
            }
            CHECK(doc->recipe(id)->bjcpStyle == t.bjcpStyle);
            CHECK(!RecipePlanning::read(doc->recipe(id)->planning).notes.empty());
            // The whole build is one undo step.
            CHECK(doc->undo());
            CHECK(doc->recipes().size() == 1);
        }
    }

    auto doc = RecipeDocument::makeNew();
    RecipeBuildRequest bad{RecipeTemplate::all()[0], "0", profile, UnitSystem::metric};
    CHECK(throwsEngineError([&] { RecipeBuilder::add(bad, *doc); }));
    CHECK(doc->recipes().size() == 1);  // a build that fails leaves nothing behind
    bad.batchValue = "x";
    CHECK(!bad.batchLiters().has_value());
}

// MARK: Store

void testDocumentStore() {
    const fs::path root = scratchFolder("store");
    AppPaths paths{root / "home", root / "data"};

    {
        DocumentStore store(paths);
        int notifications = 0;
        store.changed = [&] { ++notifications; };
        CHECK(store.documents().empty() && !store.selection());

        // With nothing selected, a new recipe starts a file in the recipe folder.
        store.newRecipe();
        CHECK(store.documents().size() == 1 && store.selection().has_value());
        CHECK(store.documents()[0]->file().has_value());
        CHECK(store.documents()[0]->file()->filename() == "New Recipe.json");
        CHECK(!store.errorMessage.has_value());
        CHECK(notifications > 0);

        // With one selected, it is added to that document.
        store.newRecipe();
        CHECK(store.documents().size() == 1 && store.documents()[0]->recipes().size() == 2);
        const auto doc = store.document();
        const ItemKey key = *store.selection();
        CHECK(doc && doc->recipe(key.id) != nullptr);

        // A rename saves under the new name, a moment after the change, and removes the old files.
        store.attempt([&] { doc->rename(doc->recipes().front().id, "Brown Ale"); });
        CHECK(store.saveStatus(*doc, doc->recipes().front().id) == "Saving\xE2\x80\xA6");
        store.tick(DocumentStore::Clock::now());  // not due yet
        CHECK(store.saveStatus(*doc, doc->recipes().front().id) == "Saving\xE2\x80\xA6");
        store.tick(DocumentStore::Clock::now() + std::chrono::seconds(5));
        CHECK(!store.errorMessage.has_value());
        CHECK(doc->file()->filename() == "Brown Ale.json");
        CHECK(fs::exists(paths.home / "KettleLogic" / "Recipes" / "Brown Ale.json"));
        CHECK(!fs::exists(paths.home / "KettleLogic" / "Recipes" / "New Recipe.json"));
        CHECK(store.saveStatus(*doc, doc->recipes().front().id) == "Saved");

        // Delete asks first, and moves the recipe to Recently Deleted, where it stays.
        store.requestDelete(key);
        CHECK(store.pendingDeletion.has_value());
        store.deleteRecipe(key);
        CHECK(!store.pendingDeletion && doc->recipes().size() == 2 && doc->isDeleted(key.id));
        CHECK(doc->folders().size() == 1 && doc->folders().front().trash);

        // A recipe in Recently Deleted asks first even with confirmation off, and removing it is undone.
        store.preferences.setBoolean(PrefKey::confirmDelete, false);
        store.requestDelete(key);
        CHECK(store.pendingDeletion.has_value() && doc->recipes().size() == 2);
        store.deleteRecipe(key);
        CHECK(!store.pendingDeletion && doc->recipes().size() == 1);
        store.undo();
        CHECK(doc->recipes().size() == 2 && doc->isDeleted(key.id));

        // Renaming goes through the document.
        store.renameRecipe(key, "Renamed Ale");
        CHECK(doc->recipe(key.id) != nullptr && doc->recipe(key.id)->name == "Renamed Ale");

        // Undo the rename, then the deletion, which also takes away the Recently Deleted folder.
        store.undo();
        store.undo();
        CHECK(doc->folders().empty() && !doc->isDeleted(key.id));

        // Folders.
        store.createFolder("Stouts", doc->id());
        CHECK(doc->folders().size() == 1);
        store.moveRecipe(ItemKey{doc->id(), doc->recipes().front().id}, doc->folders().front().id);
        CHECK(doc->recipes().front().folder.has_value());
        store.deleteFolder(ItemKey{doc->id(), doc->folders().front().id});
        CHECK(doc->folders().empty());

        // A failure is shown, not thrown, and the work it did is not kept.
        store.errorMessage.reset();
        store.attempt([&] { doc->setBatchSize(doc->recipes().front().id, "five", "gal"); });
        CHECK(store.errorMessage.has_value());

        // Libraries.
        store.showLibrary(LibraryPage::all().front());
        CHECK(!store.selection() && store.library().has_value());
        store.select(key.id == 0 ? std::nullopt : std::optional<ItemKey>(ItemKey{doc->id(), doc->recipes().front().id}));
        CHECK(!store.library().has_value());

        store.preferences.setString(PrefKey::units, "us");
        store.preferences.setBoolean(PrefKey::simpleMode, false);
    }

    {
        // A new launch opens the recipe folder, and the settings come back.
        DocumentStore store(paths);
        store.loadRecipeFolder();
        CHECK(store.documents().size() == 1 && store.documents()[0]->displayName() == "Brown Ale");
        CHECK(store.selection().has_value() && !store.errorMessage.has_value());
        CHECK(store.preferences.units() == UnitSystem::us);
        CHECK(!store.preferences.boolean(PrefKey::simpleMode));
        CHECK(!store.preferences.boolean(PrefKey::confirmDelete));
        CHECK(store.preferences.boolean(PrefKey::autosave));  // the default

        // Opening the same file again selects it, not a second copy.
        store.open(*store.documents()[0]->file());
        CHECK(store.documents().size() == 1);

        // A file that will not open is reported, and the others still open.
        std::ofstream(paths.home / "KettleLogic" / "Recipes" / "Broken.json") << "{ nope";
    }
    {
        DocumentStore store(paths);
        store.loadRecipeFolder();
        CHECK(store.documents().size() == 1);
        CHECK(store.errorMessage && store.errorMessage->find("Broken.json") != std::string::npos);
    }

    // The drag text for a recipe reads back.
    const ItemKey key{"1B2C-3D", 42};
    CHECK(ItemKey::fromDragText(key.dragText()) == key);
    CHECK(!ItemKey::fromDragText("nothing").has_value());
    CHECK(!ItemKey::fromDragText("a/b").has_value());
    fs::remove_all(root);
}

// MARK: Help, brew day, and recipe text

void testHelpMarkdown() {
    const std::string html = HelpMarkdown::html(
        "# Title <b>\n\nA *plain* paragraph with **bold**, `code <x>`, and [a link](02.md).\n\n- one\n  - nested\n- two\n\n"
        "1. first\n2. second\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n> quoted\n\n```\nraw <code>\n```\n\n---\n\nsnake_case_name and 2*3*4\n",
        "Help & more");
    CHECK(html.find("<title>Help &amp; more</title>") != std::string::npos);
    CHECK(html.find("<h1>Title &lt;b&gt;</h1>") != std::string::npos);
    CHECK(html.find("<em>plain</em>") != std::string::npos);
    CHECK(html.find("<strong>bold</strong>") != std::string::npos);
    CHECK(html.find("<code>code &lt;x&gt;</code>") != std::string::npos);
    CHECK(html.find("<a href=\"02.md\">a link</a>") != std::string::npos);
    CHECK(html.find("<ul>\n<li>one\n<ul>\n<li>nested") != std::string::npos);
    CHECK(html.find("<ol>") != std::string::npos);
    CHECK(html.find("<th>A</th>") != std::string::npos && html.find("<td>2</td>") != std::string::npos);
    CHECK(html.find("<blockquote><p>quoted</p></blockquote>") != std::string::npos);
    CHECK(html.find("<pre><code>raw &lt;code&gt;</code></pre>") != std::string::npos);
    CHECK(html.find("<hr>") != std::string::npos);
    CHECK(html.find("snake_case_name") != std::string::npos && html.find("snake<em>") == std::string::npos);
    CHECK(html.find("<script") == std::string::npos);

    const fs::path help = fs::path(KETTLELOGIC_TEST_DATA_DIR).parent_path().parent_path().parent_path() / "apps" / "macos" / "Help";
    const auto pages = helpPages(help);
    CHECK(pages.size() == 9);
    CHECK(!pages.empty() && pages[0].id == "01-getting-started.md" && !pages[0].title.empty());
}

void testBrewDayAndRecipeText() {
    BrewDayPlan plan;
    plan.recipeName = "Pale Ale";
    plan.boilMinutes = 60;
    calc::ProcedureStep a;
    a.phase = "Mash";
    a.text = "Heat the water";
    calc::ProcedureStep b;
    b.phase = "Boil";
    b.text = "Add the hops";
    b.timerMinutes = 60;
    plan.steps = {a, b};
    const std::string key = plan.progressKey();
    CHECK(key.rfind("brewDayProgress.", 0) == 0);
    BrewDayPlan same = plan;
    same.id = "other";
    CHECK(same.progressKey() == key);  // the same procedure finds its progress
    same.steps[1].text = "Add other hops";
    CHECK(same.progressKey() != key);  // a changed recipe starts clean

    Preferences preferences;
    CHECK(plan.loadProgress(preferences).empty());
    plan.saveProgress(preferences, {0, 1});
    CHECK((plan.loadProgress(preferences) == std::set<int>{0, 1}));
    plan.saveProgress(preferences, {});
    CHECK(plan.loadProgress(preferences).empty());
    CHECK(clockText(59.2) == "1:00" && clockText(0) == "0:00" && clockText(605) == "10:05");

    auto doc = RecipeDocument::makeNew();
    const EntityId id = firstRecipe(*doc);
    doc->addFermentable(id, {"2-Row", "grain", "4.5", "kg", "1.037", "sg", "2", "SRM"});
    doc->addHop(id, {"Cascade", "5.5", std::nullopt, "28", "g", std::nullopt, std::string("add_to_boil"),
                     std::string("60"), std::string("min")});
    doc->addCulture(id, {"US-05", "lager", "dry", "1", "pkg", std::string("78")});
    doc->addWater(id, {"Tap", "10", "20", "30", "40", "50", "60", std::string("20")});
    const auto& recipe = *doc->recipe(id);
    const auto& results = doc->results(id);
    CHECK(hopDetail(recipe.hops->front()) == "5.5 % AA \xC2\xB7 28 g \xC2\xB7 Boil 60 min");
    CHECK(fermentableDetail(recipe.fermentables.front()) == "4.5 kg \xC2\xB7 1.037 sg \xC2\xB7 2 SRM");
    CHECK(cultureDetail(recipe.cultures->front()) == "lager \xC2\xB7 dry \xC2\xB7 1 pkg \xC2\xB7 78 % attenuation");
    CHECK(waterDetail(recipe.waters->front()) == "Ca 10 Mg 60 Na 50 Cl 40 SO4 30 HCO3 20 mg/L \xC2\xB7 20 l");

    const model::Hop& hop = recipe.hops->front();
    CHECK(priceDetail(recipe, results, hop.price, hop.uuid).empty());
    doc->setPrice(id, IngredientKind::hop, hop.id, std::string("4"), std::string("oz"));
    const model::Hop& priced = doc->recipe(id)->hops->front();
    const std::string detail = priceDetail(*doc->recipe(id), doc->results(id), priced.price, priced.uuid);
    CHECK(detail.rfind(" \xC2\xB7 $4.00 / oz \xC2\xB7 $", 0) == 0);

    CHECK(near(*litersOf(recipe.batchSize), 18.92705892, 1e-6));
    CHECK(near(batchLiters(recipe), 18.92705892, 1e-6));
    CHECK(near(*gramsOf(hop.amount), 28, 1e-9));
    CHECK(!gramsOf(recipe.batchSize).has_value());

    const auto starter = starterPrefill(recipe, results);
    CHECK(starter.at("lager") == "1" && starter.count("originalGravity") == 1 && starter.at("batchLiters") == "18.93");
    const auto bitterness = bitternessPrefill(recipe, results, hop);
    CHECK(bitterness.at("alphaPercent") == "5.5" && bitterness.at("grams") == "28.00" &&
          bitterness.at("boilMinutes") == "60" && bitterness.count("gravity") == 1);

    CHECK(srmColor(0).r == 255 && srmColor(100).r == 18);
    const Rgb amber = srmColor(10);
    CHECK(amber.r == 248 && amber.g == 166 && amber.b == 0);
}

// The recipe's PDF comes from the engine. It is a PDF file with the recipe's sections, and it is not the brew sheet.
void testPdf() {
    auto doc = RecipeDocument::makeNew();
    const std::string pdf = doc->exportRecipePdf(firstRecipe(*doc));
    CHECK(pdf.rfind("%PDF-1.4", 0) == 0);
    CHECK(pdf.size() > 100 && pdf.compare(pdf.size() - 6, 6, "%%EOF\n") == 0);
    CHECK(pdf.find("(Estimates)") != std::string::npos);
    CHECK(pdf.find("(Brew Sheet)") == std::string::npos);
}

// A sheet's values land in the session's fields, a blank box keeps the session's value, and the readings go in
// with their dates, or the brew day when they have none.
void testBrewSheet() {
    BrewSession session;
    session.spargeRunoffPh = 5.6;
    session.brewDate = referenceSecondsAtMidday(CivilDate{2026, 10, 30});

    kettlelogic::calc::BrewSheetValues values;
    values.numbers = {{"B1", 5.4}, {"C2", 1.052}, {"C1", 25}, {"D2", 1.048}, {"E1", 1.012}, {"F2", 18.5}, {"F3", 1.040},
                      {"F6", 1.020}};
    values.dates = {{"A1", "2026-10-31"}, {"F1", "2026-11-01"}};
    applyBrewSheet(session, values);

    CHECK(session.mashPh == 5.4);
    CHECK(session.spargeRunoffPh == 5.6);  // the sheet left it blank
    CHECK(session.preBoilGravity == 1.052);
    CHECK(session.preBoilLiters == 25);
    CHECK(session.originalGravity == 1.048);
    CHECK(session.finalGravity == 1.012);
    CHECK(civilDateOf(session.brewDate).day == 31);

    CHECK(session.readings.size() == 2);
    if (session.readings.size() == 2) {
        const FermentationReading& first = session.readings[0];
        CHECK(civilDateOf(first.date).day == 1 && civilDateOf(first.date).month == 11);
        CHECK(first.temperatureC == 18.5 && first.gravity == 1.040);
        const FermentationReading& second = session.readings[1];
        CHECK(civilDateOf(second.date).day == 31);  // no date on the sheet: the brew day
        CHECK(!second.temperatureC.has_value() && second.gravity == 1.020);
    }

    // A sheet with no readings leaves the session's readings alone.
    kettlelogic::calc::BrewSheetValues none;
    applyBrewSheet(session, none);
    CHECK(session.readings.size() == 2);
}

}  // namespace

// MARK: App updates

namespace {

// A transport that serves a fixed body, or fails, and remembers the address it was asked for.
class FakeTransport : public updates::Transport {
public:
    FakeTransport(std::string body, bool fails) : body_(std::move(body)), fails_(fails) {}

    std::string get(const std::string& url) override {
        requested_ = url;
        if (fails_) {
            throw std::runtime_error("no connection");
        }
        return body_;
    }

    std::string requested_;

private:
    std::string body_;
    bool fails_;
};

template <typename Fn>
bool throwsRuntime(Fn&& fn) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

// The text of a file, or nothing if it cannot be read.
std::string readWhole(const fs::path& file) {
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// The value between `before` and `after` in `text`, such as the version in a CMake project line.
std::string between(const std::string& text, const std::string& before, const std::string& after) {
    const std::size_t start = text.find(before);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t from = start + before.size();
    const std::size_t end = text.find(after, from);
    return end == std::string::npos ? std::string{} : text.substr(from, end - from);
}

const char* kManifest = R"({
  "schema": 1,
  "app": {"version": "0.9.0", "engineAbi": 1, "url": "https://example.com/a.zip", "sha256": "00"},
  "windows": {"version": "0.2.0", "url": "https://example.com/Setup.exe",
              "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
  "linux": {"version": "0.2.0", "url": "https://example.com/release"}
})";

}  // namespace

void testAppUpdates() {
    using updates::Platform;
    using updates::Version;

    // Versions: numbers, not text, and the forms that are not versions.
    CHECK(Version::parse("0.2.0") == (Version{0, 2, 0}));
    CHECK(Version::parse("1.2") == (Version{1, 2, 0}));
    CHECK(Version::parse("1") == (Version{1, 0, 0}));
    CHECK(!Version::parse("").has_value());
    CHECK(!Version::parse("1.x.0").has_value());
    CHECK(!Version::parse("1.2.3.4").has_value());
    CHECK(!Version::parse("1..2").has_value());
    CHECK(!Version::parse("-1.0.0").has_value());
    CHECK(!Version::parse("1.0.0-beta").has_value());
    CHECK(Version::parse("0.10.0") > Version::parse("0.9.0"));
    CHECK(Version::parse("0.2.0") > Version::parse("0.1.9"));
    CHECK(Version::parse("1.2.3")->text() == "1.2.3");

    // The list: the entries for this app are read, and the others are ignored.
    const updates::Manifest manifest = updates::parseManifest(kManifest);
    CHECK(manifest.windows.has_value() && manifest.windows->version == (Version{0, 2, 0}));
    CHECK(manifest.linux.has_value() && manifest.linux->sha256.empty());
    CHECK(manifest.windows->url == "https://example.com/Setup.exe");

    // Refusals: each one names the problem.
    CHECK(throwsRuntime([] { updates::parseManifest("{"); }));
    CHECK(throwsRuntime([] { updates::parseManifest("[]"); }));
    CHECK(throwsRuntime([] { updates::parseManifest(R"({"app": {}})"); }));
    CHECK(throwsRuntime([] { updates::parseManifest(R"({"schema": 2})"); }));
    CHECK(throwsRuntime([] { updates::parseManifest(R"({"schema": 1, "windows": {"version": "0.2.0", "url": "x"}})"); }));
    CHECK(throwsRuntime([] { updates::parseManifest(R"({"schema": 1, "linux": {"version": "x", "url": "y"}})"); }));

    // Newer than this copy: the offer. Equal or older: nothing.
    CHECK(updates::newerRelease(manifest, Platform::windows, Version{0, 1, 0}).has_value());
    CHECK(!updates::newerRelease(manifest, Platform::windows, Version{0, 2, 0}).has_value());
    CHECK(!updates::newerRelease(manifest, Platform::windows, Version{0, 3, 0}).has_value());
    CHECK(!updates::newerRelease(updates::Manifest{}, Platform::linux, Version{0, 1, 0}).has_value());

    // The checksum.
    CHECK(updates::matchesSha256("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(!updates::matchesSha256("abd", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    // The check, through a transport. It asks for the given address, and never throws.
    FakeTransport current(kManifest, false);
    const updates::CheckResult offered = updates::check(current, Platform::windows, "https://example.com/u.json");
    CHECK(current.requested_ == "https://example.com/u.json");
    CHECK(offered.status == updates::CheckResult::Status::available);
    CHECK(offered.release.has_value() && offered.release->version == (Version{0, 2, 0}));

    FakeTransport failing(kManifest, true);
    const updates::CheckResult offline = updates::check(failing, Platform::windows);
    CHECK(offline.status == updates::CheckResult::Status::failed);
    CHECK(offline.message == "no connection");

    FakeTransport garbled("not json", false);
    CHECK(updates::check(garbled, Platform::linux).status == updates::CheckResult::Status::failed);

    // The version this build reports must be the one the platform projects declare.
    const std::string repo = KETTLELOGIC_REPO_ROOT;
    const std::string linuxVersion = between(readWhole(repo + "/apps/linux/CMakeLists.txt"),
                                             "project(KettleLogicGtk VERSION ", " ");
    const std::string windowsVersion = between(readWhole(repo + "/apps/windows/KettleLogic/KettleLogic.Paths.props"),
                                               "<KettleLogicUiVersion>", "</KettleLogicUiVersion>");
    CHECK(linuxVersion == updates::kAppVersion);
    CHECK(windowsVersion == updates::kAppVersion);
}

int main() {
    testText();
    testIngredientSearch();
    testEngineEditing();
    testGroupedEdits();
    testSaveAndSavedState();
    testCalculations();
    testPlanningJson();
    testReadingsCsv();
    testCatalogAndTemplates();
    testToolSpecs();
    testEquipmentLibrary();
    testCustomIngredients();
    testDraftsAndTiming();
    testWaterProfiles();
    testRecipeLibrary();
    testRecipeBuilder();
    testDocumentStore();
    testHelpMarkdown();
    testPdf();
    testBrewSheet();
    testBrewDayAndRecipeText();
    testAppUpdates();

    std::cout << checks << " checks, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
