#include "check.hpp"

#include "kettlelogic/c/kettlelogic.h"
#include "kettlelogic/calc/tools.hpp"
#include "kettlelogic/catalog/equipment.hpp"
#include "kettlelogic/catalog/fermentation_presets.hpp"
#include "kettlelogic/catalog/glossary.hpp"
#include "kettlelogic/catalog/ingredient_query.hpp"
#include "kettlelogic/catalog/ingredients.hpp"
#include "kettlelogic/catalog/options.hpp"
#include "kettlelogic/catalog/recipe_templates.hpp"
#include "kettlelogic/catalog/tool_specs.hpp"
#include "kettlelogic/json.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <utility>

namespace kettlelogic::test {

namespace {

// A menu entry is identified by its group and name, so neither may repeat within a kind.
template <typename Items>
bool distinct(const Items& items) {
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& item : items) {
        if (!seen.emplace(item.group, item.name).second) {
            return false;
        }
    }
    return true;
}

// Whether the text is a plain decimal, such as "1.037", that the add commands can take.
bool isDecimal(const std::string& text) {
    bool digits = false;
    bool point = false;
    for (const char c : text) {
        if (c == '.' && !point) {
            point = true;
        } else if (c >= '0' && c <= '9') {
            digits = true;
        } else {
            return false;
        }
    }
    return digits;
}

void testCatalogContents() {
    CHECK(catalog::fermentables().size() > 90);
    CHECK(catalog::hops().size() > 40);
    CHECK(catalog::cultures().size() > 120);
    CHECK(catalog::waters().size() == 8);
    CHECK(catalog::miscellaneous().size() > 50);

    CHECK(distinct(catalog::fermentables()));
    CHECK(distinct(catalog::hops()));
    CHECK(distinct(catalog::cultures()));
    CHECK(distinct(catalog::waters()));
    CHECK(distinct(catalog::miscellaneous()));

    const catalog::FermentableTemplate& first = catalog::fermentables().front();
    CHECK(first.name == "2-Row (Briess)" && first.group == "Base malts" && first.type == "grain");
    CHECK(catalog::waters().front().name == "Distilled or reverse osmosis");

    for (const auto& f : catalog::fermentables()) {
        CHECK(isDecimal(f.yieldSG) && isDecimal(f.colorSRM));
    }
    for (const auto& h : catalog::hops()) {
        CHECK(isDecimal(h.alphaPercent));
        CHECK(!h.betaPercent || isDecimal(*h.betaPercent));
    }
    for (const auto& c : catalog::cultures()) {
        CHECK(!c.attenuationPercent || isDecimal(*c.attenuationPercent));
    }
    for (const auto& w : catalog::waters()) {
        CHECK(isDecimal(w.calcium) && isDecimal(w.bicarbonate) && isDecimal(w.sulfate));
        CHECK(isDecimal(w.chloride) && isDecimal(w.sodium) && isDecimal(w.magnesium));
    }
}

std::string textAt(const json::Value& entry, const char* key) {
    const json::Value* v = entry.find(key);
    return v != nullptr && v->isString() ? v->asString() : "<missing>";
}

void testCatalogThroughTheFacade() {
    char* raw = kl_ingredient_catalog_json();
    CHECK(raw != nullptr);
    if (raw == nullptr) {
        return;
    }
    const json::Value root = json::parse(raw);
    kl_string_free(raw);

    const json::Value* fermentables = root.find("fermentables");
    const json::Value* hops = root.find("hops");
    const json::Value* cultures = root.find("cultures");
    const json::Value* waters = root.find("waters");
    const json::Value* misc = root.find("miscellaneous");
    CHECK(fermentables && hops && cultures && waters && misc);
    if (!(fermentables && hops && cultures && waters && misc)) {
        return;
    }
    CHECK(fermentables->asArray().size() == catalog::fermentables().size());
    CHECK(hops->asArray().size() == catalog::hops().size());
    CHECK(cultures->asArray().size() == catalog::cultures().size());
    CHECK(waters->asArray().size() == catalog::waters().size());
    CHECK(misc->asArray().size() == catalog::miscellaneous().size());

    const json::Value& malt = fermentables->asArray().front();
    CHECK(textAt(malt, "name") == "2-Row (Briess)" && textAt(malt, "type") == "grain");
    CHECK(textAt(malt, "yield_sg") == "1.037" && textAt(malt, "color_srm") == "1.8");

    // A known beta acid is carried, and an unknown attenuation is left out rather than blanked.
    CHECK(textAt(hops->asArray().front(), "beta_percent") == "6");
    bool sawAttenuation = false;
    bool sawNone = false;
    for (const json::Value& culture : cultures->asArray()) {
        (culture.find("attenuation_percent") != nullptr ? sawAttenuation : sawNone) = true;
    }
    CHECK(sawAttenuation && sawNone);

    CHECK(textAt(waters->asArray()[1], "name") == "Pilsen" && textAt(waters->asArray()[1], "calcium") == "7");
    CHECK(textAt(misc->asArray().front(), "unit") == "g");
}

catalog::IngredientEntry entry(const char* name, const char* group, const char* type, std::optional<double> color,
                               bool custom = false) {
    catalog::IngredientEntry e;
    e.id = std::string(group) + "/" + name;
    e.name = name;
    e.group = group;
    e.type = type;
    e.primary = color;
    e.custom = custom;
    return e;
}

std::vector<std::string> names(const std::vector<catalog::IngredientEntry>& entries, const std::vector<std::size_t>& found) {
    std::vector<std::string> out;
    for (const std::size_t i : found) out.push_back(entries[i].name);
    return out;
}

void testIngredientSearch() {
    CHECK(catalog::foldForSearch("  2-Row  (Briess) ") == "2row briess");
    CHECK(catalog::foldForSearch("M\xC3\xBCnchner Sch\xC3\xB6nh\xC3\xA4user \xC3\x86") == "munchner schonhauser ae");

    const std::vector<catalog::IngredientEntry> list = {
        entry("2-Row (Briess)", "Base malts", "grain", 1.8),
        entry("Munich malt", "Base malts", "grain", 9),
        entry("M\xC3\xBCnchner", "Base malts", "grain", 10),
        entry("Crystal 60", "Crystal and caramel malts", "grain", 60),
        entry("Honey", "Sugars", "sugar", 1),
        entry("Mystery", "Sugars", "extract", std::nullopt),
        entry("My porter malt", "Custom", "grain", 25, true),
    };

    catalog::IngredientQuery q;
    CHECK(catalog::queryIngredients(list, q).size() == list.size());

    q.text = "2 ROW";
    CHECK(names(list, catalog::queryIngredients(list, q)) == std::vector<std::string>{"2-Row (Briess)"});
    q.text = "2row";
    CHECK(catalog::queryIngredients(list, q).size() == 1);
    q.text = "mun";  // both Munich and M\xC3\xBCnchner match
    CHECK(catalog::queryIngredients(list, q).size() == 2);
    q.text = "munch";  // the accent is folded
    CHECK(names(list, catalog::queryIngredients(list, q)) == std::vector<std::string>{"M\xC3\xBCnchner"});
    q.text = "crystal malts";  // words may match the name and the group
    CHECK(catalog::queryIngredients(list, q).size() == 1);
    q.text = "zzz";
    CHECK(catalog::queryIngredients(list, q).empty());

    q = {};
    q.types = {"sugar", "extract"};
    CHECK(catalog::queryIngredients(list, q).size() == 2);
    q.groups = {"Sugars"};
    q.types = {"extract"};
    CHECK(names(list, catalog::queryIngredients(list, q)) == std::vector<std::string>{"Mystery"});

    q = {};
    q.source = catalog::IngredientSource::custom;
    CHECK(catalog::queryIngredients(list, q).size() == 1);
    q.source = catalog::IngredientSource::builtIn;
    CHECK(catalog::queryIngredients(list, q).size() == list.size() - 1);

    // A bound leaves out an entry with no number.
    q = {};
    q.minPrimary = 9;
    q.maxPrimary = 25;
    CHECK(names(list, catalog::queryIngredients(list, q)) ==
          (std::vector<std::string>{"Munich malt", "M\xC3\xBCnchner", "My porter malt"}));
}

void testIngredientSort() {
    const std::vector<catalog::IngredientEntry> list = {
        entry("Zeta", "B group", "grain", 5),
        entry("Alpha", "B group", "grain", std::nullopt),
        entry("Mid", "A group", "grain", 20),
        entry("Beta", "A group", "grain", 5),
    };
    catalog::IngredientQuery q;
    q.sort = catalog::IngredientSort::name;
    CHECK(names(list, catalog::queryIngredients(list, q)) == (std::vector<std::string>{"Alpha", "Beta", "Mid", "Zeta"}));
    q.descending = true;
    CHECK(names(list, catalog::queryIngredients(list, q)) == (std::vector<std::string>{"Zeta", "Mid", "Beta", "Alpha"}));

    // Groups keep their catalog order, not alphabetical: B group came first.
    q = {};
    q.sort = catalog::IngredientSort::group;
    CHECK(names(list, catalog::queryIngredients(list, q)) == (std::vector<std::string>{"Alpha", "Zeta", "Beta", "Mid"}));

    // A missing number is last either way, and ties read in name order.
    q = {};
    q.sort = catalog::IngredientSort::primary;
    CHECK(names(list, catalog::queryIngredients(list, q)) == (std::vector<std::string>{"Beta", "Zeta", "Mid", "Alpha"}));
    q.descending = true;
    CHECK(names(list, catalog::queryIngredients(list, q)) == (std::vector<std::string>{"Mid", "Beta", "Zeta", "Alpha"}));

    q = {};
    q.descending = true;  // catalog order, reversed
    CHECK(names(list, catalog::queryIngredients(list, q)) == (std::vector<std::string>{"Beta", "Mid", "Alpha", "Zeta"}));

    const catalog::IngredientFacets facets = catalog::ingredientFacets(list);
    CHECK(facets.groups == (std::vector<std::string>{"B group", "A group"}));
    CHECK(facets.types == std::vector<std::string>{"grain"} && facets.forms.empty());
    CHECK(facets.primaryMin && *facets.primaryMin == 5 && facets.primaryMax && *facets.primaryMax == 20);

    CHECK(catalog::ingredientMetrics("hop").primary->label == "Alpha acid");
    CHECK(!catalog::ingredientMetrics("water").primary && !catalog::ingredientMetrics("culture").secondary);
}

void testIngredientQueryThroughTheFacade() {
    const char* request = R"({"kind": "fermentable", "text": "malt", "sort": "primary", "descending": true,
        "minPrimary": 2, "entries": [
            {"id": "a", "name": "Pale malt", "group": "Base", "type": "grain", "primary": 3, "secondary": 1.037},
            {"id": "b", "name": "Black malt", "group": "Roasted", "type": "grain", "primary": 500},
            {"id": "c", "name": "Honey", "group": "Sugars", "type": "sugar", "primary": 1},
            {"id": "d", "name": "Pilsen malt", "group": "Base", "custom": true, "primary": null}]})";
    char* raw = kl_ingredient_query_json(request);
    CHECK(raw != nullptr);
    if (raw == nullptr) return;
    const json::Value root = json::parse(raw);
    kl_string_free(raw);
    const json::Value* indices = root.find("indices");
    CHECK(indices && indices->asArray().size() == 2);
    if (indices && indices->asArray().size() == 2) {
        CHECK(indices->asArray()[0].numberLexeme() == "1" && indices->asArray()[1].numberLexeme() == "0");
    }
    const json::Value* groups = root.find("facets")->find("groups");
    CHECK(groups && groups->asArray().size() == 3);
    CHECK(textAt(*root.find("metrics")->find("primary"), "label") == "Color");
    CHECK(kl_ingredient_query_json("{}") == nullptr && kl_ingredient_query_json("nonsense") == nullptr);
}

bool listed(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// The ingredient catalog only offers types and units that the add dialogs list.
void testCatalogMatchesOptions() {
    for (const auto& f : catalog::fermentables()) {
        CHECK(listed(catalog::fermentableTypes(), f.type));
    }
    for (const auto& c : catalog::cultures()) {
        CHECK(listed(catalog::cultureTypes(), c.type));
        CHECK(listed(catalog::cultureForms(), c.form));
    }
    for (const auto& m : catalog::miscellaneous()) {
        CHECK(listed(catalog::miscellaneousTypes(), m.type));
        CHECK(listed(catalog::miscellaneousUnits(), m.unit));
    }
}

void testRecipeTemplates() {
    CHECK(catalog::RecipeTemplate::all().size() >= 10);
    std::set<std::string> ids;
    for (const catalog::RecipeTemplate& t : catalog::RecipeTemplate::all()) {
        CHECK(ids.insert(t.id).second);
        const auto problems = t.problems();
        for (const std::string& p : problems) {
            std::cerr << t.name << ": " << p << "\n";
        }
        CHECK(problems.empty());
    }
    catalog::RecipeTemplate broken = catalog::RecipeTemplate::all().front();
    broken.grains.front().name = "No such malt";
    broken.bjcpStyle = "99Z";
    CHECK(broken.problems().size() >= 2);
}

void testEquipmentPresetsAndGlossary() {
    CHECK(catalog::builtInEquipment().size() == 14);
    CHECK(catalog::builtInEquipment().front().name == "Igloo cooler and stovetop kettle");
    for (const auto& p : catalog::builtInEquipment()) {
        CHECK(isDecimal(p.batchLiters) && isDecimal(p.efficiencyPercent) && isDecimal(p.evaporationPercent));
        CHECK(isDecimal(p.trubLossLiters));
        CHECK(p.mashSystem == "all in one" || p.mashSystem == "separate tun");
    }
    CHECK(catalog::fermentationPresets().size() == 5);
    for (const auto& preset : catalog::fermentationPresets()) {
        CHECK(!preset.steps.empty());
    }

    CHECK(catalog::allTerms().size() == 12);
    std::set<std::string> names;
    for (const catalog::Term term : catalog::allTerms()) {
        CHECK(!catalog::explanation(term).empty());
        CHECK(names.insert(catalog::termName(term)).second);
    }
    CHECK(catalog::explanation(catalog::Term::fermentationEfficiency).find("yeast") != std::string::npos);
}

void testToolSpecs() {
    std::set<std::string> names;
    for (const auto* list : {&catalog::calculators(), &catalog::converters()}) {
        for (const catalog::ToolKind kind : *list) {
            CHECK(names.insert(catalog::toolKindName(kind)).second);
            CHECK(catalog::toolKindFromName(catalog::toolKindName(kind)) == kind);
            const catalog::ToolSpec spec = catalog::toolSpec(kind);
            CHECK(!spec.title.empty() && !spec.sections.empty());

            // With its defaults, every tool runs, and every readout it shows has a value.
            for (const catalog::ToolSection& section : spec.sections) {
                if (section.readouts.empty()) {
                    continue;
                }
                try {
                    const auto output = calc::tools::run(section.tool, catalog::toolInputs(spec, section, {}));
                    for (const catalog::ToolReadout& readout : section.readouts) {
                        if (!readout.enabledBy.empty()) {
                            continue;
                        }
                        // Without chloride there is no sulfate to chloride ratio, and the form shows a dash.
                        const bool has = output.results.count(readout.key) != 0 || readout.key == "sulfateToChloride";
                        if (!has) {
                            std::cerr << spec.title << ": no result \"" << readout.key << "\" from " << section.tool << "\n";
                        }
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

    // A toggle gates the fields that name it.
    const catalog::ToolSpec water = catalog::toolSpec(catalog::ToolKind::waterProfile);
    CHECK(!catalog::toolShows(water, {}, "useTarget"));
    CHECK(catalog::toolShows(water, {{"useTarget", "1"}}, "useTarget"));
    CHECK(catalog::toolShows(water, {}, ""));
}

json::Value dataset(const char* name) {
    char* raw = kl_builtin_data_json(name);
    CHECK(raw != nullptr);
    if (raw == nullptr) {
        return json::Value();
    }
    json::Value value = json::parse(raw);
    kl_string_free(raw);
    return value;
}

void testPriceUnits() {
    CHECK((catalog::priceUnits("lb") == std::vector<std::string>{"kg", "lb", "g", "oz"}));
    CHECK(catalog::priceUnits("mg").front() == "kg");
    CHECK((catalog::priceUnits("tbsp") == std::vector<std::string>{"l", "gal", "ml", "floz"}));
    CHECK((catalog::priceUnits("each") == std::vector<std::string>{"pkg", "each"}));
    CHECK((catalog::priceUnits("weird") == std::vector<std::string>{"weird"}));
    // Every unit a fermentable, miscellaneous, hop, or culture amount can use has a price family that holds it.
    for (const auto* list : {&catalog::fermentableUnits(), &catalog::miscellaneousUnits(), &catalog::hopUnits(),
                             &catalog::cultureUnits()}) {
        for (const std::string& unit : *list) {
            const auto units = catalog::priceUnits(unit);
            CHECK(units.size() > 1 || unit == "mg");
        }
    }

    char* raw = kl_price_units_json("pkg");
    CHECK(raw != nullptr);
    if (raw != nullptr) {
        const json::Value units = json::parse(raw);
        kl_string_free(raw);
        CHECK(units.isArray() && units.asArray().size() == 2 && units.asArray()[0].asString() == "pkg");
    }
}

void testBuiltinDataThroughTheFacade() {
    CHECK(kl_builtin_data_json("no such dataset") == nullptr);

    const json::Value equipment = dataset("equipment");
    CHECK(equipment.isArray() && equipment.asArray().size() == catalog::builtInEquipment().size());
    CHECK(textAt(equipment.asArray()[1], "name") == "Anvil Foundry 6.5 gallon");
    CHECK(textAt(equipment.asArray()[1], "batchLiters") == "9.5");

    const json::Value templates = dataset("recipe_templates");
    CHECK(templates.asArray().size() == catalog::RecipeTemplate::all().size());
    const json::Value& pale = templates.asArray().front();
    CHECK(textAt(pale, "id") == "american-pale-ale" && textAt(pale, "yeast") == "Fermentis SafAle US-05");
    CHECK(pale.find("targetOG")->asRational().toDouble() == 1.054);
    CHECK(pale.find("fermentTempC")->find("upper")->asRational().toDouble() == 20);
    const json::Value& dryHop = pale.find("aromaHops")->asArray().back();
    CHECK(textAt(*dryHop.find("timing"), "kind") == "dryHop" && dryHop.find("timing")->find("value")->asRational().toDouble() == 3);
    CHECK(pale.find("yeastAttenuationPercent") == nullptr);
    CHECK(pale.find("usesWhirlfloc")->asBool());

    const json::Value presets = dataset("fermentation_presets");
    CHECK(presets.asArray().size() == 5 && presets.asArray()[2].find("steps")->asArray().size() == 3);

    const json::Value glossary = dataset("glossary");
    CHECK(glossary.asArray().size() == catalog::allTerms().size());
    CHECK(textAt(glossary.asArray().front(), "term") == "originalGravity");

    const json::Value tools = dataset("tools");
    CHECK(tools.find("calculators")->asArray().size() == catalog::calculators().size());
    CHECK(tools.find("converters")->asArray().size() == catalog::converters().size());
    const json::Value* specs = tools.find("specs");
    CHECK(specs != nullptr && specs->asObject().size() == 25);
    const json::Value* temperature = specs != nullptr ? specs->find("temperature") : nullptr;
    CHECK(temperature != nullptr && textAt(*temperature, "title") == "Temperature Converter");
    if (temperature != nullptr) {
        const json::Value& from = temperature->find("sections")->asArray()[0].find("fields")->asArray()[1];
        CHECK(textAt(from, "kind") == "choice" && from.find("choices")->asArray().size() == 2);
    }

    const json::Value options = dataset("options");
    CHECK(options.find("hopUses")->asArray().size() == catalog::hopUses().size());
    CHECK(options.find("mashStepKinds")->asArray().front().asString() == "Infusion");
    CHECK(options.find("carbonationMethods")->asArray().size() == 3);
    CHECK(options.find("recipeTypes")->asArray().front().asString() == "all grain");
}

}  // namespace

void runCatalogTests() {
    testCatalogContents();
    testCatalogThroughTheFacade();
    testCatalogMatchesOptions();
    testIngredientSearch();
    testIngredientSort();
    testIngredientQueryThroughTheFacade();
    testRecipeTemplates();
    testEquipmentPresetsAndGlossary();
    testToolSpecs();
    testBuiltinDataThroughTheFacade();
    testPriceUnits();
}

}  // namespace kettlelogic::test
