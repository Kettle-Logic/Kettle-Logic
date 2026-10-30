// The facade's view of the built-in datasets and option rules in catalog/. Each dataset is served as JSON for front ends that
// cannot link the C++ types (SwiftUI). Keys are the C++ member names, and decimal figures that the engine
// keeps as text stay text.
#include "kettlelogic/c/kettlelogic.h"

#include "kettlelogic/catalog/equipment.hpp"
#include "kettlelogic/catalog/fermentation_presets.hpp"
#include "kettlelogic/catalog/glossary.hpp"
#include "kettlelogic/catalog/options.hpp"
#include "kettlelogic/catalog/recipe_templates.hpp"
#include "kettlelogic/catalog/tool_specs.hpp"
#include "kettlelogic/json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <utility>

namespace {

using kettlelogic::json::Array;
using kettlelogic::json::Member;
using kettlelogic::json::Object;
using kettlelogic::json::Value;
namespace catalog = kettlelogic::catalog;

char* copyOut(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (out != nullptr) {
        std::memcpy(out, text.c_str(), text.size() + 1);
    }
    return out;
}

Value text(const std::string& value) { return Value::string(value); }

// A double as the shortest JSON number that reads back the same. The datasets hold figures such as 1.054 and 65.5.
Value number(double value) {
    char buffer[40];
    for (int digits = 6; digits <= 17; ++digits) {
        std::snprintf(buffer, sizeof buffer, "%.*g", digits, value);
        if (std::strtod(buffer, nullptr) == value) {
            break;
        }
    }
    return Value::number(buffer);
}

Value integer(int value) { return Value::number(std::to_string(value)); }

template <typename Items, typename Fn>
Value arrayOf(const Items& items, Fn&& make) {
    Array out;
    for (const auto& item : items) {
        out.push_back(make(item));
    }
    return Value::array(std::move(out));
}

Value strings(const std::vector<std::string>& items) {
    return arrayOf(items, [](const std::string& s) { return text(s); });
}

void put(Object& o, const char* key, Value value) { o.push_back(Member{key, std::move(value)}); }

Value equipmentJson() {
    return arrayOf(catalog::builtInEquipment(), [](const catalog::EquipmentProfile& p) {
        Object o;
        put(o, "name", text(p.name));
        put(o, "batchLiters", text(p.batchLiters));
        put(o, "efficiencyPercent", text(p.efficiencyPercent));
        put(o, "evaporationPercent", text(p.evaporationPercent));
        put(o, "trubLossLiters", text(p.trubLossLiters));
        put(o, "boilMinutes", integer(p.boilMinutes));
        put(o, "mashSystem", text(p.mashSystem));
        put(o, "note", text(p.note));
        return Value::object(std::move(o));
    });
}

Value recipeTemplatesJson() {
    return arrayOf(catalog::RecipeTemplate::all(), [](const catalog::RecipeTemplate& t) {
        Object o;
        put(o, "id", text(t.id));
        put(o, "name", text(t.name));
        put(o, "summary", text(t.summary));
        put(o, "bjcpStyle", text(t.bjcpStyle));
        put(o, "targetOG", number(t.targetOG));
        put(o, "targetIBU", number(t.targetIBU));
        put(o, "boilMinutes", integer(t.boilMinutes));
        put(o, "grains", arrayOf(t.grains, [](const catalog::TemplateGrain& g) {
                Object m;
                put(m, "name", text(g.name));
                put(m, "percent", number(g.percent));
                return Value::object(std::move(m));
            }));
        put(o, "boilHops", arrayOf(t.boilHops, [](const catalog::TemplateBoilHop& h) {
                Object m;
                put(m, "name", text(h.name));
                put(m, "minutes", integer(h.minutes));
                put(m, "ibuPercent", number(h.ibuPercent));
                return Value::object(std::move(m));
            }));
        put(o, "aromaHops", arrayOf(t.aromaHops, [](const catalog::TemplateAromaHop& h) {
                using Kind = catalog::TemplateAromaHop::Timing::Kind;
                Object timing;
                put(timing, "kind", text(h.timing.kind == Kind::boil ? "boil" : h.timing.kind == Kind::flameout ? "flameout" : "dryHop"));
                put(timing, "value", integer(h.timing.value));
                Object m;
                put(m, "name", text(h.name));
                put(m, "gramsPerLiter", number(h.gramsPerLiter));
                put(m, "timing", Value::object(std::move(timing)));
                return Value::object(std::move(m));
            }));
        put(o, "yeast", text(t.yeast));
        if (t.yeastAttenuationPercent) {
            put(o, "yeastAttenuationPercent", text(*t.yeastAttenuationPercent));
        }
        put(o, "mashTempC", number(t.mashTempC));
        Object range;
        put(range, "lower", number(t.fermentTempC.lower));
        put(range, "upper", number(t.fermentTempC.upper));
        put(o, "fermentTempC", Value::object(std::move(range)));
        put(o, "usesWhirlfloc", Value::boolean(t.usesWhirlfloc));
        put(o, "tips", strings(t.tips));
        return Value::object(std::move(o));
    });
}

Value fermentationPresetsJson() {
    return arrayOf(catalog::fermentationPresets(), [](const catalog::FermentationPreset& p) {
        Object o;
        put(o, "name", text(p.name));
        put(o, "steps", arrayOf(p.steps, [](const catalog::FermentationStep& s) {
                Object m;
                put(m, "days", number(s.days));
                put(m, "tempC", number(s.tempC));
                return Value::object(std::move(m));
            }));
        return Value::object(std::move(o));
    });
}

Value glossaryJson() {
    return arrayOf(catalog::allTerms(), [](catalog::Term term) {
        Object o;
        put(o, "term", text(catalog::termName(term)));
        put(o, "explanation", text(catalog::explanation(term)));
        return Value::object(std::move(o));
    });
}

Value toolSpecJson(const catalog::ToolSpec& spec) {
    Object o;
    put(o, "title", text(spec.title));
    put(o, "sections", arrayOf(spec.sections, [](const catalog::ToolSection& section) {
            Object s;
            put(s, "title", text(section.title));
            if (section.footer) {
                put(s, "footer", text(*section.footer));
            }
            put(s, "tool", text(section.tool));
            put(s, "fields", arrayOf(section.fields, [](const catalog::ToolField& f) {
                    using Kind = catalog::ToolField::Kind;
                    Object m;
                    put(m, "key", text(f.key));
                    put(m, "label", text(f.label));
                    put(m, "kind", text(f.kind == Kind::number ? "number" : f.kind == Kind::toggle ? "toggle" : "choice"));
                    put(m, "defaultValue", text(f.defaultValue));
                    if (f.kind == Kind::choice) {
                        put(m, "choices", arrayOf(f.choices, [](const catalog::ToolField::Choice& c) {
                                Object choice;
                                put(choice, "value", text(c.value));
                                put(choice, "label", text(c.label));
                                return Value::object(std::move(choice));
                            }));
                    }
                    if (!f.enabledBy.empty()) {
                        put(m, "enabledBy", text(f.enabledBy));
                    }
                    return Value::object(std::move(m));
                }));
            put(s, "readouts", arrayOf(section.readouts, [](const catalog::ToolReadout& r) {
                    Object m;
                    put(m, "key", text(r.key));
                    put(m, "label", text(r.label));
                    put(m, "unit", text(r.unit));
                    if (!r.enabledBy.empty()) {
                        put(m, "enabledBy", text(r.enabledBy));
                    }
                    return Value::object(std::move(m));
                }));
            return Value::object(std::move(s));
        }));
    return Value::object(std::move(o));
}

// {"calculators": [name...], "converters": [name...], "specs": {name: spec}}. The names are the stable tool names.
Value toolsJson() {
    auto names = [](const std::vector<catalog::ToolKind>& kinds) {
        return arrayOf(kinds, [](catalog::ToolKind kind) { return text(catalog::toolKindName(kind)); });
    };
    Object specs;
    for (const auto* list : {&catalog::calculators(), &catalog::converters()}) {
        for (const catalog::ToolKind kind : *list) {
            put(specs, catalog::toolKindName(kind).c_str(), toolSpecJson(catalog::toolSpec(kind)));
        }
    }
    Object o;
    put(o, "calculators", names(catalog::calculators()));
    put(o, "converters", names(catalog::converters()));
    put(o, "specs", Value::object(std::move(specs)));
    return Value::object(std::move(o));
}

Value optionsJson() {
    Object o;
    put(o, "mashStepKinds", strings(catalog::mashStepKinds()));
    put(o, "carbonationMethods", strings(catalog::carbonationMethods()));
    put(o, "currencyCodes", strings(catalog::currencyCodes()));
    put(o, "recipeTypes", strings(catalog::recipeTypes()));
    put(o, "fermentableTypes", strings(catalog::fermentableTypes()));
    put(o, "fermentableUnits", strings(catalog::fermentableUnits()));
    put(o, "miscellaneousTypes", strings(catalog::miscellaneousTypes()));
    put(o, "miscellaneousUnits", strings(catalog::miscellaneousUnits()));
    put(o, "hopForms", strings(catalog::hopForms()));
    put(o, "hopUses", strings(catalog::hopUses()));
    put(o, "hopUnits", strings(catalog::hopUnits()));
    put(o, "hopTimeUnits", strings(catalog::hopTimeUnits()));
    put(o, "cultureTypes", strings(catalog::cultureTypes()));
    put(o, "cultureForms", strings(catalog::cultureForms()));
    put(o, "cultureUnits", strings(catalog::cultureUnits()));
    return Value::object(std::move(o));
}

}  // namespace

extern "C" char* kl_price_units_json(const char* amountUnit) {
    try {
        return copyOut(kettlelogic::json::serialize(strings(catalog::priceUnits(amountUnit != nullptr ? amountUnit : "")), -1));
    } catch (...) {
        return nullptr;
    }
}

extern "C" char* kl_builtin_data_json(const char* dataset) {
    try {
        const std::string name = dataset != nullptr ? dataset : "";
        Value value;
        if (name == "equipment") value = equipmentJson();
        else if (name == "recipe_templates") value = recipeTemplatesJson();
        else if (name == "fermentation_presets") value = fermentationPresetsJson();
        else if (name == "glossary") value = glossaryJson();
        else if (name == "tools") value = toolsJson();
        else if (name == "options") value = optionsJson();
        else return nullptr;
        return copyOut(kettlelogic::json::serialize(value, -1));
    } catch (...) {
        return nullptr;
    }
}
