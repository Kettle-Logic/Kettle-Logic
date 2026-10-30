#include "kettlelogic/model/beerjson.hpp"
#include "kettlelogic/model/entities.hpp"
#include "kettlelogic/model/schema.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

namespace kettlelogic::model {

ReadError::ReadError(std::string pointer, const std::string& message)
    : std::runtime_error((pointer.empty() ? std::string("<document>") : pointer) + ": " + message),
      pointer_(std::move(pointer)) {}

namespace {

using json::Array;
using json::Member;
using json::Object;
using json::Value;

using Allowed = std::span<const std::string_view>;

[[noreturn]] void fail(const std::string& pointer, const std::string& message) {
    throw ReadError(pointer, message);
}

const Object& expectObject(const Value& v, const std::string& pointer) {
    if (!v.isObject()) {
        fail(pointer, "expected an object");
    }
    return v.asObject();
}

const Array& expectArray(const Value& v, const std::string& pointer) {
    if (!v.isArray()) {
        fail(pointer, "expected an array");
    }
    return v.asArray();
}

const std::string& expectString(const Value& v, const std::string& pointer) {
    if (!v.isString()) {
        fail(pointer, "expected a string");
    }
    return v.asString();
}

bool expectBool(const Value& v, const std::string& pointer) {
    if (!v.isBool()) {
        fail(pointer, "expected a boolean");
    }
    return v.asBool();
}

Rational expectNumber(const Value& v, const std::string& pointer) {
    if (!v.isNumber()) {
        fail(pointer, "expected a number");
    }
    return v.asRational();
}

Rational expectInteger(const Value& v, const std::string& pointer) {
    const Rational n = expectNumber(v, pointer);
    if (!n.isInteger()) {
        fail(pointer, "expected an integer");
    }
    return n;
}

const std::string& expectOneOf(const Value& v, const std::string& pointer, Allowed allowed) {
    const std::string& s = expectString(v, pointer);
    for (const std::string_view candidate : allowed) {
        if (s == candidate) {
            return s;
        }
    }
    fail(pointer, "\"" + s + "\" is not an allowed value");
}

// Reads one JSON object. Each typed property is taken with get() or require(),
// which marks it as used. finish() then classifies whatever is left over.
class Fields {
public:
    Fields(const Value& value, std::string pointer, Document& document)
        : members_(expectObject(value, pointer)),
          pointer_(std::move(pointer)),
          document_(document),
          used_(members_.size(), false) {}

    const std::string& pointer() const { return pointer_; }

    Document& document() const { return document_; }

    std::string child(std::string_view key) const {
        return jsonPointerChild(pointer_, key);
    }

    // This object's "name", if it has a string one. Used to fingerprint it.
    std::optional<std::string> parentName() const {
        for (const Member& m : members_) {
            if (m.key == "name" && m.value.isString()) {
                return m.value.asString();
            }
        }
        return std::nullopt;
    }

    const Value* get(std::string_view key) {
        for (std::size_t i = 0; i < members_.size(); ++i) {
            if (members_[i].key == key) {
                used_[i] = true;
                return &members_[i].value;
            }
        }
        return nullptr;
    }

    const Value& require(std::string_view key) {
        const Value* v = get(key);
        if (v == nullptr) {
            fail(pointer_, "missing required property \"" + std::string(key) + "\"");
        }
        return *v;
    }

    // Ends reading this object. Each unread member is classified against the
    // schema object:
    //  - a property the schema defines goes to `preserved` and is written back;
    //  - a property the schema does not define, in an object it closes, goes to
    //    the document's foreign list, with a warning, and is not written back;
    //  - in an open object, any property goes to `preserved`.
    void finish(Object& preserved, const schema::ObjectSchema& object) {
        for (std::size_t i = 0; i < members_.size(); ++i) {
            if (used_[i]) {
                continue;
            }
            const Member& member = members_[i];
            const bool defined = std::find(object.properties.begin(), object.properties.end(),
                                           std::string_view(member.key)) != object.properties.end();
            if (defined || !object.closed) {
                preserved.push_back(member);
                continue;
            }
            const std::string where = child(member.key);
            document_.foreign.push_back(ForeignProperty{pointer_, member.key, parentName(), member.value, Uuid{}, ""});
            document_.diagnostics.push_back(Diagnostic{
                where, "\"" + member.key + "\" is not defined by the BeerJSON schema; it is kept out of the saved file"});
        }
    }

private:
    const Object& members_;
    std::string pointer_;
    Document& document_;
    std::vector<bool> used_;
};

// Reads a JSON array of items into `out`, passing each item's JSON pointer.
template <typename T, typename Reader>
void readList(Fields& f, std::string_view key, std::vector<T>& out, Reader reader) {
    const Value* v = f.get(key);
    if (v == nullptr) {
        return;
    }
    const Array& items = expectArray(*v, f.child(key));
    for (std::size_t i = 0; i < items.size(); ++i) {
        out.push_back(reader(items[i], f.child(key) + "/" + std::to_string(i)));
    }
}

// An optional list keeps the difference between absent and empty: an empty
// array in the file reads as an empty list, not as no list.
template <typename T, typename Reader>
void readOptionalListOf(Fields& f, std::string_view key, std::optional<std::vector<T>>& out, Reader reader) {
    if (f.get(key) == nullptr) {
        return;
    }
    out.emplace();
    readList(f, key, *out, reader);
}

// ---- Quantities and measures ------------------------------------------------

Quantity readQuantity(const Value& v, const std::string& pointer,
                      std::initializer_list<Dimension> allowed, bool integer, Document& document) {
    Fields f(v, pointer, document);
    const Value& value = f.require("value");
    const Value& unit = f.require("unit");
    Object unknown;  // a closed object, so this stays empty
    f.finish(unknown, schema::kQuantity);

    const std::string unitPointer = f.child("unit");
    const std::string& name = expectString(unit, unitPointer);
    const std::optional<Unit> parsed = unitFromBeerJson(name);
    if (!parsed) {
        fail(unitPointer, "unknown unit \"" + name + "\"");
    }
    const Dimension dimension = dimensionOf(*parsed);
    if (std::find(allowed.begin(), allowed.end(), dimension) == allowed.end()) {
        fail(unitPointer, "unit \"" + name + "\" is not valid for this property");
    }

    const std::string valuePointer = f.child("value");
    const Rational amount = integer ? expectInteger(value, valuePointer)
                                    : expectNumber(value, valuePointer);
    return Quantity::fromUnit(amount, *parsed);
}

Measure readMeasure(const Value& v, const std::string& pointer, Document& document) {
    Fields f(v, pointer, document);
    const Value& value = f.require("value");
    const Value& unit = f.require("unit");
    Object unknown;  // a closed object, so this stays empty
    f.finish(unknown, schema::kMeasure);

    const std::string unitPointer = f.child("unit");
    const std::string& name = expectString(unit, unitPointer);
    if (!isOpaqueUnit(name)) {
        fail(unitPointer, "unit \"" + name + "\" is not valid for this property");
    }
    return Measure::fromUnit(expectNumber(value, f.child("value")), name);
}

// Optional and required wrappers around the readers above, keyed on a property.

std::optional<Quantity> optQuantity(Fields& f, std::string_view key,
                                    std::initializer_list<Dimension> allowed, bool integer = false) {
    const Value* v = f.get(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    return readQuantity(*v, f.child(key), allowed, integer, f.document());
}

Quantity reqQuantity(Fields& f, std::string_view key, std::initializer_list<Dimension> allowed,
                     bool integer = false) {
    const Value& v = f.require(key);
    return readQuantity(v, f.child(key), allowed, integer, f.document());
}

std::optional<Measure> optMeasure(Fields& f, std::string_view key) {
    const Value* v = f.get(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    return readMeasure(*v, f.child(key), f.document());
}

Measure reqMeasure(Fields& f, std::string_view key) {
    const Value& v = f.require(key);
    return readMeasure(v, f.child(key), f.document());
}

std::optional<std::string> optString(Fields& f, std::string_view key) {
    const Value* v = f.get(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    return expectString(*v, f.child(key));
}

std::string reqString(Fields& f, std::string_view key) {
    const Value& v = f.require(key);
    return expectString(v, f.child(key));
}

std::optional<std::string> optEnum(Fields& f, std::string_view key, Allowed allowed) {
    const Value* v = f.get(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    return expectOneOf(*v, f.child(key), allowed);
}

std::string reqEnum(Fields& f, std::string_view key, Allowed allowed) {
    const Value& v = f.require(key);
    return expectOneOf(v, f.child(key), allowed);
}

std::optional<bool> optBool(Fields& f, std::string_view key) {
    const Value* v = f.get(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    return expectBool(*v, f.child(key));
}

std::optional<long long> optInteger(Fields& f, std::string_view key) {
    const Value* v = f.get(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    const Rational n = expectInteger(*v, f.child(key));
    return static_cast<long long>(n.toDouble());
}

// ---- Shared parts ------------------------------------------------------------

constexpr std::initializer_list<Dimension> kMassOrVolume = {Dimension::Mass, Dimension::Volume};
constexpr std::initializer_list<Dimension> kVolume = {Dimension::Volume};
constexpr std::initializer_list<Dimension> kMassVolumeOrCount = {Dimension::Mass, Dimension::Volume,
                                                                 Dimension::Count};
constexpr std::initializer_list<Dimension> kTemperature = {Dimension::Temperature};
constexpr std::initializer_list<Dimension> kTime = {Dimension::Time};
constexpr std::initializer_list<Dimension> kPercent = {Dimension::Fraction};
constexpr std::initializer_list<Dimension> kConcentration = {Dimension::Concentration};

Timing readTiming(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    Timing t;
    t.time = optQuantity(f, "time", kTime, true);
    t.duration = optQuantity(f, "duration", kTime, true);
    t.continuous = optBool(f, "continuous");
    t.specificGravity = optMeasure(f, "specific_gravity");
    t.pH = optMeasure(f, "pH");
    t.step = optInteger(f, "step");
    t.use = optEnum(f, "use", schema::kTimingUses);
    f.finish(t.preserved, schema::kTiming);
    return t;
}

std::optional<Timing> optTiming(Fields& f, std::string_view key) {
    const Value* v = f.get(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    return readTiming(*v, f.child(key), f.document());
}

Yield readYield(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    Yield y;
    y.fineGrind = optQuantity(f, "fine_grind", kPercent);
    y.coarseGrind = optQuantity(f, "coarse_grind", kPercent);
    y.fineCoarseDifference = optQuantity(f, "fine_coarse_difference", kPercent);
    y.potential = optMeasure(f, "potential");
    f.finish(y.preserved, schema::kYield);
    if (y.empty()) {
        fail(pointer, "yield needs at least one property");
    }
    return y;
}

// ---- Ingredient additions ----------------------------------------------------

Fermentable readFermentable(const Value& v, const std::string& pointer, EntityId id, Document& doc) {
    Fields f(v, pointer, doc);
    Fermentable x;
    x.id = id;
    x.uuid = Uuid::generate();
    x.name = reqString(f, "name");
    x.type = reqEnum(f, "type", schema::kFermentableTypes);
    x.origin = optString(f, "origin");
    x.producer = optString(f, "producer");
    x.productId = optString(f, "product_id");
    x.grainGroup = optEnum(f, "grain_group", schema::kGrainGroups);
    x.yield = readYield(f.require("yield"), f.child("yield"), doc);
    x.color = reqMeasure(f, "color");
    x.amount = reqQuantity(f, "amount", kMassOrVolume);
    x.timing = optTiming(f, "timing");
    f.finish(x.preserved, schema::kFermentableAddition);
    return x;
}

Hop readHop(const Value& v, const std::string& pointer, EntityId id, Document& doc) {
    Fields f(v, pointer, doc);
    Hop h;
    h.id = id;
    h.uuid = Uuid::generate();
    h.name = reqString(f, "name");
    h.alphaAcid = reqQuantity(f, "alpha_acid", kPercent);
    h.betaAcid = optQuantity(f, "beta_acid", kPercent);
    h.producer = optString(f, "producer");
    h.productId = optString(f, "product_id");
    h.origin = optString(f, "origin");
    h.year = optString(f, "year");
    h.form = optEnum(f, "form", schema::kHopForms);
    h.amount = reqQuantity(f, "amount", kMassOrVolume);
    const Value& timing = f.require("timing");
    h.timing = readTiming(timing, f.child("timing"), doc);
    f.finish(h.preserved, schema::kHopAddition);
    return h;
}

Culture readCulture(const Value& v, const std::string& pointer, EntityId id, Document& doc) {
    Fields f(v, pointer, doc);
    Culture c;
    c.id = id;
    c.uuid = Uuid::generate();
    c.name = reqString(f, "name");
    c.type = reqEnum(f, "type", schema::kCultureTypes);
    c.form = reqEnum(f, "form", schema::kCultureForms);
    c.producer = optString(f, "producer");
    c.productId = optString(f, "product_id");
    c.attenuation = optQuantity(f, "attenuation", kPercent);
    c.timesCultured = optInteger(f, "times_cultured");
    c.cellCountBillions = optInteger(f, "cell_count_billions");
    c.amount = reqQuantity(f, "amount", kMassVolumeOrCount);
    c.timing = optTiming(f, "timing");
    f.finish(c.preserved, schema::kCultureAddition);
    return c;
}

Miscellaneous readMiscellaneous(const Value& v, const std::string& pointer, EntityId id, Document& doc) {
    Fields f(v, pointer, doc);
    Miscellaneous m;
    m.id = id;
    m.uuid = Uuid::generate();
    m.name = reqString(f, "name");
    m.type = reqEnum(f, "type", schema::kMiscellaneousTypes);
    m.producer = optString(f, "producer");
    m.productId = optString(f, "product_id");
    m.amount = reqQuantity(f, "amount", kMassVolumeOrCount);
    m.timing = optTiming(f, "timing");
    f.finish(m.preserved, schema::kMiscellaneousAddition);
    return m;
}

// Reads the water chemistry properties from `f`. The caller decides what to do
// with leftover keys, because a water addition also has an amount.
WaterProfile readWaterProfile(Fields& f) {
    WaterProfile w;
    w.name = reqString(f, "name");
    w.producer = optString(f, "producer");
    w.calcium = reqQuantity(f, "calcium", kConcentration);
    w.bicarbonate = reqQuantity(f, "bicarbonate", kConcentration);
    w.sulfate = reqQuantity(f, "sulfate", kConcentration);
    w.chloride = reqQuantity(f, "chloride", kConcentration);
    w.sodium = reqQuantity(f, "sodium", kConcentration);
    w.magnesium = reqQuantity(f, "magnesium", kConcentration);
    w.carbonate = optQuantity(f, "carbonate", kConcentration);
    w.potassium = optQuantity(f, "potassium", kConcentration);
    w.iron = optQuantity(f, "iron", kConcentration);
    w.nitrate = optQuantity(f, "nitrate", kConcentration);
    w.nitrite = optQuantity(f, "nitrite", kConcentration);
    w.fluoride = optQuantity(f, "fluoride", kConcentration);
    return w;
}

WaterAddition readWaterAddition(const Value& v, const std::string& pointer, EntityId id, Document& doc) {
    Fields f(v, pointer, doc);
    WaterAddition a;
    a.id = id;
    a.uuid = Uuid::generate();
    a.amount = optQuantity(f, "amount", kVolume);
    a.water = readWaterProfile(f);
    // Water additions are an open object in the schema, so no property is foreign.
    f.finish(a.water.preserved, schema::kWaterAddition);
    return a;
}

// ---- Procedures --------------------------------------------------------------

MashStep readMashStep(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    MashStep s;
    s.name = reqString(f, "name");
    s.type = reqEnum(f, "type", schema::kMashStepTypes);
    s.amount = optQuantity(f, "amount", kVolume);
    s.stepTemperature = reqQuantity(f, "step_temperature", kTemperature);
    s.stepTime = reqQuantity(f, "step_time", kTime, true);
    s.rampTime = optQuantity(f, "ramp_time", kTime, true);
    s.endTemperature = optQuantity(f, "end_temperature", kTemperature);
    s.description = optString(f, "description");
    s.waterGrainRatio = optMeasure(f, "water_grain_ratio");
    s.infuseTemperature = optQuantity(f, "infuse_temperature", kTemperature);
    s.startPh = optMeasure(f, "start_ph");
    s.endPh = optMeasure(f, "end_ph");
    f.finish(s.preserved, schema::kMashStep);
    return s;
}

Mash readMash(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    Mash m;
    m.name = reqString(f, "name");
    m.grainTemperature = reqQuantity(f, "grain_temperature", kTemperature);
    m.notes = optString(f, "notes");
    const Value& steps = f.require("mash_steps");
    const Array& items = expectArray(steps, f.child("mash_steps"));
    for (std::size_t i = 0; i < items.size(); ++i) {
        m.steps.push_back(readMashStep(items[i], f.child("mash_steps") + "/" + std::to_string(i), doc));
    }
    f.finish(m.preserved, schema::kMashProcedure);
    return m;
}

FermentationStep readFermentationStep(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    FermentationStep s;
    s.name = reqString(f, "name");
    s.description = optString(f, "description");
    s.startTemperature = optQuantity(f, "start_temperature", kTemperature);
    s.endTemperature = optQuantity(f, "end_temperature", kTemperature);
    s.stepTime = optQuantity(f, "step_time", kTime, true);
    s.freeRise = optBool(f, "free_rise");
    s.startGravity = optMeasure(f, "start_gravity");
    s.endGravity = optMeasure(f, "end_gravity");
    s.startPh = optMeasure(f, "start_ph");
    s.endPh = optMeasure(f, "end_ph");
    s.vessel = optString(f, "vessel");
    s.vesselPressure = optMeasure(f, "vessel_pressure");
    f.finish(s.preserved, schema::kFermentationStep);
    return s;
}

Fermentation readFermentation(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    Fermentation fermentation;
    fermentation.name = reqString(f, "name");
    fermentation.description = optString(f, "description");
    fermentation.notes = optString(f, "notes");
    const Value& steps = f.require("fermentation_steps");
    const Array& items = expectArray(steps, f.child("fermentation_steps"));
    for (std::size_t i = 0; i < items.size(); ++i) {
        fermentation.steps.push_back(readFermentationStep(
            items[i], f.child("fermentation_steps") + "/" + std::to_string(i), doc));
    }
    f.finish(fermentation.preserved, schema::kFermentationProcedure);
    return fermentation;
}

BoilStep readBoilStep(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    BoilStep s;
    s.name = reqString(f, "name");
    s.description = optString(f, "description");
    s.startTemperature = optQuantity(f, "start_temperature", kTemperature);
    s.endTemperature = optQuantity(f, "end_temperature", kTemperature);
    s.rampTime = optQuantity(f, "ramp_time", kTime, true);
    s.stepTime = optQuantity(f, "step_time", kTime, true);
    s.startGravity = optMeasure(f, "start_gravity");
    s.endGravity = optMeasure(f, "end_gravity");
    s.startPh = optMeasure(f, "start_ph");
    s.endPh = optMeasure(f, "end_ph");
    s.chillingType = optEnum(f, "chilling_type", schema::kChillingTypes);
    f.finish(s.preserved, schema::kBoilStep);
    return s;
}

Boil readBoil(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    Boil b;
    b.name = optString(f, "name");
    b.description = optString(f, "description");
    b.notes = optString(f, "notes");
    b.preBoilSize = optQuantity(f, "pre_boil_size", kVolume);
    b.boilTime = reqQuantity(f, "boil_time", kTime, true);
    readOptionalListOf(f, "boil_steps", b.steps, [&](const Value& item, const std::string& p) {
        return readBoilStep(item, p, doc);
    });
    f.finish(b.preserved, schema::kBoilProcedure);
    return b;
}

Efficiency readEfficiency(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    Efficiency e;
    e.conversion = optQuantity(f, "conversion", kPercent);
    e.lauter = optQuantity(f, "lauter", kPercent);
    e.mash = optQuantity(f, "mash", kPercent);
    e.brewhouse = reqQuantity(f, "brewhouse", kPercent);
    f.finish(e.preserved, schema::kEfficiency);
    return e;
}

// ---- Recipe and document -----------------------------------------------------

Recipe readRecipe(const Value& v, const std::string& pointer, Document& doc) {
    Fields f(v, pointer, doc);
    Recipe r;
    r.id = doc.allocateId();
    r.uuid = Uuid::generate();
    r.name = reqString(f, "name");
    // The schema enum has already been checked, so the name always maps.
    r.type = *recipeTypeFromName(reqEnum(f, "type", schema::kRecipeTypes));
    r.author = reqString(f, "author");
    r.coauthor = optString(f, "coauthor");
    r.created = optString(f, "created");
    r.batchSize = reqQuantity(f, "batch_size", kVolume);
    r.efficiency = readEfficiency(f.require("efficiency"), f.child("efficiency"), doc);

    // The ingredients object: fermentable_additions is required.
    const Value& ingredientsValue = f.require("ingredients");
    Fields ingredients(ingredientsValue, f.child("ingredients"), doc);
    if (ingredients.get("fermentable_additions") == nullptr) {
        fail(ingredients.pointer(), "missing required property \"fermentable_additions\"");
    }
    readList(ingredients, "fermentable_additions", r.fermentables,
             [&](const Value& item, const std::string& p) { return readFermentable(item, p, doc.allocateId(), doc); });
    readOptionalListOf(ingredients, "hop_additions", r.hops,
                       [&](const Value& item, const std::string& p) { return readHop(item, p, doc.allocateId(), doc); });
    readOptionalListOf(ingredients, "miscellaneous_additions", r.miscellaneous,
                       [&](const Value& item, const std::string& p) {
                           return readMiscellaneous(item, p, doc.allocateId(), doc);
                       });
    readOptionalListOf(ingredients, "culture_additions", r.cultures,
                       [&](const Value& item, const std::string& p) { return readCulture(item, p, doc.allocateId(), doc); });
    readOptionalListOf(ingredients, "water_additions", r.waters,
                       [&](const Value& item, const std::string& p) {
                           return readWaterAddition(item, p, doc.allocateId(), doc);
                       });
    ingredients.finish(r.ingredientsPreserved, schema::kIngredients);

    if (const Value* mash = f.get("mash")) {
        r.mash = readMash(*mash, f.child("mash"), doc);
    }
    r.notes = optString(f, "notes");
    r.originalGravity = optMeasure(f, "original_gravity");
    r.finalGravity = optMeasure(f, "final_gravity");
    r.alcoholByVolume = optQuantity(f, "alcohol_by_volume", kPercent);
    r.colorEstimate = optMeasure(f, "color_estimate");
    r.beerPh = optMeasure(f, "beer_pH");
    r.apparentAttenuation = optQuantity(f, "apparent_attenuation", kPercent);
    if (const Value* carbonation = f.get("carbonation")) {
        r.carbonation = expectNumber(*carbonation, f.child("carbonation"));
    }
    if (const Value* calories = f.get("calories_per_pint")) {
        r.caloriesPerPint = expectNumber(*calories, f.child("calories_per_pint"));
    }
    if (const Value* fermentation = f.get("fermentation")) {
        r.fermentation = readFermentation(*fermentation, f.child("fermentation"), doc);
    }
    if (const Value* boil = f.get("boil")) {
        r.boil = readBoil(*boil, f.child("boil"), doc);
    }

    // Typed later: style, ibu_estimate, packaging, taste.
    f.finish(r.preserved, schema::kRecipe);
    return r;
}

Rational readVersion(const Value& v, const std::string& pointer) {
    const Rational version = expectNumber(v, pointer);
    const Rational supported[] = {Rational::fromDecimal("2.01"), Rational::fromDecimal("2.06"), Rational(3)};
    for (const Rational& candidate : supported) {
        if (version == candidate) {
            return version;
        }
    }
    fail(pointer, "version is not one the schema accepts (2.01, 2.06, or 3)");
}

}  // namespace

// Gives each foreign property the entity that contains it, and the path from
// that entity to the property's parent.
void attachForeignOwners(Document& document) {
    struct Anchor {
        std::string pointer;
        Uuid uuid;
    };
    std::vector<Anchor> anchors;
    forEachEntity(document, [&](std::string_view, const std::string& pointer, const std::string&, const Uuid& uuid) {
        anchors.push_back(Anchor{pointer, uuid});
    });

    for (ForeignProperty& property : document.foreign) {
        // The innermost containing entity: the longest pointer that is a prefix of the parent.
        const Anchor* owner = nullptr;
        for (const Anchor& a : anchors) {
            const bool contains = property.parent == a.pointer ||
                                  property.parent.compare(0, a.pointer.size() + 1, a.pointer + "/") == 0;
            if (contains && (owner == nullptr || a.pointer.size() > owner->pointer.size())) {
                owner = &a;
            }
        }
        if (owner != nullptr) {
            property.owner = owner->uuid;
            property.path = property.parent.substr(owner->pointer.size());
        } else {
            property.owner = Uuid{};
            property.path = property.parent;
        }
    }
}

Document readBeerJson(std::string_view text) {
    const Value root = json::parse(text);
    Document document;

    // The root object's only defined property is "beerjson".
    constexpr std::array<std::string_view, 1> rootProperties = {"beerjson"};
    const schema::ObjectSchema rootObject{"Root", true, rootProperties};
    Fields top(root, "", document);
    const Value& beerjson = top.require("beerjson");
    Object unusedRoot;
    top.finish(unusedRoot, rootObject);

    Fields f(beerjson, "/beerjson", document);
    document.version = readVersion(f.require("version"), f.child("version"));
    readList(f, "recipes", document.recipes,
             [&](const Value& item, const std::string& p) { return readRecipe(item, p, document); });
    f.finish(document.preserved, schema::kBeerJson);
    attachForeignOwners(document);
    return document;
}

}  // namespace kettlelogic::model
