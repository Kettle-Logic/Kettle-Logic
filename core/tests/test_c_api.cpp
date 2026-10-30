#include "check.hpp"

#include "kettlelogic/c/kettlelogic.h"
#include "kettlelogic/version.hpp"

#include <cstdlib>
#include <cstddef>
#include <filesystem>
#include <string>

namespace kettlelogic::test {

namespace {

// Takes ownership of a facade string and returns it as a std::string.
std::string take(char* text) {
    std::string out = text != nullptr ? text : "";
    kl_string_free(text);
    return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The id of the item named `name`. Each item's "id" comes just before its "name" in the snapshot.
kl_ingredient_id idOf(const std::string& snapshot, const std::string& name) {
    const std::size_t named = snapshot.find("\"name\":\"" + name + "\"");
    if (named == std::string::npos) {
        return 0;
    }
    const std::size_t key = snapshot.rfind("\"id\":", named);
    return key == std::string::npos ? 0 : std::strtoull(snapshot.c_str() + key + 5, nullptr, 10);
}

// The snapshot is JSON, so the checks look for the exact text it must hold.
void testNewDocumentAndSnapshot() {
    char* error = nullptr;
    kl_document* doc = kl_document_new(&error);
    CHECK(doc != nullptr);
    CHECK(error == nullptr);
    const std::string snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"name\":\"New Recipe\""));
    CHECK(contains(snapshot, "\"type\":\"all grain\""));
    CHECK(contains(snapshot, "\"value\":\"5\""));
    CHECK(contains(snapshot, "\"fermentationEfficiency\":{\"value\":\"92\""));
    kl_document_free(doc);
}

// Edits made in a group undo as one step, and a canceled group leaves nothing behind.
void testGroupedEditsAreOneUndoStep() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_group_begin(doc, nullptr) == 0);
    CHECK(kl_group_begin(doc, nullptr) == -1);
    CHECK(kl_set_recipe_name(doc, 1, "Grouped", nullptr) == 0);
    CHECK(kl_set_recipe_type(doc, 1, "extract", nullptr) == 0);
    CHECK(kl_add_miscellaneous(doc, 1, "Whirlfloc tablet", "fining", "1", "each", nullptr) == 0);
    CHECK(kl_group_end(doc, "New Recipe from Style", nullptr) == 0);
    CHECK(kl_group_end(doc, "again", nullptr) == -1);
    CHECK(take(kl_undo_label(doc)) == "New Recipe from Style");

    CHECK(kl_undo(doc, nullptr) == 1);
    std::string snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"name\":\"New Recipe\""));
    CHECK(!contains(snapshot, "Whirlfloc"));
    CHECK(kl_can_undo(doc) == 0);

    CHECK(kl_redo(doc, nullptr) == 1);
    snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"name\":\"Grouped\""));
    CHECK(contains(snapshot, "Whirlfloc"));

    // A canceled group is undone and cannot be redone.
    CHECK(kl_group_begin(doc, nullptr) == 0);
    CHECK(kl_set_recipe_name(doc, 1, "Canceled", nullptr) == 0);
    CHECK(kl_group_cancel(doc, nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"Grouped\""));
    CHECK(kl_can_redo(doc) == 0);
    kl_document_free(doc);
}

// Duplicating a recipe reports the copy's id, and undo removes the copy.
void testDuplicateThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    kl_recipe_id copy = 0;
    CHECK(kl_duplicate_recipe(doc, 1, &copy, nullptr) == 0);
    CHECK(copy != 0 && copy != 1);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"New Recipe copy\""));
    CHECK(take(kl_undo_label(doc)) == "Duplicate Recipe");
    CHECK(kl_undo(doc, nullptr) == 1);
    CHECK(!contains(take(kl_document_snapshot_json(doc)), "New Recipe copy"));
    CHECK(kl_duplicate_recipe(doc, 999, &copy, nullptr) == -1);
    kl_document_free(doc);
}

// Renames, undo, and redo go through the command stack, and their labels are exposed.
void testCommandsUndoAndRedo() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_can_undo(doc) == 0);
    CHECK(kl_set_recipe_name(doc, 1, "Pale Ale", nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"Pale Ale\""));
    CHECK(take(kl_undo_label(doc)) == "Rename Recipe");

    CHECK(kl_undo(doc, nullptr) == 1);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"New Recipe\""));
    CHECK(kl_can_redo(doc) == 1);
    CHECK(kl_redo(doc, nullptr) == 1);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"Pale Ale\""));
    CHECK(kl_redo(doc, nullptr) == 0);
    kl_document_free(doc);
}

// A bad value is refused with a message, and nothing is recorded.
void testBadInputIsRefused() {
    kl_document* doc = kl_document_new(nullptr);
    char* error = nullptr;
    CHECK(kl_set_batch_size(doc, 1, "five", "gal", &error) == -1);
    CHECK(error != nullptr);
    kl_string_free(error);

    error = nullptr;
    CHECK(kl_set_batch_size(doc, 1, "5", "furlongs", &error) == -1);
    CHECK(error != nullptr && contains(std::string(error), "unknown unit"));
    kl_string_free(error);

    CHECK(kl_set_recipe_type(doc, 1, "soup", nullptr) == -1);
    CHECK(kl_set_fermentation_efficiency(doc, 1, "0", nullptr) == -1);
    CHECK(kl_set_recipe_name(doc, 999, "Ghost", nullptr) == -1);
    CHECK(kl_can_undo(doc) == 0);
    kl_document_free(doc);
}

// A drink recipe's type and batch size reach the snapshot, and the drink plan is in it.
void testDrinkThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_set_recipe_type(doc, 1, "cider", nullptr) == 0);
    CHECK(kl_set_batch_size(doc, 1, "5", "gal", nullptr) == 0);
    const std::string snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"type\":\"cider\""));
    CHECK(contains(snapshot, "\"drink\":{"));
    kl_document_free(doc);
}

// Recipes and ingredients are added and removed through the facade, and undo covers them.
void testIngredientsThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);

    kl_recipe_id added = 0;
    char* error = nullptr;
    CHECK(kl_add_recipe(doc, &added, &error) == 0);
    CHECK(error == nullptr);
    CHECK(added != 0 && added != 1);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"New Recipe\""));
    CHECK(take(kl_undo_label(doc)) == "Add Recipe");
    CHECK(kl_undo(doc, nullptr) == 1);
    CHECK(kl_add_recipe(doc, &added, nullptr) == 0);
    CHECK(kl_redo(doc, nullptr) == 0);

    CHECK(kl_add_fermentable(doc, 1, "Pilsner", "grain", "2", "kg", "1.037", "sg", "2", "SRM", nullptr) == 0);
    const std::string withMalt = take(kl_document_snapshot_json(doc));
    CHECK(contains(withMalt, "\"name\":\"Pilsner\""));
    CHECK(contains(withMalt, "\"amount\":{\"value\":\"2\",\"unit\":\"kg\"}"));

    // Bad input is refused with a message, and the fermentable is not added.
    error = nullptr;
    CHECK(kl_add_fermentable(doc, 1, "Soup", "soup", "1", "kg", "1.037", "sg", "2", "SRM", &error) == -1);
    CHECK(error != nullptr && contains(std::string(error), "not a fermentable type"));
    kl_string_free(error);
    CHECK(kl_add_fermentable(doc, 1, "Pale", "grain", "1", "kg", "1.037", "sg", "not srm", "SRM", nullptr) == -1);

    // Miscellaneous ingredients follow the same path.
    CHECK(kl_add_miscellaneous(doc, 1, "Irish moss", "fining", "1", "each", nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"Irish moss\""));
    CHECK(kl_add_miscellaneous(doc, 1, "Mystery", "soup", "1", "g", nullptr) == -1);

    // Removing the ingredient by its id takes it out of the snapshot, and undo puts it back.
    const kl_ingredient_id irish = idOf(take(kl_document_snapshot_json(doc)), "Irish moss");
    CHECK(irish != 0);
    CHECK(kl_remove_miscellaneous(doc, 1, irish, nullptr) == 0);
    CHECK(!contains(take(kl_document_snapshot_json(doc)), "\"name\":\"Irish moss\""));
    CHECK(kl_undo(doc, nullptr) == 1);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"Irish moss\""));
    kl_document_free(doc);
}

// Hops, cultures, and waters go through the facade, and a hop changes the bitterness.
void testHopsCulturesAndWatersThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_add_fermentable(doc, 1, "Pale", "grain", "4", "kg", "1.037", "sg", "3", "SRM", nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"ibu\":0"));

    char* error = nullptr;
    CHECK(kl_add_hop(doc, 1, "Cascade", "5.5", nullptr, "30", "g", "pellet", "add_to_boil", "60", "min", &error) == 0);
    CHECK(error == nullptr);
    const std::string withHop = take(kl_document_snapshot_json(doc));
    CHECK(contains(withHop, "\"name\":\"Cascade\""));
    CHECK(contains(withHop, "\"use\":\"add_to_boil\""));
    CHECK(!contains(withHop, "\"ibu\":0,"));
    CHECK(take(kl_undo_label(doc)) == "Add Hop");

    // The optional values may be left out.
    CHECK(kl_add_hop(doc, 1, "Plain", "4", nullptr, "10", "g", nullptr, nullptr, nullptr, nullptr, nullptr) == 0);
    CHECK(kl_add_hop(doc, 1, "Half", "4", nullptr, "10", "g", nullptr, nullptr, "60", nullptr, nullptr) == -1);
    error = nullptr;
    CHECK(kl_add_hop(doc, 1, "Odd", "4", nullptr, "10", "g", "gummy", nullptr, nullptr, nullptr, &error) == -1);
    CHECK(error != nullptr && contains(std::string(error), "not a hop form"));
    kl_string_free(error);

    CHECK(kl_add_culture(doc, 1, "US-05", "ale", "dry", "1", "pkg", "78", nullptr) == 0);
    CHECK(kl_add_culture(doc, 1, "Plain", "ale", "dry", "1", "pkg", nullptr, nullptr) == 0);
    CHECK(kl_add_culture(doc, 1, "Soup", "soup", "dry", "1", "pkg", nullptr, nullptr) == -1);

    CHECK(kl_add_water(doc, 1, "Tap", "50", "100", "30", "20", "10", "5", "20", nullptr) == 0);
    CHECK(kl_add_water(doc, 1, "Soft", "5", "5", "5", "5", "5", "5", nullptr, nullptr) == 0);
    CHECK(kl_add_water(doc, 1, "Bad", "five", "5", "5", "5", "5", "5", nullptr, nullptr) == -1);
    const std::string all = take(kl_document_snapshot_json(doc));
    CHECK(contains(all, "\"name\":\"US-05\""));
    CHECK(contains(all, "\"name\":\"Tap\""));
    CHECK(contains(all, "\"calcium\":{\"value\":\"50\",\"unit\":\"mg/l\"}"));

    // Each can be removed by id, and undo restores it.
    const kl_ingredient_id hop = idOf(all, "Cascade");
    const kl_ingredient_id culture = idOf(all, "US-05");
    const kl_ingredient_id water = idOf(all, "Tap");
    CHECK(hop != 0 && culture != 0 && water != 0);
    CHECK(kl_remove_hop(doc, 1, hop, nullptr) == 0);
    CHECK(kl_remove_culture(doc, 1, culture, nullptr) == 0);
    CHECK(kl_remove_water(doc, 1, water, nullptr) == 0);
    const std::string removed = take(kl_document_snapshot_json(doc));
    CHECK(!contains(removed, "\"name\":\"Cascade\""));
    CHECK(!contains(removed, "\"name\":\"US-05\""));
    CHECK(!contains(removed, "\"name\":\"Tap\""));
    CHECK(kl_remove_hop(doc, 1, hop, nullptr) == -1);
    CHECK(kl_undo(doc, nullptr) == 1);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"Tap\""));
    kl_document_free(doc);
}

// The beta acid goes in and out of the snapshot, and a price changes the cost, one undo step each.
void testBetaAcidAndPricesThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_add_fermentable(doc, 1, "Pale", "grain", "4", "kg", "1.037", "sg", "3", "SRM", nullptr) == 0);
    CHECK(kl_add_hop(doc, 1, "Cascade", "5.5", "6.2", "30", "g", "pellet", "add_to_boil", "60", "min", nullptr) == 0);
    CHECK(kl_add_hop(doc, 1, "Plain", "4", nullptr, "10", "g", nullptr, nullptr, nullptr, nullptr, nullptr) == 0);
    std::string snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"betaAcid\":{\"value\":\"6.2\",\"unit\":\"%\"}"));
    CHECK(contains(snapshot, "\"betaAcid\":null"));
    CHECK(contains(snapshot, "\"cost\":null"));
    CHECK(contains(snapshot, "\"currency\":\"USD\""));
    const kl_ingredient_id hop = idOf(snapshot, "Cascade");

    // Editing a hop keeps its beta acid unless the edit changes it.
    CHECK(kl_set_hop(doc, 1, hop, "Cascade", "6", "5", "50", "g", "pellet", "add_to_boil", "20", "min", nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"betaAcid\":{\"value\":\"5\",\"unit\":\"%\"}"));
    char* error = nullptr;
    CHECK(kl_set_hop(doc, 1, hop, "Cascade", "6", "five", "50", "g", "pellet", nullptr, nullptr, nullptr, &error) == -1);
    kl_string_free(error);

    // 50 g at 2 per g is 100, and 4 kg at 3 per kg is 12.
    const kl_ingredient_id malt = idOf(snapshot, "Pale");
    CHECK(kl_set_price(doc, 1, "hop", hop, "2", "g", nullptr) == 0);
    CHECK(take(kl_undo_label(doc)) == "Set Price");
    CHECK(kl_set_price(doc, 1, "fermentable", malt, "3", "kg", nullptr) == 0);
    snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"price\":{\"amount\":\"3\",\"per\":\"kg\"}"));
    CHECK(contains(snapshot, "\"total\":112"));
    CHECK(contains(snapshot, "\"unpriced\":1"));

    // A price in the wrong kind of unit is refused, and NULL clears one.
    error = nullptr;
    CHECK(kl_set_price(doc, 1, "hop", hop, "2", "l", &error) == -1);
    CHECK(error != nullptr && contains(std::string(error), "same thing"));
    kl_string_free(error);
    CHECK(kl_set_price(doc, 1, "water", hop, "2", "g", nullptr) == -1);
    CHECK(kl_set_price(doc, 1, "fermentable", malt, nullptr, nullptr, nullptr) == 0);
    CHECK(take(kl_undo_label(doc)) == "Clear Price");
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"total\":100"));

    CHECK(kl_set_recipe_currency(doc, 1, "gbp", nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"currency\":\"GBP\""));
    CHECK(kl_set_recipe_currency(doc, 1, "pounds", nullptr) == -1);
    kl_document_free(doc);
}

// Edits change the values in place, and removing a recipe can be undone.
void testEditsAndRemovalThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_add_hop(doc, 1, "Cascade", "5.5", nullptr, "30", "g", "pellet", "add_to_boil", "60", "min", nullptr) == 0);
    const kl_ingredient_id hop = idOf(take(kl_document_snapshot_json(doc)), "Cascade");
    CHECK(kl_set_hop(doc, 1, hop, "Cascade", "6", nullptr, "50", "g", "pellet", "add_to_boil", "20", "min", nullptr) == 0);
    std::string snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"amount\":{\"value\":\"50\",\"unit\":\"g\"}"));
    CHECK(contains(snapshot, "\"time\":{\"value\":\"20\",\"unit\":\"min\"}"));
    CHECK(idOf(snapshot, "Cascade") == hop);
    CHECK(take(kl_undo_label(doc)) == "Edit Hop");

    // Clearing the time leaves the hop without one.
    CHECK(kl_set_hop(doc, 1, hop, "Cascade", "6", nullptr, "50", "g", "pellet", nullptr, nullptr, nullptr, nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"time\":null"));

    // A bad value is refused and nothing changes.
    CHECK(kl_set_hop(doc, 1, hop, "Cascade", "six", nullptr, "50", "g", "pellet", nullptr, nullptr, nullptr, nullptr) == -1);

    CHECK(kl_add_fermentable(doc, 1, "Pale", "grain", "4", "kg", "1.037", "sg", "3", "SRM", nullptr) == 0);
    const kl_ingredient_id malt = idOf(take(kl_document_snapshot_json(doc)), "Pale");
    CHECK(kl_set_fermentable(doc, 1, malt, "Pale", "grain", "3", "kg", "1.037", "sg", "4", "SRM", nullptr) == 0);
    snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"amount\":{\"value\":\"3\",\"unit\":\"kg\"}"));
    CHECK(contains(snapshot, "\"potential\":{\"value\":\"1.037\",\"unit\":\"sg\"}"));

    CHECK(kl_remove_recipe(doc, 1, nullptr) == 0);
    CHECK(!contains(take(kl_document_snapshot_json(doc)), "\"name\":\"New Recipe\""));
    CHECK(kl_undo(doc, nullptr) == 1);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"New Recipe\""));
    kl_document_free(doc);
}

// The procedure places additions by the boil length given, flags a time that does not fit,
// and an equipment profile is one undo step.
void testProcedureAndEquipment() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_add_fermentable(doc, 1, "Pale", "grain", "4", "kg", "1.037", "sg", "3", "SRM", nullptr) == 0);
    CHECK(kl_add_hop(doc, 1, "Cascade", "5.5", nullptr, "30", "g", "pellet", "add_to_boil", "60", "min", nullptr) == 0);
    CHECK(kl_add_hop(doc, 1, "Citra", "12", nullptr, "20", "g", "pellet", "add_to_boil", "15", "min", nullptr) == 0);
    CHECK(kl_add_hop(doc, 1, "Late", "4", nullptr, "10", "g", nullptr, "add_to_boil", "90", "min", nullptr) == 0);
    CHECK(kl_add_culture(doc, 1, "US-05", "ale", "dry", "1", "pkg", "78", nullptr) == 0);

    char* error = nullptr;
    char* json = kl_procedure_json(doc, 1, 60, "all in one", &error);
    CHECK(json != nullptr && error == nullptr);
    const std::string procedure = take(json);
    CHECK(contains(procedure, "\"minute\":45"));  // Citra, 15 minutes before the end of a 60-minute boil
    CHECK(contains(procedure, "\"minute\":0"));
    CHECK(contains(procedure, "cannot be placed"));  // Late, at 90 minutes, does not fit
    CHECK(contains(procedure, "\"warning\":true"));
    CHECK(contains(procedure, "US-05"));

    CHECK(kl_procedure_json(doc, 1, 0, "all in one", &error) == nullptr && error != nullptr);
    kl_string_free(error);
    error = nullptr;
    CHECK(kl_procedure_json(doc, 1, 60, "tea kettle", &error) == nullptr && error != nullptr);
    CHECK(error != nullptr && contains(std::string(error), "not a mash system"));
    kl_string_free(error);

    CHECK(kl_apply_equipment(doc, 1, "19", "l", "75", "9", "1", nullptr) == 0);
    const std::string applied = take(kl_document_snapshot_json(doc));
    CHECK(contains(applied, "\"brewhouseEfficiency\":{\"value\":\"75\""));
    CHECK(contains(applied, "\"batchSize\":{\"value\":\"19\",\"unit\":\"l\"}"));
    CHECK(take(kl_undo_label(doc)) == "Apply Equipment Profile");
    CHECK(kl_undo(doc, nullptr) == 1);
    const std::string undone = take(kl_document_snapshot_json(doc));
    CHECK(contains(undone, "\"batchSize\":{\"value\":\"5\",\"unit\":\"gal\"}"));
    CHECK(contains(undone, "\"brewhouseEfficiency\":{\"value\":\"72\""));
    kl_document_free(doc);
}

// Saving and opening through the facade keeps the recipe.
void testSaveAndOpen() {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "kettlelogic-capi-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::string file = (folder / "Pale.json").string();

    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_set_recipe_name(doc, 1, "Saved Ale", nullptr) == 0);
    char* error = nullptr;
    CHECK(kl_document_save(doc, file.c_str(), &error) == 0);
    CHECK(error == nullptr);
    kl_document_free(doc);

    kl_document* opened = kl_document_open(file.c_str(), &error);
    CHECK(opened != nullptr);
    CHECK(contains(take(kl_document_snapshot_json(opened)), "\"name\":\"Saved Ale\""));
    kl_document_free(opened);

    error = nullptr;
    CHECK(kl_document_open((folder / "Missing.json").string().c_str(), &error) == nullptr);
    CHECK(error != nullptr);
    kl_string_free(error);
    std::filesystem::remove_all(folder);
}

// The engine's version is one string in version.hpp, and the one CMake gives the project. They must match.
void testEngineVersion() {
    CHECK(std::string(kl_engine_version()) == kEngineVersion);
    CHECK(std::string(kEngineVersion) == KETTLELOGIC_PROJECT_VERSION);
}

}  // namespace

void runCApiTests() {
    testEngineVersion();
    testNewDocumentAndSnapshot();
    testCommandsUndoAndRedo();
    testBadInputIsRefused();
    testDrinkThroughTheFacade();
    testSaveAndOpen();
    testIngredientsThroughTheFacade();
    testHopsCulturesAndWatersThroughTheFacade();
    testBetaAcidAndPricesThroughTheFacade();
    testEditsAndRemovalThroughTheFacade();
    testProcedureAndEquipment();
    testDuplicateThroughTheFacade();
    testGroupedEditsAreOneUndoStep();
}

}  // namespace kettlelogic::test
