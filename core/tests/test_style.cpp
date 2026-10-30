#include "check.hpp"

#include "kettlelogic/c/kettlelogic.h"
#include "kettlelogic/calc/starter.hpp"
#include "kettlelogic/json.hpp"
#include "kettlelogic/calc/style.hpp"
#include "kettlelogic/styles/style.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

// The "id" that comes just before the item named `name` in the snapshot.
std::uint64_t idOf(const std::string& snapshot, const std::string& name) {
    const std::size_t named = snapshot.find("\"name\":\"" + name + "\"");
    if (named == std::string::npos) {
        return 0;
    }
    const std::size_t key = snapshot.rfind("\"id\":", named);
    return key == std::string::npos ? 0 : std::strtoull(snapshot.c_str() + key + 5, nullptr, 10);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

void testBjcpTable() {
    const calc::BjcpStyle* ipa = calc::findBjcpStyle("21A");
    CHECK(ipa != nullptr);
    if (ipa != nullptr) {
        CHECK(ipa->name == "American IPA");
        CHECK(ipa->og.low == 1.056 && ipa->og.high == 1.070);
        CHECK(ipa->fg.low == 1.008 && ipa->fg.high == 1.014);
        CHECK(ipa->ibu.low == 40 && ipa->ibu.high == 70);
        CHECK(ipa->srm.low == 6 && ipa->srm.high == 14);
        CHECK(ipa->abv.low == 5.5 && ipa->abv.high == 7.5);
    }
    CHECK(calc::findBjcpStyle("99Z") == nullptr);
    CHECK(calc::bjcpStyles().size() == 86);
}

void testStyleComparisonThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    char* error = nullptr;

    // No style chosen yet, so the comparison is null.
    CHECK(take(kl_style_comparison_json(doc, 1, &error)) == "null");
    CHECK(error == nullptr);

    CHECK(kl_set_recipe_style(doc, 1, "99Z", nullptr) == -1);
    CHECK(kl_set_recipe_style(doc, 1, "21A", nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"bjcpStyle\":\"21A\""));

    // With no fermentables the engine's gravity is 1.000, which is below the style, and the
    // final gravity cannot be worked out, so its check is unknown.
    const std::string empty = take(kl_style_comparison_json(doc, 1, &error));
    CHECK(contains(empty, "\"style\":{\"id\":\"21A\",\"name\":\"American IPA\"}"));
    CHECK(contains(empty, "\"metric\":\"originalGravity\",\"value\":1,"));
    CHECK(contains(empty, "\"metric\":\"finalGravity\",\"value\":null"));
    CHECK(contains(empty, "\"fit\":\"unknown\""));

    // Five kilograms of pale malt in 20 liters comes out well above a light lager's gravity.
    CHECK(kl_add_fermentable(doc, 1, "Pale", "grain", "5", "kg", "1.037", "sg", "2", "SRM", nullptr) == 0);
    CHECK(kl_set_recipe_style(doc, 1, "1A", nullptr) == 0);
    const std::string lager = take(kl_style_comparison_json(doc, 1, &error));
    CHECK(contains(lager, "\"metric\":\"originalGravity\""));
    CHECK(contains(lager, "\"fit\":\"above\""));
    CHECK(error == nullptr);

    kl_document_free(doc);
}

void testFoldersThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    char* error = nullptr;
    kl_folder_id summer = 0;

    CHECK(kl_add_folder(doc, "Summer", &summer, &error) == 0);
    CHECK(error == nullptr);
    CHECK(summer != 0);

    std::string snapshot = take(kl_document_snapshot_json(doc));
    CHECK(idOf(snapshot, "Summer") == summer);
    CHECK(contains(snapshot, "\"folderId\":null"));

    CHECK(kl_move_recipe_to_folder(doc, 1, summer, nullptr) == 0);
    snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"folderId\":" + std::to_string(summer)));

    // Removing the folder moves its recipe back to the top level, and undo restores both.
    CHECK(kl_remove_folder(doc, summer, nullptr) == 0);
    snapshot = take(kl_document_snapshot_json(doc));
    CHECK(!contains(snapshot, "\"name\":\"Summer\""));
    CHECK(contains(snapshot, "\"folderId\":null"));
    CHECK(kl_undo(doc, nullptr) == 1);
    snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"name\":\"Summer\""));
    CHECK(contains(snapshot, "\"folderId\":" + std::to_string(summer)));

    // Renames are refused when blank, and a folder must exist to be renamed.
    CHECK(kl_rename_folder(doc, summer, "   ", &error) == -1);
    CHECK(error != nullptr);
    kl_string_free(error);
    error = nullptr;
    CHECK(kl_rename_folder(doc, summer, "Winter", nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"name\":\"Winter\""));
    CHECK(kl_rename_folder(doc, 9999, "Ghost", &error) == -1);
    CHECK(error != nullptr);
    kl_string_free(error);

    // Passing 0 files the recipe at the top level.
    CHECK(kl_move_recipe_to_folder(doc, 1, 0, nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"folderId\":null"));

    kl_document_free(doc);
}

void testTrashThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    char* error = nullptr;

    CHECK(kl_trash_recipe(doc, 1, &error) == 0);
    CHECK(error == nullptr);
    std::string snapshot = take(kl_document_snapshot_json(doc));
    CHECK(contains(snapshot, "\"name\":\"Recently Deleted\""));
    CHECK(contains(snapshot, "\"trash\":true"));
    const kl_folder_id trash = idOf(snapshot, "Recently Deleted");
    CHECK(contains(snapshot, "\"folderId\":" + std::to_string(trash)));

    // A deleted recipe is refused a second delete, and the folder is protected.
    CHECK(kl_trash_recipe(doc, 1, &error) == -1);
    CHECK(error != nullptr);
    kl_string_free(error);
    error = nullptr;
    CHECK(kl_rename_folder(doc, trash, "Bin", &error) == -1);
    kl_string_free(error);
    error = nullptr;
    CHECK(kl_remove_folder(doc, trash, &error) == -1);
    kl_string_free(error);
    error = nullptr;

    // Undo files the recipe back at the top level, and takes the folder away.
    CHECK(kl_undo(doc, nullptr) == 1);
    snapshot = take(kl_document_snapshot_json(doc));
    CHECK(!contains(snapshot, "Recently Deleted"));
    CHECK(contains(snapshot, "\"folderId\":null"));
    kl_document_free(doc);
}

void testTrashSurvivesSaving() {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "kettlelogic-trash-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::string file = (folder / "Trashed.json").string();

    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_set_recipe_name(doc, 1, "Trashed", nullptr) == 0);
    CHECK(kl_trash_recipe(doc, 1, nullptr) == 0);
    CHECK(kl_document_save(doc, file.c_str(), nullptr) == 0);
    kl_document_free(doc);

    kl_document* opened = kl_document_open(file.c_str(), nullptr);
    CHECK(opened != nullptr);
    if (opened != nullptr) {
        const std::string snapshot = take(kl_document_snapshot_json(opened));
        CHECK(contains(snapshot, "\"trash\":true"));
        CHECK(contains(snapshot, "\"folderId\":" + std::to_string(idOf(snapshot, "Recently Deleted"))));
        kl_document_free(opened);
    }

    std::filesystem::remove_all(folder);
}

void testFoldersAndStyleSurviveSaving() {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "kettlelogic-style-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::string file = (folder / "Ipa.json").string();

    kl_document* doc = kl_document_new(nullptr);
    kl_folder_id winter = 0;
    CHECK(kl_add_folder(doc, "Winter", &winter, nullptr) == 0);
    CHECK(kl_move_recipe_to_folder(doc, 1, winter, nullptr) == 0);
    CHECK(kl_set_recipe_style(doc, 1, "21A", nullptr) == 0);
    CHECK(kl_document_save(doc, file.c_str(), nullptr) == 0);
    kl_document_free(doc);

    char* error = nullptr;
    kl_document* opened = kl_document_open(file.c_str(), &error);
    CHECK(opened != nullptr);
    CHECK(error == nullptr);
    if (opened != nullptr) {
        const std::string snapshot = take(kl_document_snapshot_json(opened));
        CHECK(contains(snapshot, "\"name\":\"Winter\""));
        CHECK(contains(snapshot, "\"folderId\":" + std::to_string(idOf(snapshot, "Winter"))));
        CHECK(contains(snapshot, "\"bjcpStyle\":\"21A\""));
        kl_document_free(opened);
    }

    // A sidecar from before folders existed (schema 6) still opens, with no folders.
    const std::filesystem::path sidecar = folder / "Ipa.kettlelogic.json";
    const std::string current = readFile(sidecar);
    const std::string::size_type at = current.find("\"schema\": 7");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        std::string previous = current;
        previous.replace(at, std::string("\"schema\": 7").size(), "\"schema\": 6");
        writeFile(sidecar, previous);
        kl_document* older = kl_document_open(file.c_str(), &error);
        CHECK(older != nullptr);
        if (older != nullptr) {
            CHECK(!contains(take(kl_document_snapshot_json(older)), "\"name\":\"Winter\""));
            kl_document_free(older);
        }
    }

    std::filesystem::remove_all(folder);
}

}  // namespace

// The procedure names each ingredient with its weight or volume.
void testProcedureShowsAmounts() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_add_fermentable(doc, 1, "Pilsner malt", "grain", "5", "kg", "1.037", "sg", "2", "SRM", nullptr) == 0);
    CHECK(kl_add_fermentable(doc, 1, "Crystal 60", "grain", "500", "g", "1.034", "sg", "60", "SRM", nullptr) == 0);
    CHECK(kl_add_culture(doc, 1, "Safale US-05", "ale", "dry", "1", "pkg", nullptr, nullptr) == 0);

    char* error = nullptr;
    char* json = kl_procedure_json(doc, 1, 60, "all in one", &error);
    CHECK(json != nullptr);
    const std::string procedure = take(json);
    CHECK(contains(procedure, "Add 5 kg Pilsner malt, 500 g Crystal 60 to the kettle"));
    CHECK(contains(procedure, "Pitch 1 pkg of Safale US-05"));
    kl_document_free(doc);
}

// A 19 L batch at 1.050 needs 0.75 * 19 * 12.33 = 175.7 billion cells for an ale. Two packs of 100
// billion cover it. A 2 L starter at 1.040 needs 7.7 oz of dry malt, and one-stage growth from the
// pack needs about half a liter of starter.
void testStarterPlan() {
    calc::StarterInputs inputs;
    inputs.starterLiters = 2;
    const calc::StarterPlan plan = calc::planStarter(19, 1.050, inputs);
    CHECK(plan.cellsNeededBillion > 175.5 && plan.cellsNeededBillion < 175.9);
    CHECK(plan.packsWithoutStarter == 2);
    CHECK(plan.cellsWithoutStarterBillion == 200);
    CHECK(plan.dryMaltOunces > 7.6 && plan.dryMaltOunces < 7.8);
    CHECK(plan.oneStageLiters > 0.4 && plan.oneStageLiters < 0.6);
    CHECK(throws([] { calc::planStarter(0, 1.050, calc::StarterInputs{}); }));
}

// Reads a number from a facade JSON result.
double numberAt(const json::Value& root, const char* key) {
    const json::Value* v = root.find(key);
    return v != nullptr && v->isNumber() ? v->asRational().toDouble() : -1e9;
}

// 5 kg of grain at the default 3 L/kg: 15 L of strike water. Three grams of gypsum in that water
// gives 46.6 mg/L calcium and 111.6 mg/L sulfate; one gram of baking soda gives 48.4 mg/L bicarbonate.
// Two grams of 88 % lactic acid supply 19.5 milliequivalents.
void testBrewingPlan() {
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_add_fermentable(doc, 1, "Pilsner malt", "grain", "5", "kg", "1.037", "sg", "2", "SRM", nullptr) == 0);
    const std::string inputs =
        R"({"mashRatioLPerKg": 3, "salts": [{"kind": "gypsum", "grams": 3, "sparge": false},)"
        R"({"kind": "baking soda", "grams": 1, "sparge": false}],)"
        R"("acids": [{"kind": "lactic acid", "grams": 2}]})";
    char* error = nullptr;
    char* out = kl_brewing_plan_json(doc, 1, inputs.c_str(), &error);
    CHECK(out != nullptr);
    if (out != nullptr) {
        const json::Value plan = json::parse(take(out));
        CHECK(numberAt(plan, "strikeLiters") > 14.99 && numberAt(plan, "strikeLiters") < 15.01);
        CHECK(numberAt(plan, "strikeTempC") > 73.4 && numberAt(plan, "strikeTempC") < 73.6);
        const json::Value* mash = plan.find("mashWater");
        CHECK(mash != nullptr);
        if (mash != nullptr) {
            const json::Value* ions = mash->find("ions");
            CHECK(ions != nullptr);
            if (ions != nullptr) {
                CHECK(numberAt(*ions, "calcium") > 46.5 && numberAt(*ions, "calcium") < 46.7);
                CHECK(numberAt(*ions, "sulfate") > 111.5 && numberAt(*ions, "sulfate") < 111.7);
                // Baking soda gives 48.4 mg/L of bicarbonate, and calculation keeps 73 % of it at the mash pH.
                CHECK(numberAt(*ions, "bicarbonate") > 38.7 && numberAt(*ions, "bicarbonate") < 38.8);
            }
            // No base water, so the residual is the bicarbonate of the baking soda less the gypsum's calcium
            CHECK(numberAt(*mash, "residualAlkalinity") > -1.49 && numberAt(*mash, "residualAlkalinity") < -1.46);
        }
        // 2 g of 88 % lactic acid at pH 5.4 gives 19.5 milliequivalents of protons, of which 97 % are given up.
        CHECK(numberAt(plan, "acidMilliequivalents") > 18.9 && numberAt(plan, "acidMilliequivalents") < 19.1);
    }
    kl_document_free(doc);

    // Leaving the salts out gives the base water, which here has none.
    doc = kl_document_new(nullptr);
    const std::string none = R"({"leaveOutSalts": true, "salts": [{"kind": "gypsum", "grams": 3}]})";
    out = kl_brewing_plan_json(doc, 1, none.c_str(), &error);
    CHECK(out != nullptr);
    if (out != nullptr) {
        const std::string text = take(out);
        CHECK(text.find("\"calcium\":0") != std::string::npos);
    }
    kl_document_free(doc);
}

// The mash pH and color estimates, the force-carbonation pressure, and the ABV for a strong beer.
void testEstimatesAndPressure() {
    // The water test's mash pH 5.685 before the acid
    kl_document* doc = kl_document_new(nullptr);
    CHECK(kl_add_fermentable(doc, 1, "Pilsner malt", "grain", "5", "kg", "1.037", "sg", "2", "SRM", nullptr) == 0);
    const std::string inputs =
        R"({"mashRatioLPerKg": 3, "salts": [{"kind": "gypsum", "grams": 3}, {"kind": "baking soda", "grams": 1}],)"
        R"("acids": [{"kind": "lactic acid", "grams": 2}]})";
    char* out = kl_brewing_plan_json(doc, 1, inputs.c_str(), nullptr);
    CHECK(out != nullptr);
    if (out != nullptr) {
        const json::Value plan = json::parse(take(out));
        CHECK(numberAt(plan, "estimatedMashPhBeforeAcid") > 5.684 && numberAt(plan, "estimatedMashPhBeforeAcid") < 5.686);
        // After the acid, the residual alkalinity falls by the 63 ppm the acid neutralizes.
        CHECK(numberAt(plan, "estimatedMashPh") < numberAt(plan, "estimatedMashPhBeforeAcid"));
        CHECK(numberAt(plan, "colorLowSrm") >= 0 && numberAt(plan, "colorLowSrm") < numberAt(plan, "colorHighSrm"));
    }
    kl_document_free(doc);

    // 2.5 volumes at 40 °F (4.44 °C): the patent's equation gives about 12.2 psi.
    kl_document* journal = kl_document_new(nullptr);
    const std::string measured = R"({"carbonationVolumes": 2.5, "carbonationTempC": 4.444})";
    char* jout = kl_journal_plan_json(journal, 1, measured.c_str(), nullptr);
    CHECK(jout != nullptr);
    if (jout != nullptr) {
        const json::Value result = json::parse(take(jout));
        CHECK(numberAt(result, "forceCarbonationPsi") > 12.0 && numberAt(result, "forceCarbonationPsi") < 12.4);
    }
    kl_document_free(journal);
}

// A user style defined through the facade, its comparison, edits, validation, and removal with undo.
void testUserStylesThroughTheFacade() {
    kl_document* doc = kl_document_new(nullptr);
    char* error = nullptr;
    char* created = nullptr;
    const std::string house =
        R"({"name": "House Pale", "og": {"low": 1.040, "high": 1.050}, "fg": {"low": 1.008, "high": 1.012},)"
        R"( "ibu": {"low": 20, "high": 30}, "srm": {"low": 4, "high": 7}, "abv": {"low": 4, "high": 5.5}})";
    CHECK(kl_add_user_style(doc, house.c_str(), &created, &error) == 0);
    CHECK(error == nullptr);
    const std::string id = created != nullptr ? created : "";
    kl_string_free(created);
    CHECK(kettlelogic::styles::isUserStyleId(id));
    CHECK(id.rfind("custom-", 0) == 0);
    CHECK(contains(take(kl_user_styles_json(doc, nullptr)), "\"name\":\"House Pale\""));

    // A user style can be the recipe's style, and its comparison names it.
    CHECK(kl_set_recipe_style(doc, 1, id.c_str(), nullptr) == 0);
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"bjcpStyle\":\"" + id + "\""));
    const std::string cmp = take(kl_style_comparison_json(doc, 1, &error));
    CHECK(contains(cmp, "\"style\":{\"id\":\"" + id + "\",\"name\":\"House Pale\"}"));
    CHECK(error == nullptr);

    // Blank names, backwards ranges, and unknown ids are refused.
    const std::string blank =
        R"({"name": "  ", "og": {"low": 1, "high": 2}, "fg": {"low": 1, "high": 2}, "ibu": {"low": 1, "high": 2},)"
        R"( "srm": {"low": 1, "high": 2}, "abv": {"low": 1, "high": 2}})";
    CHECK(kl_add_user_style(doc, blank.c_str(), nullptr, &error) == -1);
    CHECK(error != nullptr);
    kl_string_free(error);
    error = nullptr;
    const std::string backwards =
        R"({"name": "Backwards", "og": {"low": 1.06, "high": 1.04}, "fg": {"low": 1, "high": 2},)"
        R"( "ibu": {"low": 1, "high": 2}, "srm": {"low": 1, "high": 2}, "abv": {"low": 1, "high": 2}})";
    CHECK(kl_add_user_style(doc, backwards.c_str(), nullptr, &error) == -1);
    kl_string_free(error);
    error = nullptr;
    CHECK(kl_set_recipe_style(doc, 1, "custom-missing", &error) == -1);
    kl_string_free(error);
    error = nullptr;
    CHECK(kl_set_user_style(doc, id.c_str(), backwards.c_str(), &error) == -1);
    kl_string_free(error);
    error = nullptr;

    // An edit keeps the id and changes the name in place.
    const std::string renamed =
        R"({"name": "House Ale", "og": {"low": 1.040, "high": 1.050}, "fg": {"low": 1.008, "high": 1.012},)"
        R"( "ibu": {"low": 20, "high": 30}, "srm": {"low": 4, "high": 7}, "abv": {"low": 4, "high": 5.5}})";
    CHECK(kl_set_user_style(doc, id.c_str(), renamed.c_str(), nullptr) == 0);
    CHECK(contains(take(kl_user_styles_json(doc, nullptr)), "\"id\":\"" + id + "\",\"name\":\"House Ale\""));

    // Removing a style clears the recipes that used it, and undo brings both back.
    CHECK(kl_remove_user_style(doc, id.c_str(), nullptr) == 0);
    CHECK(take(kl_user_styles_json(doc, nullptr)) == "[]");
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"bjcpStyle\":null"));
    CHECK(take(kl_style_comparison_json(doc, 1, nullptr)) == "null");
    CHECK(kl_undo(doc, nullptr) == 1);
    CHECK(contains(take(kl_user_styles_json(doc, nullptr)), "\"name\":\"House Ale\""));
    CHECK(contains(take(kl_document_snapshot_json(doc)), "\"bjcpStyle\":\"" + id + "\""));
    kl_document_free(doc);
}

// A document with user styles saves them in a schema 8 sidecar, which opens with them again.
// A document with none is still written as schema 7.
void testUserStylesSurviveSaving() {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "kettlelogic-user-style-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::string file = (folder / "House.json").string();
    const std::string sidecar = (folder / "House.kettlelogic.json").string();

    kl_document* doc = kl_document_new(nullptr);
    char* created = nullptr;
    const std::string style =
        R"({"name": "House Pale", "og": {"low": 1.045, "high": 1.052}, "fg": {"low": 1.008, "high": 1.012},)"
        R"( "ibu": {"low": 20, "high": 30}, "srm": {"low": 4, "high": 7}, "abv": {"low": 4, "high": 5.5}})";
    CHECK(kl_add_user_style(doc, style.c_str(), &created, nullptr) == 0);
    const std::string id = created != nullptr ? created : "";
    kl_string_free(created);
    CHECK(kl_set_recipe_style(doc, 1, id.c_str(), nullptr) == 0);
    CHECK(kl_document_save(doc, file.c_str(), nullptr) == 0);
    kl_document_free(doc);
    CHECK(contains(readFile(sidecar), "\"schema\": 8"));
    CHECK(contains(readFile(sidecar), "\"userStyles\""));

    kl_document* opened = kl_document_open(file.c_str(), nullptr);
    CHECK(opened != nullptr);
    if (opened != nullptr) {
        CHECK(contains(take(kl_user_styles_json(opened, nullptr)), "\"og\":{\"low\":1.045,\"high\":1.052}"));
        CHECK(contains(take(kl_document_snapshot_json(opened)), "\"bjcpStyle\":\"" + id + "\""));
        kl_document_free(opened);
    }

    // No user styles: the sidecar stays at schema 7, which older readers open.
    kl_document* plain = kl_document_new(nullptr);
    CHECK(kl_document_save(plain, file.c_str(), nullptr) == 0);
    kl_document_free(plain);
    CHECK(contains(readFile(sidecar), "\"schema\": 7"));
    CHECK(!contains(readFile(sidecar), "userStyles"));

    std::filesystem::remove_all(folder);
}

void runStyleTests() {
    testEstimatesAndPressure();
    testBrewingPlan();
    testStarterPlan();
    testProcedureShowsAmounts();
    testBjcpTable();
    testStyleComparisonThroughTheFacade();
    testFoldersThroughTheFacade();
    testFoldersAndStyleSurviveSaving();
    testTrashThroughTheFacade();
    testTrashSurvivesSaving();
    testUserStylesThroughTheFacade();
    testUserStylesSurviveSaving();
}

}  // namespace kettlelogic::test
