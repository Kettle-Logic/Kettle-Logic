#include "kettlelogic/model/beerjson.hpp"

#include <optional>
#include <string>
#include <utility>

namespace kettlelogic::model {

namespace {

using json::Array;
using json::Member;
using json::Object;
using json::Value;

void put(Object& o, std::string key, Value value) {
    o.push_back(Member{std::move(key), std::move(value)});
}

template <typename T, typename Convert>
void putOpt(Object& o, std::string key, const std::optional<T>& v, Convert convert) {
    if (v) {
        put(o, std::move(key), convert(*v));
    }
}

// Appends every preserved member after the typed ones.
void appendPreserved(Object& o, const Object& preserved) {
    o.insert(o.end(), preserved.begin(), preserved.end());
}

template <typename T, typename Convert>
Array mapList(const std::vector<T>& items, Convert convert) {
    Array out;
    out.reserve(items.size());
    for (const T& item : items) {
        out.push_back(convert(item));
    }
    return out;
}

Value strJson(const std::string& s) {
    return Value::string(s);
}

// Written in the unit the quantity was authored in, so a value read in pounds
// is written in pounds. Repeating decimals are rounded (see Rational::toDecimal).
Value quantityJson(const Quantity& q) {
    Object o;
    put(o, "value", Value::fromRational(q.valueIn(q.unit())));
    put(o, "unit", Value::string(std::string(beerJsonName(q.unit()))));
    return Value::object(std::move(o));
}

Value measureJson(const Measure& m) {
    Object o;
    put(o, "value", Value::fromRational(m.value()));
    put(o, "unit", Value::string(m.unit()));
    return Value::object(std::move(o));
}

Value timingJson(const Timing& t) {
    Object o;
    putOpt(o, "time", t.time, quantityJson);
    putOpt(o, "duration", t.duration, quantityJson);
    putOpt(o, "continuous", t.continuous, [](bool b) { return Value::boolean(b); });
    putOpt(o, "specific_gravity", t.specificGravity, measureJson);
    putOpt(o, "pH", t.pH, measureJson);
    putOpt(o, "step", t.step, [](long long n) { return Value::fromRational(Rational(n)); });
    putOpt(o, "use", t.use, strJson);
    appendPreserved(o, t.preserved);
    return Value::object(std::move(o));
}

Value yieldJson(const Yield& y) {
    Object o;
    putOpt(o, "fine_grind", y.fineGrind, quantityJson);
    putOpt(o, "coarse_grind", y.coarseGrind, quantityJson);
    putOpt(o, "fine_coarse_difference", y.fineCoarseDifference, quantityJson);
    putOpt(o, "potential", y.potential, measureJson);
    appendPreserved(o, y.preserved);
    return Value::object(std::move(o));
}

Value fermentableJson(const Fermentable& x) {
    Object o;
    put(o, "name", strJson(x.name));
    put(o, "type", strJson(x.type));
    putOpt(o, "origin", x.origin, strJson);
    putOpt(o, "producer", x.producer, strJson);
    putOpt(o, "product_id", x.productId, strJson);
    putOpt(o, "grain_group", x.grainGroup, strJson);
    put(o, "yield", yieldJson(x.yield));
    put(o, "color", measureJson(x.color));
    put(o, "amount", quantityJson(x.amount));
    putOpt(o, "timing", x.timing, timingJson);
    appendPreserved(o, x.preserved);
    return Value::object(std::move(o));
}

Value hopJson(const Hop& h) {
    Object o;
    put(o, "name", strJson(h.name));
    put(o, "alpha_acid", quantityJson(h.alphaAcid));
    putOpt(o, "beta_acid", h.betaAcid, quantityJson);
    putOpt(o, "producer", h.producer, strJson);
    putOpt(o, "product_id", h.productId, strJson);
    putOpt(o, "origin", h.origin, strJson);
    putOpt(o, "year", h.year, strJson);
    putOpt(o, "form", h.form, strJson);
    put(o, "amount", quantityJson(h.amount));
    put(o, "timing", timingJson(h.timing));
    appendPreserved(o, h.preserved);
    return Value::object(std::move(o));
}

Value cultureJson(const Culture& c) {
    Object o;
    put(o, "name", strJson(c.name));
    put(o, "type", strJson(c.type));
    put(o, "form", strJson(c.form));
    putOpt(o, "producer", c.producer, strJson);
    putOpt(o, "product_id", c.productId, strJson);
    putOpt(o, "attenuation", c.attenuation, quantityJson);
    putOpt(o, "times_cultured", c.timesCultured, [](long long n) { return Value::fromRational(Rational(n)); });
    putOpt(o, "cell_count_billions", c.cellCountBillions,
           [](long long n) { return Value::fromRational(Rational(n)); });
    put(o, "amount", quantityJson(c.amount));
    putOpt(o, "timing", c.timing, timingJson);
    appendPreserved(o, c.preserved);
    return Value::object(std::move(o));
}

Value miscellaneousJson(const Miscellaneous& m) {
    Object o;
    put(o, "name", strJson(m.name));
    put(o, "type", strJson(m.type));
    putOpt(o, "producer", m.producer, strJson);
    putOpt(o, "product_id", m.productId, strJson);
    put(o, "amount", quantityJson(m.amount));
    putOpt(o, "timing", m.timing, timingJson);
    appendPreserved(o, m.preserved);
    return Value::object(std::move(o));
}

void waterProfileInto(Object& o, const WaterProfile& w) {
    put(o, "name", strJson(w.name));
    putOpt(o, "producer", w.producer, strJson);
    put(o, "calcium", quantityJson(w.calcium));
    put(o, "bicarbonate", quantityJson(w.bicarbonate));
    put(o, "sulfate", quantityJson(w.sulfate));
    put(o, "chloride", quantityJson(w.chloride));
    put(o, "sodium", quantityJson(w.sodium));
    put(o, "magnesium", quantityJson(w.magnesium));
    putOpt(o, "carbonate", w.carbonate, quantityJson);
    putOpt(o, "potassium", w.potassium, quantityJson);
    putOpt(o, "iron", w.iron, quantityJson);
    putOpt(o, "nitrate", w.nitrate, quantityJson);
    putOpt(o, "nitrite", w.nitrite, quantityJson);
    putOpt(o, "fluoride", w.fluoride, quantityJson);
    appendPreserved(o, w.preserved);
}

Value waterAdditionJson(const WaterAddition& a) {
    Object o;
    waterProfileInto(o, a.water);
    putOpt(o, "amount", a.amount, quantityJson);
    return Value::object(std::move(o));
}

Value mashStepJson(const MashStep& s) {
    Object o;
    put(o, "name", strJson(s.name));
    put(o, "type", strJson(s.type));
    putOpt(o, "amount", s.amount, quantityJson);
    put(o, "step_temperature", quantityJson(s.stepTemperature));
    put(o, "step_time", quantityJson(s.stepTime));
    putOpt(o, "ramp_time", s.rampTime, quantityJson);
    putOpt(o, "end_temperature", s.endTemperature, quantityJson);
    putOpt(o, "description", s.description, strJson);
    putOpt(o, "water_grain_ratio", s.waterGrainRatio, measureJson);
    putOpt(o, "infuse_temperature", s.infuseTemperature, quantityJson);
    putOpt(o, "start_ph", s.startPh, measureJson);
    putOpt(o, "end_ph", s.endPh, measureJson);
    appendPreserved(o, s.preserved);
    return Value::object(std::move(o));
}

Value mashJson(const Mash& m) {
    Object o;
    put(o, "name", strJson(m.name));
    put(o, "grain_temperature", quantityJson(m.grainTemperature));
    putOpt(o, "notes", m.notes, strJson);
    Array steps;
    for (const MashStep& s : m.steps) {
        steps.push_back(mashStepJson(s));
    }
    put(o, "mash_steps", Value::array(std::move(steps)));
    appendPreserved(o, m.preserved);
    return Value::object(std::move(o));
}

Value fermentationStepJson(const FermentationStep& s) {
    Object o;
    put(o, "name", strJson(s.name));
    putOpt(o, "description", s.description, strJson);
    putOpt(o, "start_temperature", s.startTemperature, quantityJson);
    putOpt(o, "end_temperature", s.endTemperature, quantityJson);
    putOpt(o, "step_time", s.stepTime, quantityJson);
    putOpt(o, "free_rise", s.freeRise, [](bool b) { return Value::boolean(b); });
    putOpt(o, "start_gravity", s.startGravity, measureJson);
    putOpt(o, "end_gravity", s.endGravity, measureJson);
    putOpt(o, "start_ph", s.startPh, measureJson);
    putOpt(o, "end_ph", s.endPh, measureJson);
    putOpt(o, "vessel", s.vessel, strJson);
    putOpt(o, "vessel_pressure", s.vesselPressure, measureJson);
    appendPreserved(o, s.preserved);
    return Value::object(std::move(o));
}

Value fermentationJson(const Fermentation& f) {
    Object o;
    put(o, "name", strJson(f.name));
    putOpt(o, "description", f.description, strJson);
    putOpt(o, "notes", f.notes, strJson);
    Array steps;
    for (const FermentationStep& s : f.steps) {
        steps.push_back(fermentationStepJson(s));
    }
    put(o, "fermentation_steps", Value::array(std::move(steps)));
    appendPreserved(o, f.preserved);
    return Value::object(std::move(o));
}

Value boilStepJson(const BoilStep& s) {
    Object o;
    put(o, "name", strJson(s.name));
    putOpt(o, "description", s.description, strJson);
    putOpt(o, "start_temperature", s.startTemperature, quantityJson);
    putOpt(o, "end_temperature", s.endTemperature, quantityJson);
    putOpt(o, "ramp_time", s.rampTime, quantityJson);
    putOpt(o, "step_time", s.stepTime, quantityJson);
    putOpt(o, "start_gravity", s.startGravity, measureJson);
    putOpt(o, "end_gravity", s.endGravity, measureJson);
    putOpt(o, "start_ph", s.startPh, measureJson);
    putOpt(o, "end_ph", s.endPh, measureJson);
    putOpt(o, "chilling_type", s.chillingType, strJson);
    appendPreserved(o, s.preserved);
    return Value::object(std::move(o));
}

Value boilJson(const Boil& b) {
    Object o;
    putOpt(o, "name", b.name, strJson);
    putOpt(o, "description", b.description, strJson);
    putOpt(o, "notes", b.notes, strJson);
    putOpt(o, "pre_boil_size", b.preBoilSize, quantityJson);
    put(o, "boil_time", quantityJson(b.boilTime));
    putOpt(o, "boil_steps", b.steps, [](const std::vector<BoilStep>& items) {
        return Value::array(mapList(items, boilStepJson));
    });
    appendPreserved(o, b.preserved);
    return Value::object(std::move(o));
}

Value efficiencyJson(const Efficiency& e) {
    Object o;
    putOpt(o, "conversion", e.conversion, quantityJson);
    putOpt(o, "lauter", e.lauter, quantityJson);
    putOpt(o, "mash", e.mash, quantityJson);
    put(o, "brewhouse", quantityJson(e.brewhouse));
    appendPreserved(o, e.preserved);
    return Value::object(std::move(o));
}

Value recipeJson(const Recipe& r) {
    Object o;
    put(o, "name", strJson(r.name));
    put(o, "type", strJson(std::string(recipeTypeName(r.type))));
    put(o, "author", strJson(r.author));
    putOpt(o, "coauthor", r.coauthor, strJson);
    putOpt(o, "created", r.created, strJson);
    put(o, "batch_size", quantityJson(r.batchSize));
    put(o, "efficiency", efficiencyJson(r.efficiency));

    // fermentable_additions is required. Each optional list is written exactly
    // when it is present, including as [] when it is present but empty.
    Object ingredients;
    put(ingredients, "fermentable_additions",
        Value::array(mapList(r.fermentables, fermentableJson)));
    putOpt(ingredients, "hop_additions", r.hops,
           [](const std::vector<Hop>& items) { return Value::array(mapList(items, hopJson)); });
    putOpt(ingredients, "miscellaneous_additions", r.miscellaneous,
           [](const std::vector<Miscellaneous>& items) { return Value::array(mapList(items, miscellaneousJson)); });
    putOpt(ingredients, "culture_additions", r.cultures,
           [](const std::vector<Culture>& items) { return Value::array(mapList(items, cultureJson)); });
    putOpt(ingredients, "water_additions", r.waters,
           [](const std::vector<WaterAddition>& items) { return Value::array(mapList(items, waterAdditionJson)); });
    appendPreserved(ingredients, r.ingredientsPreserved);
    put(o, "ingredients", Value::object(std::move(ingredients)));

    putOpt(o, "mash", r.mash, mashJson);
    putOpt(o, "notes", r.notes, strJson);
    putOpt(o, "original_gravity", r.originalGravity, measureJson);
    putOpt(o, "final_gravity", r.finalGravity, measureJson);
    putOpt(o, "alcohol_by_volume", r.alcoholByVolume, quantityJson);
    putOpt(o, "color_estimate", r.colorEstimate, measureJson);
    putOpt(o, "beer_pH", r.beerPh, measureJson);
    putOpt(o, "apparent_attenuation", r.apparentAttenuation, quantityJson);
    putOpt(o, "carbonation", r.carbonation, [](const Rational& n) { return Value::fromRational(n); });
    putOpt(o, "calories_per_pint", r.caloriesPerPint, [](const Rational& n) { return Value::fromRational(n); });
    putOpt(o, "fermentation", r.fermentation, fermentationJson);
    putOpt(o, "boil", r.boil, boilJson);
    appendPreserved(o, r.preserved);
    return Value::object(std::move(o));
}

}  // namespace

std::string writeBeerJson(const Document& document) {
    Object beerjson;
    put(beerjson, "version", Value::fromRational(document.version));
    put(beerjson, "recipes", Value::array(mapList(document.recipes, recipeJson)));
    appendPreserved(beerjson, document.preserved);

    Object root;
    put(root, "beerjson", Value::object(std::move(beerjson)));
    return json::serialize(Value::object(std::move(root)), 2);
}

}  // namespace kettlelogic::model
