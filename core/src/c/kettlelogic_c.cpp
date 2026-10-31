#include "kettlelogic/c/kettlelogic.h"

#include "kettlelogic/calc/brew_sheet.hpp"
#include "kettlelogic/calc/procedure.hpp"
#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/calc/brew_plan.hpp"
#include "kettlelogic/calc/brew_water.hpp"
#include "kettlelogic/calc/journal.hpp"
#include "kettlelogic/calc/starter.hpp"
#include "kettlelogic/calc/style.hpp"
#include "kettlelogic/calc/tools.hpp"
#include "kettlelogic/catalog/ingredient_query.hpp"
#include "kettlelogic/catalog/ingredients.hpp"
#include "kettlelogic/command.hpp"
#include "kettlelogic/io/beerxml_import.hpp"
#include "kettlelogic/io/brew_sheet_pdf.hpp"
#include "kettlelogic/io/document_file.hpp"
#include "kettlelogic/io/recipe_export.hpp"
#include "kettlelogic/io/recipe_pdf.hpp"
#include "kettlelogic/json.hpp"
#include "kettlelogic/model/beerjson.hpp"
#include "kettlelogic/model/commands.hpp"
#include "kettlelogic/model/model.hpp"
#include "kettlelogic/model/sidecar.hpp"
#include "kettlelogic/quantity.hpp"
#include "kettlelogic/version.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <stdexcept>
#include <utility>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using kettlelogic::Measure;
using kettlelogic::Quantity;
using kettlelogic::Rational;
using kettlelogic::Unit;
using kettlelogic::json::Array;
using kettlelogic::json::Member;
using kettlelogic::json::Object;
using kettlelogic::json::Value;
using kettlelogic::model::Document;
using kettlelogic::model::Recipe;

struct kl_document {
    Document document;
    kettlelogic::CommandStack<Document> stack;
    std::string source = "current";
    // The undo depth where the open group began, or nothing when no group is open.
    std::optional<std::size_t> groupDepth;
};

namespace {

// A recipe a new document starts with. It passes the schema's required rules.
constexpr const char* kNewDocument = R"({"beerjson": {"version": 3, "recipes": [{
    "name": "New Recipe", "type": "all grain", "author": "Unknown",
    "batch_size": {"value": 5, "unit": "gal"},
    "efficiency": {"brewhouse": {"value": 72, "unit": "%"}},
    "ingredients": {"fermentable_additions": []}}]}})";

char* copyOut(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (out != nullptr) {
        std::memcpy(out, text.c_str(), text.size() + 1);
    }
    return out;
}

void setError(char** error, const std::string& message) {
    if (error != nullptr) {
        *error = copyOut(message);
    }
}

// Runs `fn`, turning any exception into an error message and -1.
template <typename Fn>
int guarded(char** error, Fn&& fn) {
    try {
        fn();
        return 0;
    } catch (const std::exception& e) {
        setError(error, e.what());
    } catch (...) {
        setError(error, "unknown error");
    }
    return -1;
}

Value number(const std::string& lexeme) {
    return Value::number(lexeme);
}

Value finite(double value) {
    if (!std::isfinite(value)) {
        return Value{};
    }
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.10g", value);
    return number(buffer);
}

Value optionalFinite(const std::optional<double>& value) {
    return value ? finite(*value) : Value{};
}

Value rangeJson(const kettlelogic::calc::Range& range) {
    Object o;
    o.push_back(Member{"low", finite(range.low)});
    o.push_back(Member{"high", finite(range.high)});
    return Value::object(std::move(o));
}

Value quantityJson(const Quantity& q) {
    Object o;
    o.push_back(Member{"value", Value::string(q.valueIn(q.unit()).toDecimal())});
    o.push_back(Member{"unit", Value::string(std::string(kettlelogic::beerJsonName(q.unit())))});
    return Value::object(std::move(o));
}

Value measureJson(const kettlelogic::Measure& m) {
    Object o;
    o.push_back(Member{"value", Value::string(m.value().toDecimal())});
    o.push_back(Member{"unit", Value::string(m.unit())});
    return Value::object(std::move(o));
}

Value stringArray(const std::vector<std::string>& items) {
    Array out;
    for (const std::string& item : items) {
        out.push_back(Value::string(item));
    }
    return Value::array(std::move(out));
}

Value priceJson(const std::optional<kettlelogic::model::Price>& price) {
    if (!price) {
        return Value{};
    }
    Object o;
    o.push_back(Member{"amount", Value::string(price->amount.toDecimal())});
    o.push_back(Member{"per", Value::string(std::string(kettlelogic::beerJsonName(price->per)))});
    return Value::object(std::move(o));
}

Value costJson(const kettlelogic::calc::RecipeCost& cost) {
    Object o;
    o.push_back(Member{"currency", Value::string(cost.currency)});
    o.push_back(Member{"total", finite(cost.total)});
    o.push_back(Member{"perLiter", optionalFinite(cost.perLiter)});
    o.push_back(Member{"perGallon", optionalFinite(cost.perGallon)});
    o.push_back(Member{"unpriced", number(std::to_string(cost.unpriced))});
    Array lines;
    for (const auto& line : cost.byIngredient) {
        Object entry;
        entry.push_back(Member{"ingredient", Value::string(line.ingredient.toString())});
        entry.push_back(Member{"cost", finite(line.cost)});
        lines.push_back(Value::object(std::move(entry)));
    }
    o.push_back(Member{"byIngredient", Value::array(std::move(lines))});
    return Value::object(std::move(o));
}

Value resultsJson(const kettlelogic::calc::Results& r) {
    Object o;
    o.push_back(Member{"originalGravity", optionalFinite(r.originalGravity)});
    o.push_back(Member{"finalGravity", optionalFinite(r.finalGravity)});
    o.push_back(Member{"abv", optionalFinite(r.abv)});
    o.push_back(Member{"ibu", optionalFinite(r.ibu)});
    o.push_back(Member{"srm", optionalFinite(r.srm)});
    o.push_back(Member{"preBoilLiters", optionalFinite(r.preBoilLiters)});
    o.push_back(Member{"postBoilLiters", optionalFinite(r.postBoilLiters)});
    o.push_back(Member{"sulfiteSo2Ppm", optionalFinite(r.sulfiteSo2Ppm)});
    o.push_back(Member{"tartaricAcidGramsPerLiter", optionalFinite(r.tartaricAcidGramsPerLiter)});
    o.push_back(Member{"cost", r.cost ? costJson(*r.cost) : Value{}});
    if (r.drink) {
        Object drink;
        drink.push_back(Member{"gravityGrams", optionalFinite(r.drink->gravityGrams)});
        drink.push_back(Member{"alcoholGrams", optionalFinite(r.drink->alcoholGrams)});
        drink.push_back(Member{"notes", stringArray(r.drink->notes)});
        o.push_back(Member{"drink", Value::object(std::move(drink))});
    } else {
        o.push_back(Member{"drink", Value{}});
    }
    o.push_back(Member{"notes", stringArray(r.notes)});
    return Value::object(std::move(o));
}

Value recipeJson(const Recipe& recipe) {
    Object o;
    o.push_back(Member{"id", number(std::to_string(recipe.id))});
    o.push_back(Member{"name", Value::string(recipe.name)});
    o.push_back(Member{"type", Value::string(std::string(kettlelogic::model::recipeTypeName(recipe.type)))});
    o.push_back(Member{"batchSize", quantityJson(recipe.batchSize)});
    o.push_back(Member{"brewhouseEfficiency", quantityJson(recipe.efficiency.brewhouse)});
    o.push_back(Member{"fermentationEfficiency", quantityJson(recipe.fermentationEfficiency)});
    o.push_back(Member{"folderId", recipe.folder ? number(std::to_string(*recipe.folder)) : Value{}});
    o.push_back(Member{"bjcpStyle", recipe.bjcpStyle ? Value::string(*recipe.bjcpStyle) : Value{}});
    o.push_back(Member{"planning", Value::string(recipe.planning)});
    o.push_back(Member{"currency", Value::string(recipe.currency)});

    Object boil;
    boil.push_back(Member{"evaporationRate", quantityJson(recipe.boilOff.evaporationRate)});
    boil.push_back(Member{"trubLoss", quantityJson(recipe.boilOff.trubLoss)});
    o.push_back(Member{"boilOff", Value::object(std::move(boil))});

    Array fermentables;
    for (const auto& f : recipe.fermentables) {
        Object item;
        item.push_back(Member{"id", number(std::to_string(f.id))});
        item.push_back(Member{"uuid", Value::string(f.uuid.toString())});
        item.push_back(Member{"price", priceJson(f.price)});
        item.push_back(Member{"name", Value::string(f.name)});
        item.push_back(Member{"type", Value::string(f.type)});
        item.push_back(Member{"amount", quantityJson(f.amount)});
        item.push_back(Member{"potential", f.yield.potential ? measureJson(*f.yield.potential) : Value{}});
        item.push_back(Member{"color", measureJson(f.color)});
        fermentables.push_back(Value::object(std::move(item)));
    }
    o.push_back(Member{"fermentables", Value::array(std::move(fermentables))});

    Array miscellaneous;
    for (const auto& m : recipe.miscellaneous.value_or(std::vector<kettlelogic::model::Miscellaneous>{})) {
        Object item;
        item.push_back(Member{"id", number(std::to_string(m.id))});
        item.push_back(Member{"uuid", Value::string(m.uuid.toString())});
        item.push_back(Member{"price", priceJson(m.price)});
        item.push_back(Member{"name", Value::string(m.name)});
        item.push_back(Member{"type", Value::string(m.type)});
        item.push_back(Member{"amount", quantityJson(m.amount)});
        miscellaneous.push_back(Value::object(std::move(item)));
    }
    o.push_back(Member{"miscellaneous", Value::array(std::move(miscellaneous))});

    Array hops;
    for (const auto& h : recipe.hops.value_or(std::vector<kettlelogic::model::Hop>{})) {
        Object item;
        item.push_back(Member{"id", number(std::to_string(h.id))});
        item.push_back(Member{"uuid", Value::string(h.uuid.toString())});
        item.push_back(Member{"price", priceJson(h.price)});
        item.push_back(Member{"name", Value::string(h.name)});
        item.push_back(Member{"alphaAcid", quantityJson(h.alphaAcid)});
        item.push_back(Member{"betaAcid", h.betaAcid ? quantityJson(*h.betaAcid) : Value{}});
        item.push_back(Member{"amount", quantityJson(h.amount)});
        item.push_back(Member{"form", h.form ? Value::string(*h.form) : Value{}});
        item.push_back(Member{"use", h.timing.use ? Value::string(*h.timing.use) : Value{}});
        item.push_back(Member{"time", h.timing.time ? quantityJson(*h.timing.time) : Value{}});
        hops.push_back(Value::object(std::move(item)));
    }
    o.push_back(Member{"hops", Value::array(std::move(hops))});

    Array cultures;
    for (const auto& c : recipe.cultures.value_or(std::vector<kettlelogic::model::Culture>{})) {
        Object item;
        item.push_back(Member{"id", number(std::to_string(c.id))});
        item.push_back(Member{"uuid", Value::string(c.uuid.toString())});
        item.push_back(Member{"price", priceJson(c.price)});
        item.push_back(Member{"name", Value::string(c.name)});
        item.push_back(Member{"type", Value::string(c.type)});
        item.push_back(Member{"form", Value::string(c.form)});
        item.push_back(Member{"amount", quantityJson(c.amount)});
        item.push_back(Member{"attenuation", c.attenuation ? quantityJson(*c.attenuation) : Value{}});
        cultures.push_back(Value::object(std::move(item)));
    }
    o.push_back(Member{"cultures", Value::array(std::move(cultures))});

    Array waters;
    for (const auto& w : recipe.waters.value_or(std::vector<kettlelogic::model::WaterAddition>{})) {
        Object item;
        item.push_back(Member{"id", number(std::to_string(w.id))});
        item.push_back(Member{"name", Value::string(w.water.name)});
        item.push_back(Member{"calcium", quantityJson(w.water.calcium)});
        item.push_back(Member{"bicarbonate", quantityJson(w.water.bicarbonate)});
        item.push_back(Member{"sulfate", quantityJson(w.water.sulfate)});
        item.push_back(Member{"chloride", quantityJson(w.water.chloride)});
        item.push_back(Member{"sodium", quantityJson(w.water.sodium)});
        item.push_back(Member{"magnesium", quantityJson(w.water.magnesium)});
        item.push_back(Member{"amount", w.amount ? quantityJson(*w.amount) : Value{}});
        waters.push_back(Value::object(std::move(item)));
    }
    o.push_back(Member{"waters", Value::array(std::move(waters))});

    o.push_back(Member{"results", resultsJson(kettlelogic::calc::calculate(recipe))});
    return Value::object(std::move(o));
}

Quantity quantityFrom(const char* value, const char* unitName) {
    const auto unit = kettlelogic::unitFromBeerJson(unitName);
    if (!unit) {
        throw std::invalid_argument(std::string("unknown unit \"") + unitName + "\"");
    }
    return Quantity::fromUnit(Rational::fromDecimal(value), *unit);
}

// Helpers for the set functions: they find an item to edit, and read optional values.
const Recipe& recipeIn(const Document& document, kl_recipe_id id) {
    const Recipe* recipe = document.findRecipe(id);
    if (recipe == nullptr) {
        throw std::invalid_argument("no recipe with id " + std::to_string(id));
    }
    return *recipe;
}

template <typename T>
T itemIn(const std::vector<T>* list, kl_ingredient_id id, const char* noun) {
    if (list != nullptr) {
        for (const T& item : *list) {
            if (item.id == id) {
                return item;
            }
        }
    }
    throw std::invalid_argument(std::string("no ") + noun + " with id " + std::to_string(id));
}

template <typename T>
const std::vector<T>* listIn(const std::optional<std::vector<T>>& list) {
    return list ? &*list : nullptr;
}

std::optional<Quantity> optionalQuantity(const char* value, const char* unitName) {
    if (value == nullptr) {
        return std::nullopt;
    }
    return quantityFrom(value, unitName);
}

std::optional<std::string> optionalText(const char* text) {
    if (text == nullptr) {
        return std::nullopt;
    }
    return std::string(text);
}

Measure measureFrom(const char* value, const char* unitName) {
    return Measure::fromUnit(Rational::fromDecimal(value), unitName);
}

}  // namespace

namespace {

std::string textOf(const Value& object, const char* key) {
    const Value* v = object.find(key);
    return v != nullptr && v->isString() ? v->asString() : std::string();
}

std::optional<double> numberOf(const Value& object, const char* key) {
    const Value* v = object.find(key);
    if (v == nullptr || !v->isNumber()) return std::nullopt;
    return std::strtod(v->numberLexeme().c_str(), nullptr);
}

// A range object {"low", "high"} under `key`. Throws if either end is missing.
kettlelogic::styles::Range rangeOf(const Value& object, const char* key) {
    const Value* v = object.find(key);
    if (v == nullptr || !v->isObject()) throw std::invalid_argument(std::string("a style needs its \"") + key + "\" range");
    const std::optional<double> low = numberOf(*v, "low");
    const std::optional<double> high = numberOf(*v, "high");
    if (!low || !high) throw std::invalid_argument(std::string("the \"") + key + "\" range needs a low and a high number");
    return kettlelogic::styles::Range{*low, *high};
}

// A user style from its JSON text: {"name", "og", "fg", "ibu", "srm", "abv"}, each range {"low", "high"}.
kettlelogic::styles::UserStyle userStyleFrom(const char* id, const char* text) {
    if (text == nullptr) throw std::invalid_argument("no style given");
    const Value root = kettlelogic::json::parse(text);
    if (!root.isObject()) throw std::invalid_argument("a style is a JSON object");
    kettlelogic::styles::UserStyle style;
    style.id = id != nullptr ? id : "";
    style.name = textOf(root, "name");
    style.ranges.og = rangeOf(root, "og");
    style.ranges.fg = rangeOf(root, "fg");
    style.ranges.ibu = rangeOf(root, "ibu");
    style.ranges.srm = rangeOf(root, "srm");
    style.ranges.abv = rangeOf(root, "abv");
    return style;
}

std::vector<std::string> textsOf(const Value& object, const char* key) {
    std::vector<std::string> out;
    const Value* v = object.find(key);
    if (v != nullptr && v->isArray()) {
        for (const Value& item : v->asArray()) {
            if (item.isString()) out.push_back(item.asString());
        }
    }
    return out;
}

Value textsJson(const std::vector<std::string>& items) {
    Array out;
    for (const std::string& item : items) out.push_back(Value::string(item));
    return Value::array(std::move(out));
}

Value metricJson(const std::optional<kettlelogic::catalog::IngredientMetric>& metric) {
    if (!metric) return Value{};
    Object o;
    o.push_back(Member{"label", Value::string(metric->label)});
    o.push_back(Member{"unit", Value::string(metric->unit)});
    return Value::object(std::move(o));
}

}  // namespace

extern "C" {

kl_document* kl_document_new(char** error) {
    kl_document* document = nullptr;
    if (guarded(error, [&] {
            document = new kl_document{};
            document->document = kettlelogic::model::readBeerJson(kNewDocument);
            document->source = "current";
        }) != 0) {
        delete document;
        return nullptr;
    }
    return document;
}

kl_document* kl_document_open(const char* path, char** error) {
    kl_document* document = nullptr;
    if (guarded(error, [&] {
            auto loaded = kettlelogic::io::loadDocument(path);
            document = new kl_document{};
            document->document = std::move(loaded.document);
            switch (loaded.source) {
                case kettlelogic::io::Source::Current: document->source = "current"; break;
                case kettlelogic::io::Source::Autosave: document->source = "autosave"; break;
                case kettlelogic::io::Source::Previous: document->source = "previous"; break;
            }
        }) != 0) {
        delete document;
        return nullptr;
    }
    return document;
}

int kl_document_save(kl_document* document, const char* path, char** error) {
    return guarded(error, [&] {
        kettlelogic::io::saveDocument(document->document, path);
        document->source = "current";
    });
}

void kl_document_free(kl_document* document) {
    delete document;
}

char* kl_document_snapshot_json(const kl_document* document) {
    try {
        Array recipes;
        for (const Recipe& recipe : document->document.recipes) {
            recipes.push_back(recipeJson(recipe));
        }
        Array folders;
        for (const kettlelogic::model::Folder& folder : document->document.folders) {
            Object o;
            o.push_back(Member{"id", number(std::to_string(folder.id))});
            o.push_back(Member{"name", Value::string(folder.name)});
            o.push_back(Member{"trash", Value::boolean(folder.trash)});
            folders.push_back(Value::object(std::move(o)));
        }
        Object root;
        root.push_back(Member{"source", Value::string(document->source)});
        root.push_back(Member{"recipes", Value::array(std::move(recipes))});
        root.push_back(Member{"folders", Value::array(std::move(folders))});
        return copyOut(kettlelogic::json::serialize(Value::object(std::move(root)), -1));
    } catch (...) {
        return nullptr;
    }
}

int kl_set_recipe_name(kl_document* document, kl_recipe_id recipe, const char* name, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::setRecipeName(recipe, name));
    });
}

int kl_set_recipe_type(kl_document* document, kl_recipe_id recipe, const char* type_name, char** error) {
    return guarded(error, [&] {
        const auto type = kettlelogic::model::recipeTypeFromName(type_name);
        if (!type) {
            throw std::invalid_argument(std::string("unknown recipe type \"") + type_name + "\"");
        }
        document->stack.execute(document->document, kettlelogic::model::setRecipeType(recipe, *type));
    });
}

int kl_set_batch_size(kl_document* document, kl_recipe_id recipe, const char* value, const char* unit, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document,
                                kettlelogic::model::setBatchSize(recipe, quantityFrom(value, unit)));
    });
}

int kl_set_fermentation_efficiency(kl_document* document, kl_recipe_id recipe, const char* percent, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document,
                                kettlelogic::model::setFermentationEfficiency(recipe, quantityFrom(percent, "%")));
    });
}

int kl_set_boil_off(kl_document* document, kl_recipe_id recipe, const char* evaporation_percent_per_hour,
                    const char* trub_loss_liters, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::BoilOff boilOff;
        boilOff.evaporationRate = quantityFrom(evaporation_percent_per_hour, "%");
        boilOff.trubLoss = quantityFrom(trub_loss_liters, "l");
        document->stack.execute(document->document, kettlelogic::model::setBoilOff(recipe, boilOff));
    });
}

int kl_add_recipe(kl_document* document, kl_recipe_id* created, char** error) {
    return guarded(error, [&] {
        Recipe recipe;
        recipe.name = "New Recipe";
        recipe.author = "Unknown";
        recipe.batchSize = quantityFrom("5", "gal");
        recipe.efficiency.brewhouse = quantityFrom("72", "%");
        document->stack.execute(document->document, kettlelogic::model::addRecipe(std::move(recipe)));
        if (created != nullptr) {
            *created = document->document.recipes.back().id;
        }
    });
}

int kl_add_fermentable(kl_document* document, kl_recipe_id recipe, const char* name, const char* type,
                       const char* amount, const char* amount_unit, const char* yield, const char* yield_unit,
                       const char* color, const char* color_unit, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Fermentable fermentable;
        fermentable.name = name;
        fermentable.type = type;
        fermentable.amount = quantityFrom(amount, amount_unit);
        kettlelogic::model::Yield yieldRecord;
        yieldRecord.potential = measureFrom(yield, yield_unit);
        fermentable.yield = yieldRecord;
        fermentable.color = measureFrom(color, color_unit);
        document->stack.execute(document->document,
                                kettlelogic::model::addFermentable(recipe, std::move(fermentable)));
    });
}

int kl_remove_fermentable(kl_document* document, kl_recipe_id recipe, kl_ingredient_id fermentable, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::removeFermentable(recipe, fermentable));
    });
}

int kl_add_miscellaneous(kl_document* document, kl_recipe_id recipe, const char* name, const char* type,
                         const char* amount, const char* amount_unit, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Miscellaneous miscellaneous;
        miscellaneous.name = name;
        miscellaneous.type = type;
        miscellaneous.amount = quantityFrom(amount, amount_unit);
        document->stack.execute(document->document,
                                kettlelogic::model::addMiscellaneous(recipe, std::move(miscellaneous)));
    });
}

int kl_remove_miscellaneous(kl_document* document, kl_recipe_id recipe, kl_ingredient_id miscellaneous,
                            char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::removeMiscellaneous(recipe, miscellaneous));
    });
}

int kl_add_hop(kl_document* document, kl_recipe_id recipe, const char* name, const char* alpha_percent,
               const char* beta_percent, const char* amount, const char* amount_unit, const char* form, const char* use, const char* time,
               const char* time_unit, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Hop hop;
        hop.name = name;
        hop.alphaAcid = quantityFrom(alpha_percent, "%");
        hop.betaAcid = optionalQuantity(beta_percent, "%");
        hop.amount = quantityFrom(amount, amount_unit);
        if (form != nullptr) {
            hop.form = form;
        }
        if (use != nullptr) {
            hop.timing.use = use;
        }
        if (time != nullptr) {
            if (time_unit == nullptr) {
                throw std::invalid_argument("a hop time needs a unit");
            }
            hop.timing.time = quantityFrom(time, time_unit);
        }
        document->stack.execute(document->document, kettlelogic::model::addHop(recipe, std::move(hop)));
    });
}

int kl_remove_hop(kl_document* document, kl_recipe_id recipe, kl_ingredient_id hop, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::removeHop(recipe, hop));
    });
}

int kl_add_culture(kl_document* document, kl_recipe_id recipe, const char* name, const char* type,
                   const char* form, const char* amount, const char* amount_unit, const char* attenuation_percent,
                   char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Culture culture;
        culture.name = name;
        culture.type = type;
        culture.form = form;
        culture.amount = quantityFrom(amount, amount_unit);
        if (attenuation_percent != nullptr) {
            culture.attenuation = quantityFrom(attenuation_percent, "%");
        }
        document->stack.execute(document->document, kettlelogic::model::addCulture(recipe, std::move(culture)));
    });
}

int kl_remove_culture(kl_document* document, kl_recipe_id recipe, kl_ingredient_id culture, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::removeCulture(recipe, culture));
    });
}

int kl_add_water(kl_document* document, kl_recipe_id recipe, const char* name, const char* calcium,
                 const char* bicarbonate, const char* sulfate, const char* chloride, const char* sodium,
                 const char* magnesium, const char* amount_liters, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::WaterAddition addition;
        addition.water.name = name;
        addition.water.calcium = quantityFrom(calcium, "mg/l");
        addition.water.bicarbonate = quantityFrom(bicarbonate, "mg/l");
        addition.water.sulfate = quantityFrom(sulfate, "mg/l");
        addition.water.chloride = quantityFrom(chloride, "mg/l");
        addition.water.sodium = quantityFrom(sodium, "mg/l");
        addition.water.magnesium = quantityFrom(magnesium, "mg/l");
        if (amount_liters != nullptr) {
            addition.amount = quantityFrom(amount_liters, "l");
        }
        document->stack.execute(document->document, kettlelogic::model::addWater(recipe, std::move(addition)));
    });
}

int kl_remove_water(kl_document* document, kl_recipe_id recipe, kl_ingredient_id water, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::removeWater(recipe, water));
    });
}

int kl_apply_equipment(kl_document* document, kl_recipe_id recipe, const char* batch_value, const char* batch_unit,
                       const char* efficiency_percent, const char* evaporation_percent_per_hour,
                       const char* trub_loss_liters, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::BoilOff boilOff;
        boilOff.evaporationRate = quantityFrom(evaporation_percent_per_hour, "%");
        boilOff.trubLoss = quantityFrom(trub_loss_liters, "l");
        document->stack.execute(document->document,
                                kettlelogic::model::applyEquipment(recipe, quantityFrom(batch_value, batch_unit),
                                                                   quantityFrom(efficiency_percent, "%"), boilOff));
    });
}

int kl_set_recipe_style(kl_document* document, kl_recipe_id recipe, const char* style_id, char** error) {
    return guarded(error, [&] {
        std::optional<std::string> style;
        if (style_id != nullptr) {
            if (!kettlelogic::calc::findStyle(document->document, style_id)) {
                throw std::invalid_argument(std::string("no style with id \"") + style_id + "\"");
            }
            style = std::string(style_id);
        }
        document->stack.execute(document->document, kettlelogic::model::setBjcpStyle(recipe, style));
    });
}

int kl_add_user_style(kl_document* document, const char* style_json, char** created_id, char** error) {
    return guarded(error, [&] {
        const kettlelogic::styles::UserStyle style = userStyleFrom(nullptr, style_json);
        document->stack.execute(document->document, kettlelogic::model::addUserStyle(style));
        if (created_id != nullptr) {
            *created_id = copyOut(document->document.userStyles.back().id);
        }
    });
}

int kl_set_user_style(kl_document* document, const char* style_id, const char* style_json, char** error) {
    return guarded(error, [&] {
        if (style_id == nullptr) throw std::invalid_argument("no style id given");
        document->stack.execute(document->document,
                                kettlelogic::model::setUserStyle(userStyleFrom(style_id, style_json)));
    });
}

int kl_remove_user_style(kl_document* document, const char* style_id, char** error) {
    return guarded(error, [&] {
        if (style_id == nullptr) throw std::invalid_argument("no style id given");
        document->stack.execute(document->document, kettlelogic::model::removeUserStyle(style_id));
    });
}

int kl_add_folder(kl_document* document, const char* name, kl_folder_id* created, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Folder folder;
        folder.name = name;
        document->stack.execute(document->document, kettlelogic::model::addFolder(std::move(folder)));
        if (created != nullptr) {
            *created = document->document.folders.back().id;
        }
    });
}

int kl_rename_folder(kl_document* document, kl_folder_id folder, const char* name, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::renameFolder(folder, name));
    });
}

int kl_remove_folder(kl_document* document, kl_folder_id folder, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::removeFolder(folder));
    });
}

int kl_move_recipe_to_folder(kl_document* document, kl_recipe_id recipe, kl_folder_id folder, char** error) {
    return guarded(error, [&] {
        std::optional<kl_folder_id> target;
        if (folder != 0) {
            target = folder;
        }
        document->stack.execute(document->document, kettlelogic::model::moveRecipeToFolder(recipe, target));
    });
}

namespace {

const char* metricName(kettlelogic::calc::Metric metric) {
    switch (metric) {
        case kettlelogic::calc::Metric::OriginalGravity: return "originalGravity";
        case kettlelogic::calc::Metric::FinalGravity: return "finalGravity";
        case kettlelogic::calc::Metric::Abv: return "abv";
        case kettlelogic::calc::Metric::Ibu: return "ibu";
        case kettlelogic::calc::Metric::Srm: return "srm";
    }
    return "";
}

const char* fitName(kettlelogic::calc::Fit fit) {
    switch (fit) {
        case kettlelogic::calc::Fit::Below: return "below";
        case kettlelogic::calc::Fit::Within: return "within";
        case kettlelogic::calc::Fit::Above: return "above";
        case kettlelogic::calc::Fit::Unknown: return "unknown";
    }
    return "";
}

}  // namespace

char* kl_styles_json(void) {
    try {
        Array styles;
        for (const kettlelogic::calc::BjcpStyle& style : kettlelogic::calc::bjcpStyles()) {
            Object o;
            o.push_back(Member{"id", Value::string(std::string(style.id))});
            o.push_back(Member{"name", Value::string(std::string(style.name))});
            o.push_back(Member{"og", rangeJson(style.og)});
            o.push_back(Member{"fg", rangeJson(style.fg)});
            o.push_back(Member{"ibu", rangeJson(style.ibu)});
            o.push_back(Member{"srm", rangeJson(style.srm)});
            o.push_back(Member{"abv", rangeJson(style.abv)});
            styles.push_back(Value::object(std::move(o)));
        }
        return copyOut(kettlelogic::json::serialize(Value::array(std::move(styles)), -1));
    } catch (...) {
        return nullptr;
    }
}

char* kl_user_styles_json(const kl_document* document, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            Array items;
            for (const kettlelogic::styles::UserStyle& style : document->document.userStyles) {
                Object o;
                o.push_back(Member{"id", Value::string(style.id)});
                o.push_back(Member{"name", Value::string(style.name)});
                o.push_back(Member{"og", rangeJson(style.ranges.og)});
                o.push_back(Member{"fg", rangeJson(style.ranges.fg)});
                o.push_back(Member{"ibu", rangeJson(style.ranges.ibu)});
                o.push_back(Member{"srm", rangeJson(style.ranges.srm)});
                o.push_back(Member{"abv", rangeJson(style.ranges.abv)});
                items.push_back(Value::object(std::move(o)));
            }
            result = copyOut(kettlelogic::json::serialize(Value::array(std::move(items)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

char* kl_ingredient_catalog_json(void) {
    try {
        namespace catalog = kettlelogic::catalog;
        auto text = [](const std::string& value) { return Value::string(value); };
        auto entries = [&](const auto& items, auto fill) {
            Array out;
            for (const auto& item : items) {
                Object o;
                o.push_back(Member{"name", text(item.name)});
                o.push_back(Member{"group", text(item.group)});
                fill(o, item);
                out.push_back(Value::object(std::move(o)));
            }
            return Value::array(std::move(out));
        };
        auto add = [&](Object& o, const char* key, const std::string& value) { o.push_back(Member{key, text(value)}); };

        Object root;
        root.push_back(Member{"fermentables", entries(catalog::fermentables(), [&](Object& o, const catalog::FermentableTemplate& f) {
                                  add(o, "type", f.type);
                                  add(o, "yield_sg", f.yieldSG);
                                  add(o, "color_srm", f.colorSRM);
                              })});
        root.push_back(Member{"hops", entries(catalog::hops(), [&](Object& o, const catalog::HopTemplate& h) {
                                  add(o, "alpha_percent", h.alphaPercent);
                                  if (h.betaPercent) add(o, "beta_percent", *h.betaPercent);
                              })});
        root.push_back(Member{"cultures", entries(catalog::cultures(), [&](Object& o, const catalog::CultureTemplate& c) {
                                  add(o, "type", c.type);
                                  add(o, "form", c.form);
                                  if (c.attenuationPercent) add(o, "attenuation_percent", *c.attenuationPercent);
                              })});
        root.push_back(Member{"waters", entries(catalog::waters(), [&](Object& o, const catalog::WaterTemplate& w) {
                                  add(o, "calcium", w.calcium);
                                  add(o, "bicarbonate", w.bicarbonate);
                                  add(o, "sulfate", w.sulfate);
                                  add(o, "chloride", w.chloride);
                                  add(o, "sodium", w.sodium);
                                  add(o, "magnesium", w.magnesium);
                              })});
        root.push_back(Member{"miscellaneous", entries(catalog::miscellaneous(), [&](Object& o, const catalog::MiscellaneousTemplate& m) {
                                  add(o, "type", m.type);
                                  add(o, "unit", m.unit);
                              })});
        return copyOut(kettlelogic::json::serialize(Value::object(std::move(root)), -1));
    } catch (...) {
        return nullptr;
    }
}

char* kl_ingredient_query_json(const char* request_json) {
    try {
        namespace catalog = kettlelogic::catalog;
        if (request_json == nullptr) return nullptr;
        const Value request = kettlelogic::json::parse(request_json);
        const Value* listed = request.find("entries");
        if (!request.isObject() || listed == nullptr || !listed->isArray()) return nullptr;

        std::vector<catalog::IngredientEntry> entries;
        for (const Value& item : listed->asArray()) {
            catalog::IngredientEntry e;
            e.id = textOf(item, "id");
            e.name = textOf(item, "name");
            e.group = textOf(item, "group");
            e.type = textOf(item, "type");
            e.form = textOf(item, "form");
            const Value* custom = item.find("custom");
            e.custom = custom != nullptr && custom->isBool() && custom->asBool();
            e.primary = numberOf(item, "primary");
            e.secondary = numberOf(item, "secondary");
            entries.push_back(std::move(e));
        }

        catalog::IngredientQuery query;
        query.text = textOf(request, "text");
        query.groups = textsOf(request, "groups");
        query.types = textsOf(request, "types");
        query.forms = textsOf(request, "forms");
        const std::string source = textOf(request, "source");
        query.source = source == "builtIn" ? catalog::IngredientSource::builtIn
                       : source == "custom" ? catalog::IngredientSource::custom
                                            : catalog::IngredientSource::any;
        query.minPrimary = numberOf(request, "minPrimary");
        query.maxPrimary = numberOf(request, "maxPrimary");
        const std::string sort = textOf(request, "sort");
        query.sort = sort == "name"        ? catalog::IngredientSort::name
                     : sort == "group"     ? catalog::IngredientSort::group
                     : sort == "primary"   ? catalog::IngredientSort::primary
                     : sort == "secondary" ? catalog::IngredientSort::secondary
                                           : catalog::IngredientSort::catalog;
        const Value* descending = request.find("descending");
        query.descending = descending != nullptr && descending->isBool() && descending->asBool();

        Array indices;
        for (const std::size_t index : catalog::queryIngredients(entries, query)) {
            indices.push_back(number(std::to_string(index)));
        }
        const catalog::IngredientFacets facets = catalog::ingredientFacets(entries);
        Object facetsJson;
        facetsJson.push_back(Member{"groups", textsJson(facets.groups)});
        facetsJson.push_back(Member{"types", textsJson(facets.types)});
        facetsJson.push_back(Member{"forms", textsJson(facets.forms)});
        facetsJson.push_back(Member{"primaryMin", optionalFinite(facets.primaryMin)});
        facetsJson.push_back(Member{"primaryMax", optionalFinite(facets.primaryMax)});
        const catalog::IngredientMetrics metrics = catalog::ingredientMetrics(textOf(request, "kind"));
        Object metricsJson;
        metricsJson.push_back(Member{"primary", metricJson(metrics.primary)});
        metricsJson.push_back(Member{"secondary", metricJson(metrics.secondary)});

        Object root;
        root.push_back(Member{"indices", Value::array(std::move(indices))});
        root.push_back(Member{"facets", Value::object(std::move(facetsJson))});
        root.push_back(Member{"metrics", Value::object(std::move(metricsJson))});
        return copyOut(kettlelogic::json::serialize(Value::object(std::move(root)), -1));
    } catch (...) {
        return nullptr;
    }
}

char* kl_style_comparison_json(const kl_document* document, kl_recipe_id recipe, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Recipe& found = recipeIn(document->document, recipe);
            if (!found.bjcpStyle) {
                result = copyOut("null");
                return;
            }
            const std::optional<kettlelogic::calc::StyleRef> style =
                kettlelogic::calc::findStyle(document->document, *found.bjcpStyle);
            if (!style) {
                throw std::invalid_argument("no style with id \"" + *found.bjcpStyle + "\"");
            }
            Array checks;
            for (const kettlelogic::calc::Check& check :
                 kettlelogic::calc::compareToStyle(kettlelogic::calc::calculate(found), style->ranges)) {
                Object o;
                o.push_back(Member{"metric", Value::string(metricName(check.metric))});
                o.push_back(Member{"value", optionalFinite(check.value)});
                o.push_back(Member{"low", finite(check.range.low)});
                o.push_back(Member{"high", finite(check.range.high)});
                o.push_back(Member{"fit", Value::string(fitName(check.fit))});
                checks.push_back(Value::object(std::move(o)));
            }
            Object styleJson;
            styleJson.push_back(Member{"id", Value::string(style->id)});
            styleJson.push_back(Member{"name", Value::string(style->name)});
            Object root;
            root.push_back(Member{"style", Value::object(std::move(styleJson))});
            root.push_back(Member{"checks", Value::array(std::move(checks))});
            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(root)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

}  // extern "C"

namespace {

// Reads the inputs the planning calls take, from the JSON object the app sends.
const Value& inputObject(const Value& root) {
    if (!root.isObject()) {
        throw std::invalid_argument("the inputs must be a JSON object");
    }
    return root;
}

std::optional<double> optionalInput(const Value& object, const char* key) {
    const Value* value = object.find(key);
    if (value == nullptr || value->isNull()) {
        return std::nullopt;
    }
    if (!value->isNumber()) {
        throw std::invalid_argument(std::string("\"") + key + "\" must be a number");
    }
    return value->asRational().toDouble();
}

double numberInput(const Value& object, const char* key, double fallback) {
    return optionalInput(object, key).value_or(fallback);
}

bool boolInput(const Value& object, const char* key, bool fallback) {
    const Value* value = object.find(key);
    return value != nullptr && value->isBool() ? value->asBool() : fallback;
}

std::string stringInput(const Value& object, const char* key, const std::string& fallback) {
    const Value* value = object.find(key);
    return value != nullptr && value->isString() ? value->asString() : fallback;
}

kettlelogic::calc::Ions ionsInput(const Value& object) {
    kettlelogic::calc::Ions ions;
    ions.calcium = numberInput(object, "calcium", 0);
    ions.magnesium = numberInput(object, "magnesium", 0);
    ions.sodium = numberInput(object, "sodium", 0);
    ions.sulfate = numberInput(object, "sulfate", 0);
    ions.chloride = numberInput(object, "chloride", 0);
    ions.bicarbonate = numberInput(object, "bicarbonate", 0);
    ions.carbonate = numberInput(object, "carbonate", 0);
    return ions;
}

Value ionsJson(const kettlelogic::calc::Ions& ions) {
    Object o;
    o.push_back(Member{"calcium", finite(ions.calcium)});
    o.push_back(Member{"magnesium", finite(ions.magnesium)});
    o.push_back(Member{"sodium", finite(ions.sodium)});
    o.push_back(Member{"sulfate", finite(ions.sulfate)});
    o.push_back(Member{"chloride", finite(ions.chloride)});
    o.push_back(Member{"bicarbonate", finite(ions.bicarbonate)});
    o.push_back(Member{"carbonate", finite(ions.carbonate)});
    return Value::object(std::move(o));
}

Value analysisJson(const kettlelogic::calc::WaterAnalysis& a) {
    Object o;
    o.push_back(Member{"ions", ionsJson(a.ions)});
    o.push_back(Member{"alkalinity", finite(a.alkalinity)});
    o.push_back(Member{"effectiveHardness", finite(a.effectiveHardness)});
    o.push_back(Member{"residualAlkalinity", finite(a.residualAlkalinity)});
    o.push_back(Member{"sulfateToChloride", optionalFinite(a.sulfateToChloride)});
    o.push_back(Member{"balance", Value::string(a.balance)});
    return Value::object(std::move(o));
}

}  // namespace

extern "C" {

char* kl_brewing_plan_json(const kl_document* document, kl_recipe_id recipe, const char* inputs_json, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Recipe& found = recipeIn(document->document, recipe);
            const Value input = inputObject(kettlelogic::json::parse(inputs_json));
            kettlelogic::calc::BrewPlanInputs in;
            if (const Value* water = input.find("baseWater"); water != nullptr && water->isObject()) {
                in.baseWater = ionsInput(*water);
            }
            in.mashRatioLPerKg = numberInput(input, "mashRatioLPerKg", in.mashRatioLPerKg);
            in.absorptionLPerKg = numberInput(input, "absorptionLPerKg", in.absorptionLPerKg);
            in.grainTempC = numberInput(input, "grainTempC", in.grainTempC);
            in.tunTempC = numberInput(input, "tunTempC", in.tunTempC);
            in.mashTempC = numberInput(input, "mashTempC", in.mashTempC);
            in.spargeTempC = numberInput(input, "spargeTempC", in.spargeTempC);
            in.deadspaceLiters = numberInput(input, "deadspaceLiters", in.deadspaceLiters);
            in.adjustForDeadspace = boolInput(input, "adjustForDeadspace", in.adjustForDeadspace);
            in.leaveOutSalts = boolInput(input, "leaveOutSalts", in.leaveOutSalts);
            in.holdSpargeSaltsUntilBoil = boolInput(input, "holdSpargeSaltsUntilBoil", in.holdSpargeSaltsUntilBoil);
            in.acidForTarget = stringInput(input, "acidForTarget", in.acidForTarget);
            in.targetMashPh = numberInput(input, "targetMashPh", in.targetMashPh);
            in.spargeAcid = stringInput(input, "spargeAcid", in.spargeAcid);
            in.spargeAcidStrengthPercent = numberInput(input, "spargeAcidStrengthPercent", in.spargeAcidStrengthPercent);
            in.spargeStartPh = numberInput(input, "spargeStartPh", in.spargeStartPh);
            in.spargeTargetPh = numberInput(input, "spargeTargetPh", in.spargeTargetPh);
            in.roPercent = numberInput(input, "roPercent", in.roPercent);
            if (const Value* ro = input.find("roWater"); ro != nullptr && ro->isObject()) {
                in.roWater = ionsInput(*ro);
            }
            if (const Value* target = input.find("targetWater"); target != nullptr && target->isObject()) {
                in.targetWater = ionsInput(*target);
            }
            if (const Value* salts = input.find("salts"); salts != nullptr && salts->isArray()) {
                for (const Value& item : salts->asArray()) {
                    in.salts.push_back(kettlelogic::calc::WaterSalt{stringInput(item, "kind", ""),
                                                                    numberInput(item, "grams", 0),
                                                                    boolInput(item, "sparge", false)});
                }
            }
            if (const Value* acids = input.find("acids"); acids != nullptr && acids->isArray()) {
                for (const Value& item : acids->asArray()) {
                    in.acids.push_back(kettlelogic::calc::AcidAddition{stringInput(item, "kind", ""),
                                                                       numberInput(item, "grams", 0)});
                }
            }

            const auto plan = kettlelogic::calc::planBrewing(found, in);
            Object o;
            o.push_back(Member{"grainKg", finite(plan.grainKg)});
            o.push_back(Member{"strikeLiters", finite(plan.strikeLiters)});
            o.push_back(Member{"absorptionLiters", finite(plan.absorptionLiters)});
            o.push_back(Member{"firstRunningsLiters", finite(plan.firstRunningsLiters)});
            o.push_back(Member{"spargeLiters", optionalFinite(plan.spargeLiters)});
            o.push_back(Member{"mashVolumeLiters", finite(plan.mashVolumeLiters)});
            o.push_back(Member{"mashTunVolumeLiters", finite(plan.mashTunVolumeLiters)});
            o.push_back(Member{"strikeTempC", optionalFinite(plan.strikeTempC)});
            o.push_back(Member{"mashWater", analysisJson(plan.mashWater)});
            o.push_back(Member{"spargeWater", analysisJson(plan.spargeWater)});
            o.push_back(Member{"acidMilliequivalents", finite(plan.acidMilliequivalents)});
            o.push_back(Member{"alkalinityAfterAcid", finite(plan.alkalinityAfterAcid)});
            o.push_back(Member{"acidGramsForTarget", optionalFinite(plan.acidGramsForTarget)});
            o.push_back(Member{"maltAcidityMilliequivalents", finite(plan.maltAcidityMilliequivalents)});
            o.push_back(Member{"quartsPerPound", finite(plan.quartsPerPound)});
            o.push_back(Member{"estimatedMashPhBeforeAcid", finite(plan.estimatedMashPhBeforeAcid)});
            o.push_back(Member{"estimatedMashPh", finite(plan.estimatedMashPh)});
            o.push_back(Member{"spargeAcidGrams", optionalFinite(plan.spargeAcidGrams)});
            o.push_back(Member{"mashDifference", plan.mashDifference ? ionsJson(*plan.mashDifference) : Value{}});
            o.push_back(Member{"spargeAcidMilliliters", optionalFinite(plan.spargeAcidMilliliters)});
            o.push_back(Member{"colorLowSrm", optionalFinite(plan.colorLowSrm)});
            o.push_back(Member{"colorHighSrm", optionalFinite(plan.colorHighSrm)});
            o.push_back(Member{"notes", stringArray(plan.notes)});
            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(o)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

char* kl_journal_plan_json(const kl_document* document, kl_recipe_id recipe, const char* inputs_json, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Recipe& found = recipeIn(document->document, recipe);
            const Value input = inputObject(kettlelogic::json::parse(inputs_json));
            kettlelogic::calc::JournalInputs in;
            in.postMashGravity = optionalInput(input, "postMashGravity");
            in.preBoilLiters = optionalInput(input, "preBoilLiters");
            in.preBoilGravity = optionalInput(input, "preBoilGravity");
            in.batchLiters = optionalInput(input, "batchLiters");
            in.originalGravity = optionalInput(input, "originalGravity");
            in.finalGravity = optionalInput(input, "finalGravity");
            in.bottlingLiters = optionalInput(input, "bottlingLiters");
            in.carbonationVolumes = optionalInput(input, "carbonationVolumes");
            in.carbonationTempC = optionalInput(input, "carbonationTempC");

            const auto r = kettlelogic::calc::planJournal(found, in);
            Object o;
            o.push_back(Member{"expectedBrewhouseEfficiency", optionalFinite(r.expectedBrewhouseEfficiency)});
            o.push_back(Member{"expectedPreBoilGravity", optionalFinite(r.expectedPreBoilGravity)});
            o.push_back(Member{"expectedOriginalGravity", optionalFinite(r.expectedOriginalGravity)});
            o.push_back(Member{"expectedFinalGravity", optionalFinite(r.expectedFinalGravity)});
            o.push_back(Member{"expectedAbv", optionalFinite(r.expectedAbv)});
            o.push_back(Member{"expectedAttenuation", optionalFinite(r.expectedAttenuation)});
            o.push_back(Member{"expectedCaloriesPer12Oz", optionalFinite(r.expectedCaloriesPer12Oz)});
            o.push_back(Member{"measuredMashEfficiency", optionalFinite(r.measuredMashEfficiency)});
            o.push_back(Member{"measuredBrewhouseEfficiency", optionalFinite(r.measuredBrewhouseEfficiency)});
            o.push_back(Member{"measuredAbv", optionalFinite(r.measuredAbv)});
            o.push_back(Member{"forceCarbonationPsi", optionalFinite(r.forceCarbonationPsi)});
            o.push_back(Member{"measuredAttenuation", optionalFinite(r.measuredAttenuation)});
            o.push_back(Member{"measuredCaloriesPer12Oz", optionalFinite(r.measuredCaloriesPer12Oz)});
            o.push_back(Member{"primingSugarGramsPerLiter", optionalFinite(r.primingSugarGramsPerLiter)});
            o.push_back(Member{"primingSugarGrams", optionalFinite(r.primingSugarGrams)});
            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(o)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

int kl_set_recipe_planning(kl_document* document, kl_recipe_id recipe, const char* planning, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::setPlanning(recipe, planning));
    });
}

int kl_set_price(kl_document* document, kl_recipe_id recipe, const char* kind, kl_ingredient_id ingredient,
                 const char* amount, const char* per_unit, char** error) {
    return guarded(error, [&] {
        using kettlelogic::model::IngredientKind;
        const std::string_view name = kind;
        IngredientKind which;
        if (name == "fermentable") {
            which = IngredientKind::Fermentable;
        } else if (name == "hop") {
            which = IngredientKind::Hop;
        } else if (name == "culture") {
            which = IngredientKind::Culture;
        } else if (name == "miscellaneous") {
            which = IngredientKind::Miscellaneous;
        } else {
            throw std::invalid_argument("\"" + std::string(name) + "\" is not an ingredient kind that has a price");
        }
        std::optional<kettlelogic::model::Price> price;
        if (amount != nullptr) {
            if (per_unit == nullptr) {
                throw std::invalid_argument("a price needs a unit");
            }
            const auto per = kettlelogic::unitFromBeerJson(per_unit);
            if (!per) {
                throw std::invalid_argument(std::string("unknown unit \"") + per_unit + "\"");
            }
            price = kettlelogic::model::Price{Rational::fromDecimal(amount), *per};
        }
        document->stack.execute(document->document,
                                kettlelogic::model::setPrice(recipe, which, ingredient, std::move(price)));
    });
}

int kl_set_recipe_currency(kl_document* document, kl_recipe_id recipe, const char* currency, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::setCurrency(recipe, currency));
    });
}

// extern "C++" because MSVC assumes a function with C linkage does not throw, and this one does.
extern "C++" {
namespace {

// A number the user typed, as exact decimal text. Throws if it is not a number.
double typedNumber(const char* text, const char* what) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') {
        throw std::invalid_argument(std::string(what) + " must be a number");
    }
    return value;
}

}  // namespace
}  // extern "C++"

char* kl_starter_plan_json(const kl_document* document, kl_recipe_id recipe, int lager,
                           const char* pack_cells_billion, const char* growth_billion_per_liter_plato,
                           const char* starter_liters, const char* starter_gravity, int two_stage,
                           const char* first_stage_liters, int add_to_bottling, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Recipe& found = recipeIn(document->document, recipe);
            const auto results = kettlelogic::calc::calculate(found);
            if (!results.originalGravity) {
                throw std::invalid_argument("the recipe has no original gravity yet, so its yeast cannot be planned");
            }
            kettlelogic::calc::StarterInputs inputs;
            inputs.lager = lager != 0;
            inputs.packCellsBillion = typedNumber(pack_cells_billion, "the pack size");
            inputs.growthBillionPerLiterPlato = typedNumber(growth_billion_per_liter_plato, "the growth");
            inputs.starterLiters = typedNumber(starter_liters, "the starter volume");
            inputs.starterGravity = typedNumber(starter_gravity, "the starter gravity");
            inputs.twoStage = two_stage != 0;
            inputs.firstStageLiters = typedNumber(first_stage_liters, "the first stage");
            inputs.addToBottling = add_to_bottling != 0;

            const auto plan = kettlelogic::calc::planStarter(
                found.batchSize.valueIn(Unit::Liter).toDouble(), *results.originalGravity, inputs);

            Object starter;
            starter.push_back(Member{"liters", finite(plan.starterLiters)});
            starter.push_back(Member{"gravity", finite(plan.starterGravity)});
            starter.push_back(Member{"dryMaltOunces", finite(plan.dryMaltOunces)});
            starter.push_back(Member{"dryMaltGrams", finite(plan.dryMaltGrams)});
            starter.push_back(Member{"cellsWithStarterBillion", finite(plan.cellsWithStarterBillion)});
            starter.push_back(Member{"addToBottlingLiters", finite(plan.addToBottlingLiters)});

            Object twoStage;
            twoStage.push_back(Member{"firstLiters", finite(plan.twoStageFirstLiters)});
            twoStage.push_back(Member{"secondLiters", finite(plan.twoStageSecondLiters)});
            twoStage.push_back(Member{"use", Value::boolean(inputs.twoStage)});

            Object root;
            root.push_back(Member{"targetGravity", finite(plan.targetGravity)});
            root.push_back(Member{"targetPlato", finite(plan.targetPlato)});
            root.push_back(Member{"batchLiters", finite(plan.batchLiters)});
            root.push_back(Member{"pitchRate", finite(plan.pitchRate)});
            root.push_back(Member{"cellsNeededBillion", finite(plan.cellsNeededBillion)});
            root.push_back(Member{"packsWithoutStarter", number(std::to_string(plan.packsWithoutStarter))});
            root.push_back(Member{"cellsWithoutStarterBillion", finite(plan.cellsWithoutStarterBillion)});
            root.push_back(Member{"starter", Value::object(std::move(starter))});
            root.push_back(Member{"oneStageLiters", finite(plan.oneStageLiters)});
            root.push_back(Member{"twoStage", Value::object(std::move(twoStage))});
            root.push_back(Member{"notes", stringArray(plan.notes)});
            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(root)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

char* kl_procedure_json(const kl_document* document, kl_recipe_id recipe, int boil_minutes, const char* system,
                        char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Recipe& found = recipeIn(document->document, recipe);
            const auto steps = kettlelogic::calc::brewDayProcedure(
                found, boil_minutes, kettlelogic::calc::mashSystemFromName(system));
            Array out;
            for (const auto& step : steps) {
                Object o;
                o.push_back(Member{"phase", Value::string(step.phase)});
                o.push_back(Member{"minute", step.minute ? number(std::to_string(*step.minute)) : Value{}});
                o.push_back(Member{"text", Value::string(step.text)});
                o.push_back(Member{"warning", Value::boolean(step.warning)});
                o.push_back(Member{"timerMinutes", step.timerMinutes ? number(std::to_string(*step.timerMinutes)) : Value{}});
                out.push_back(Value::object(std::move(o)));
            }
            result = copyOut(kettlelogic::json::serialize(Value::array(std::move(out)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

int kl_remove_recipe(kl_document* document, kl_recipe_id recipe, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::removeRecipe(recipe));
    });
}

int kl_trash_recipe(kl_document* document, kl_recipe_id recipe, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::trashRecipe(recipe));
    });
}

int kl_duplicate_recipe(kl_document* document, kl_recipe_id recipe, kl_recipe_id* created, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::duplicateRecipe(recipe));
        if (created != nullptr) {
            // The copy is placed right after the original.
            const auto& recipes = document->document.recipes;
            const auto it = std::find_if(recipes.begin(), recipes.end(),
                                         [recipe](const Recipe& r) { return r.id == recipe; });
            *created = (it + 1)->id;
        }
    });
}

int kl_duplicate_fermentable(kl_document* document, kl_recipe_id recipe, kl_ingredient_id fermentable, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::duplicateFermentable(recipe, fermentable));
    });
}

int kl_duplicate_hop(kl_document* document, kl_recipe_id recipe, kl_ingredient_id hop, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::duplicateHop(recipe, hop));
    });
}

int kl_duplicate_culture(kl_document* document, kl_recipe_id recipe, kl_ingredient_id culture, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::duplicateCulture(recipe, culture));
    });
}

int kl_duplicate_water(kl_document* document, kl_recipe_id recipe, kl_ingredient_id water, char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document, kettlelogic::model::duplicateWater(recipe, water));
    });
}

int kl_duplicate_miscellaneous(kl_document* document, kl_recipe_id recipe, kl_ingredient_id miscellaneous,
                               char** error) {
    return guarded(error, [&] {
        document->stack.execute(document->document,
                                kettlelogic::model::duplicateMiscellaneous(recipe, miscellaneous));
    });
}

int kl_set_fermentable(kl_document* document, kl_recipe_id recipe, kl_ingredient_id fermentable, const char* name,
                       const char* type, const char* amount, const char* amount_unit, const char* yield,
                       const char* yield_unit, const char* color, const char* color_unit, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Fermentable item =
            itemIn(&recipeIn(document->document, recipe).fermentables, fermentable, "fermentable");
        item.name = name;
        item.type = type;
        item.amount = quantityFrom(amount, amount_unit);
        item.yield.potential = measureFrom(yield, yield_unit);
        item.color = measureFrom(color, color_unit);
        document->stack.execute(document->document, kettlelogic::model::setFermentable(recipe, std::move(item)));
    });
}

int kl_set_hop(kl_document* document, kl_recipe_id recipe, kl_ingredient_id hop, const char* name,
               const char* alpha_percent, const char* beta_percent, const char* amount, const char* amount_unit, const char* form,
               const char* use, const char* time, const char* time_unit, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Hop item = itemIn(listIn(recipeIn(document->document, recipe).hops), hop, "hop");
        item.name = name;
        item.alphaAcid = quantityFrom(alpha_percent, "%");
        item.betaAcid = optionalQuantity(beta_percent, "%");
        item.amount = quantityFrom(amount, amount_unit);
        item.form = optionalText(form);
        item.timing.use = optionalText(use);
        if (time != nullptr && time_unit == nullptr) {
            throw std::invalid_argument("a hop time needs a unit");
        }
        item.timing.time = time != nullptr ? optionalQuantity(time, time_unit) : std::nullopt;
        document->stack.execute(document->document, kettlelogic::model::setHop(recipe, std::move(item)));
    });
}

int kl_set_culture(kl_document* document, kl_recipe_id recipe, kl_ingredient_id culture, const char* name,
                   const char* type, const char* form, const char* amount, const char* amount_unit,
                   const char* attenuation_percent, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Culture item =
            itemIn(listIn(recipeIn(document->document, recipe).cultures), culture, "culture");
        item.name = name;
        item.type = type;
        item.form = form;
        item.amount = quantityFrom(amount, amount_unit);
        item.attenuation = optionalQuantity(attenuation_percent, "%");
        document->stack.execute(document->document, kettlelogic::model::setCulture(recipe, std::move(item)));
    });
}

int kl_set_water(kl_document* document, kl_recipe_id recipe, kl_ingredient_id water, const char* name,
                 const char* calcium, const char* bicarbonate, const char* sulfate, const char* chloride,
                 const char* sodium, const char* magnesium, const char* amount_liters, char** error) {
    return guarded(error, [&] {
        kettlelogic::model::WaterAddition item =
            itemIn(listIn(recipeIn(document->document, recipe).waters), water, "water");
        item.water.name = name;
        item.water.calcium = quantityFrom(calcium, "mg/l");
        item.water.bicarbonate = quantityFrom(bicarbonate, "mg/l");
        item.water.sulfate = quantityFrom(sulfate, "mg/l");
        item.water.chloride = quantityFrom(chloride, "mg/l");
        item.water.sodium = quantityFrom(sodium, "mg/l");
        item.water.magnesium = quantityFrom(magnesium, "mg/l");
        item.amount = optionalQuantity(amount_liters, "l");
        document->stack.execute(document->document, kettlelogic::model::setWater(recipe, std::move(item)));
    });
}

int kl_set_miscellaneous(kl_document* document, kl_recipe_id recipe, kl_ingredient_id miscellaneous,
                         const char* name, const char* type, const char* amount, const char* amount_unit,
                         char** error) {
    return guarded(error, [&] {
        kettlelogic::model::Miscellaneous item =
            itemIn(listIn(recipeIn(document->document, recipe).miscellaneous), miscellaneous, "miscellaneous ingredient");
        item.name = name;
        item.type = type;
        item.amount = quantityFrom(amount, amount_unit);
        document->stack.execute(document->document,
                                kettlelogic::model::setMiscellaneous(recipe, std::move(item)));
    });
}

int kl_group_begin(kl_document* document, char** error) {
    return guarded(error, [&] {
        if (document->groupDepth) {
            throw std::logic_error("an edit group is already open");
        }
        document->groupDepth = document->stack.undoDepth();
        document->stack.seal();
    });
}

int kl_group_end(kl_document* document, const char* label, char** error) {
    return guarded(error, [&] {
        if (!document->groupDepth) {
            throw std::logic_error("no edit group is open");
        }
        const std::size_t depth = *document->groupDepth;
        document->groupDepth.reset();
        document->stack.groupSince(depth, label);
    });
}

int kl_group_cancel(kl_document* document, char** error) {
    return guarded(error, [&] {
        if (!document->groupDepth) {
            throw std::logic_error("no edit group is open");
        }
        const std::size_t depth = *document->groupDepth;
        document->groupDepth.reset();
        document->stack.rollbackTo(document->document, depth);
    });
}

int kl_undo(kl_document* document, char** error) {
    int done = 0;
    if (guarded(error, [&] { done = document->stack.undo(document->document) ? 1 : 0; }) != 0) {
        return -1;
    }
    return done;
}

int kl_redo(kl_document* document, char** error) {
    int done = 0;
    if (guarded(error, [&] { done = document->stack.redo(document->document) ? 1 : 0; }) != 0) {
        return -1;
    }
    return done;
}

int kl_can_undo(const kl_document* document) {
    return document->stack.canUndo() ? 1 : 0;
}

int kl_can_redo(const kl_document* document) {
    return document->stack.canRedo() ? 1 : 0;
}

char* kl_undo_label(const kl_document* document) {
    return copyOut(document->stack.undoLabel());
}

char* kl_redo_label(const kl_document* document) {
    return copyOut(document->stack.redoLabel());
}

char* kl_tool_json(const char* tool, const char* inputs_json, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            kettlelogic::calc::tools::Inputs inputs;
            const Value parsed = kettlelogic::json::parse(inputs_json != nullptr ? inputs_json : "{}");
            if (!parsed.isObject()) {
                throw std::invalid_argument("the inputs must be a JSON object");
            }
            for (const Member& member : parsed.asObject()) {
                if (!member.value.isString()) {
                    throw std::invalid_argument("the input \"" + member.key + "\" must be text");
                }
                inputs[member.key] = member.value.asString();
            }

            const auto output = kettlelogic::calc::tools::run(tool != nullptr ? tool : "", inputs);
            Object results;
            for (const auto& [name, value] : output.results) {
                results.push_back(Member{name, finite(value)});
            }
            Object root;
            root.push_back(Member{"results", Value::object(std::move(results))});
            root.push_back(Member{"notes", stringArray(output.notes)});
            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(root)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

char* kl_recipe_export(const kl_document* document, kl_recipe_id recipe, const char* format, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Recipe& found = recipeIn(document->document, recipe);
            const std::string kind = format != nullptr ? format : "";
            if (kind == "beerxml") {
                result = copyOut(kettlelogic::io::exportBeerXml(found, kettlelogic::calc::calculate(found)));
            } else if (kind == "text") {
                result = copyOut(kettlelogic::io::exportText(found, kettlelogic::calc::calculate(found)));
            } else if (kind == "beerjson") {
                result = copyOut(kettlelogic::io::exportBeerJson(found));
            } else {
                throw std::invalid_argument("unknown export format \"" + kind + "\"");
            }
        }) != 0) {
        return nullptr;
    }
    return result;
}

int kl_recipe_write_pdf(const kl_document* document, kl_recipe_id recipe, const char* path, char** error) {
    return guarded(error, [&] {
        const Recipe& found = recipeIn(document->document, recipe);
        kettlelogic::io::writeRecipePdf(document->document, found, kettlelogic::calc::calculate(found), path);
    });
}

char* kl_beerxml_to_beerjson(const char* beerxml, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Document document = kettlelogic::io::importBeerXml(beerxml != nullptr ? beerxml : "");
            Object root = kettlelogic::json::parse(kettlelogic::model::writeBeerJson(document)).asObject();
            Array warnings;
            for (const kettlelogic::model::Diagnostic& diagnostic : document.diagnostics) {
                warnings.push_back(Value::object({Member{"pointer", Value::string(diagnostic.pointer)},
                                                  Member{"message", Value::string(diagnostic.message)}}));
            }
            root.push_back(Member{"warnings", Value::array(std::move(warnings))});
            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(root)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

namespace {

kettlelogic::calc::SheetUnits sheetUnits(int imperial) {
    return imperial != 0 ? kettlelogic::calc::SheetUnits::Imperial : kettlelogic::calc::SheetUnits::Metric;
}

const char* kindName(kettlelogic::calc::BoxKind kind) {
    using kettlelogic::calc::BoxKind;
    switch (kind) {
        case BoxKind::Date: return "date";
        case BoxKind::Volume: return "volume";
        case BoxKind::Temperature: return "temperature";
        case BoxKind::Gravity: return "gravity";
        case BoxKind::Ph: return "ph";
    }
    return "";
}

}  // namespace

char* kl_brew_sheet_json(const kl_document* document, kl_recipe_id recipe, int imperial, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Recipe& found = recipeIn(document->document, recipe);
            const auto sheet = kettlelogic::calc::brewSheet(found, kettlelogic::calc::calculate(found), sheetUnits(imperial));
            Array sections;
            for (const auto& section : sheet.sections) {
                Array boxes;
                for (const auto& b : section.boxes) {
                    Object o;
                    o.push_back(Member{"code", Value::string(b.code)});
                    o.push_back(Member{"label", Value::string(b.label)});
                    o.push_back(Member{"kind", Value::string(kindName(b.kind))});
                    o.push_back(Member{"unit", Value::string(b.unit)});
                    o.push_back(Member{"expected", optionalFinite(b.expected)});
                    o.push_back(Member{"expectedText", Value::string(b.expectedText)});
                    boxes.push_back(Value::object(std::move(o)));
                }
                Object s;
                s.push_back(Member{"code", Value::string(section.code)});
                s.push_back(Member{"title", Value::string(section.title)});
                s.push_back(Member{"boxes", Value::array(std::move(boxes))});
                sections.push_back(Value::object(std::move(s)));
            }
            Object o;
            o.push_back(Member{"recipeName", Value::string(sheet.recipeName)});
            o.push_back(Member{"batchText", Value::string(sheet.batchText)});
            o.push_back(Member{"units", Value::string(imperial != 0 ? "imperial" : "metric")});
            o.push_back(Member{"sections", Value::array(std::move(sections))});
            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(o)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

int kl_brew_sheet_write_pdf(const kl_document* document, kl_recipe_id recipe, int imperial, const char* path,
                            char** error) {
    return guarded(error, [&] {
        const Recipe& found = recipeIn(document->document, recipe);
        const auto sheet = kettlelogic::calc::brewSheet(found, kettlelogic::calc::calculate(found), sheetUnits(imperial));
        kettlelogic::io::writeBrewSheetPdf(sheet, path);
    });
}

char* kl_brew_sheet_read_json(const kl_document* document, kl_recipe_id recipe, int imperial, const char* entries_json,
                              char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            const Recipe& found = recipeIn(document->document, recipe);
            const auto sheet = kettlelogic::calc::brewSheet(found, kettlelogic::calc::calculate(found), sheetUnits(imperial));
            const Value entries = inputObject(kettlelogic::json::parse(entries_json));
            std::map<std::string, std::string> typed;
            for (const Member& m : entries.asObject()) {
                if (!m.value.isString()) {
                    throw std::invalid_argument("the text in box " + m.key + " must be a string");
                }
                typed[m.key] = m.value.asString();
            }
            const auto values = kettlelogic::calc::readBrewSheet(sheet, typed);

            Object numbers;
            for (const auto& [code, value] : values.numbers) {
                numbers.push_back(Member{code, finite(value)});
            }
            Object dates;
            for (const auto& [code, text] : values.dates) {
                dates.push_back(Member{code, Value::string(text)});
            }
            Array issues;
            for (const auto& issue : values.issues) {
                Object o;
                o.push_back(Member{"code", Value::string(issue.code)});
                o.push_back(Member{"message", Value::string(issue.message)});
                issues.push_back(Value::object(std::move(o)));
            }
            Object o;
            o.push_back(Member{"numbers", Value::object(std::move(numbers))});
            o.push_back(Member{"dates", Value::object(std::move(dates))});
            o.push_back(Member{"issues", Value::array(std::move(issues))});
            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(o)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

// The brew water calculations, as JSON. Each part is optional: "water" (the report, in mg/L), "conversion"
// (an alkalinity and pH to bicarbonate and carbonate), "mash", and "sparge". See kettlelogic.h.
char* kl_brew_water_json(const char* inputs_json, char** error) {
    char* result = nullptr;
    if (guarded(error, [&] {
            namespace brew_water = kettlelogic::calc::brew_water;
            const Value input = inputObject(kettlelogic::json::parse(inputs_json));
            const Value noObject = Value::object(Object{});
            const Value* waterValue = input.find("water");
            const Value& water = waterValue != nullptr && waterValue->isObject() ? *waterValue : noObject;
            const kettlelogic::calc::Ions ions = ionsInput(water);

            brew_water::OtherIons other;
            other.potassium = numberInput(water, "potassium", 0);
            other.iron = numberInput(water, "iron", 0);
            other.nitrate = numberInput(water, "nitrate", 0);
            other.nitrite = numberInput(water, "nitrite", 0);
            other.fluoride = numberInput(water, "fluoride", 0);
            const brew_water::ReportBalance balance = brew_water::balanceReport(ions, other);

            // The water as the report gives it. Its alkalinity in CaCO3.
            const double alkalinity = ions.bicarbonate * 50.0 / 61.0 + ions.carbonate * 50.0 / 30.0;
            Object report;
            report.push_back(Member{"cationsMeq", finite(balance.cationsMeq)});
            report.push_back(Member{"anionsMeq", finite(balance.anionsMeq)});
            report.push_back(Member{"balanced", Value::boolean(balance.balanced)});
            report.push_back(Member{"alkalinity", finite(alkalinity)});
            report.push_back(Member{"residualAlkalinity", finite(kettlelogic::calc::residualAlkalinity(ions))});
            Object root;
            root.push_back(Member{"report", Value::object(std::move(report))});

            if (const Value* conversion = input.find("conversion"); conversion != nullptr && conversion->isObject()) {
                const brew_water::Speciation species = brew_water::speciesFromAlkalinity(
                    numberInput(*conversion, "alkalinity", 0), numberInput(*conversion, "pH", 7));
                Object o;
                o.push_back(Member{"bicarbonate", finite(species.bicarbonate)});
                o.push_back(Member{"carbonate", finite(species.carbonate)});
                root.push_back(Member{"conversion", Value::object(std::move(o))});
            }

            if (const Value* mash = input.find("mash"); mash != nullptr && mash->isObject()) {
                std::vector<brew_water::Grain> grist;
                double grainKg = 0;
                if (const Value* grains = mash->find("grains"); grains != nullptr && grains->isArray()) {
                    for (const Value& item : grains->asArray()) {
                        brew_water::Grain grain;
                        grain.kilograms = numberInput(item, "kilograms", 0);
                        const double color = numberInput(item, "color", 0);
                        const std::string unit = stringInput(item, "colorUnit", "Lovi");
                        grain.lovibond = unit == "EBC" ? brew_water::lovibondFromEbc(color)
                                         : unit == "SRM" ? (color + 0.6) / 1.3
                                                         : color;
                        const std::string category = stringInput(item, "category", "base");
                        if (category == "base") grain.category = brew_water::MaltCategory::Base;
                        else if (category == "crystal") grain.category = brew_water::MaltCategory::Crystal;
                        else if (category == "roast") grain.category = brew_water::MaltCategory::Roast;
                        else if (category == "acid") grain.category = brew_water::MaltCategory::Acid;
                        else throw std::invalid_argument("the malt category \"" + category + "\" is not known");
                        grainKg += grain.kilograms;
                        grist.push_back(grain);
                    }
                }
                const double liters = numberInput(*mash, "waterLiters", 0);
                const double targetPh = numberInput(*mash, "targetPh", 5.4);
                if (!(grainKg > 0) || !(liters > 0)) {
                    throw std::invalid_argument("the mash needs grain and water to find its pH");
                }
                const double quartsPerPound = (liters / 0.946352946) / (grainKg / 0.45359237);
                const double acidity = brew_water::maltAcidityMilliequivalents(grist);
                const double raBefore = kettlelogic::calc::residualAlkalinity(ions);

                // The acids added, at the target pH, and the alkalinity they remove.
                double removed = 0;
                if (const Value* acids = mash->find("acids"); acids != nullptr && acids->isArray()) {
                    for (const Value& item : acids->asArray()) {
                        const std::string kind = stringInput(item, "kind", "");
                        const brew_water::Acid* acid = brew_water::findAcid(kind);
                        if (acid == nullptr) throw std::invalid_argument("\"" + kind + "\" is not an acid");
                        removed += brew_water::alkalinityRemovedByAcid(*acid, numberInput(item, "grams", 0),
                                                                 numberInput(item, "strength", 100), liters, targetPh);
                    }
                }
                const double raAfter = raBefore - removed;

                // The acid that brings the mash to the target pH, if the user names one.
                Object mashOut;
                mashOut.push_back(Member{"maltAcidityMeq", finite(acidity)});
                mashOut.push_back(Member{"quartsPerPound", finite(quartsPerPound)});
                mashOut.push_back(Member{"residualAlkalinityBefore", finite(raBefore)});
                mashOut.push_back(Member{"phBefore", finite(brew_water::mashPh(acidity, raBefore, quartsPerPound, liters))});
                mashOut.push_back(Member{"residualAlkalinityAfter", finite(raAfter)});
                mashOut.push_back(Member{"phAfter", finite(brew_water::mashPh(acidity, raAfter, quartsPerPound, liters))});
                mashOut.push_back(Member{"targetPh", finite(targetPh)});
                const double raTarget = brew_water::residualAlkalinityForMashPh(targetPh, acidity, quartsPerPound, liters);
                mashOut.push_back(Member{"residualAlkalinityForTarget", finite(raTarget)});
                if (const Value* target = mash->find("targetAcid"); target != nullptr && target->isObject()) {
                    const std::string kind = stringInput(*target, "kind", "");
                    const brew_water::Acid* acid = brew_water::findAcid(kind);
                    if (acid == nullptr) throw std::invalid_argument("\"" + kind + "\" is not an acid");
                    const double toRemove = std::max(0.0, raBefore - raTarget);
                    const double strength = numberInput(*target, "strength", 100);
                    mashOut.push_back(Member{"acidGramsForTarget", finite(brew_water::acidGramsToRemoveAlkalinity(
                                                                      *acid, toRemove, strength, liters, targetPh))});
                }
                root.push_back(Member{"mash", Value::object(std::move(mashOut))});
            }

            if (const Value* sparge = input.find("sparge"); sparge != nullptr && sparge->isObject()) {
                const std::string kind = stringInput(*sparge, "acid", "phosphoric acid");
                const brew_water::Acid* acid = brew_water::findAcid(kind);
                if (acid == nullptr) throw std::invalid_argument("\"" + kind + "\" is not an acid");
                const brew_water::SpargeAcid s = brew_water::spargeAcidification(
                    alkalinity, numberInput(*sparge, "startPh", 7), numberInput(*sparge, "targetPh", 5.6),
                    numberInput(*sparge, "liters", 0), *acid, numberInput(*sparge, "strength", 85));
                Object o;
                o.push_back(Member{"alkalinity", finite(alkalinity)});
                o.push_back(Member{"acidMmolPerLiter", finite(s.acidMmolPerLiter)});
                o.push_back(Member{"pureGrams", finite(s.pureGrams)});
                o.push_back(Member{"solutionGrams", optionalFinite(s.solutionGrams)});
                o.push_back(Member{"milliliters", optionalFinite(s.milliliters)});
                root.push_back(Member{"sparge", Value::object(std::move(o))});
            }

            result = copyOut(kettlelogic::json::serialize(Value::object(std::move(root)), -1));
        }) != 0) {
        return nullptr;
    }
    return result;
}

void kl_string_free(char* string) {
    std::free(string);
}

const char* kl_engine_version(void) {
    return kettlelogic::kEngineVersion;
}

int kl_engine_abi_version(void) {
    return KL_ENGINE_ABI_VERSION;
}

}  // extern "C"
