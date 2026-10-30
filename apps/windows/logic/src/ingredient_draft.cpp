#include "kettlelogic/app/ingredient_draft.hpp"

#include "kettlelogic/app/text.hpp"
#include "kettlelogic/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace kettlelogic::app {

namespace {

std::optional<std::string> nonEmpty(const std::string& text) {
    return text.empty() ? std::nullopt : std::optional<std::string>(text);
}

std::optional<std::string> nonBlank(const std::string& text) {
    return trimmed(text).empty() ? std::nullopt : std::optional<std::string>(text);
}

}  // namespace

IngredientDraft::IngredientDraft(IngredientKind k, UnitSystem system) : kind(k) {
    const Units units{system};
    switch (kind) {
        case IngredientKind::fermentable:
            type = fermentableTypes()[0];
            unit = units.bulkMassUnit();
            yieldValue = "1.036";
            colorValue = "2";
            break;
        case IngredientKind::hop:
            form = hopForms()[0];
            use = hopUses()[0];
            unit = units.smallMassUnit();
            time = "60";
            timeUnit = hopTimeUnits()[0];
            break;
        case IngredientKind::culture:
            type = cultureTypes()[0];
            form = cultureForms()[0];
            amount = "1";
            unit = cultureUnits()[0];
            break;
        case IngredientKind::water:
            break;
        case IngredientKind::miscellaneous:
            type = miscellaneousTypes()[0];
            unit = units.smallMassUnit();
            break;
    }
    priceUnit = unit;
}

void IngredientDraft::adoptPrice(const std::optional<PriceText>& existing) {
    price = existing ? existing->amount : "";
    priceUnit = existing ? existing->per : unit;
}

IngredientDraft IngredientDraft::from(const model::Fermentable& f, UnitSystem units) {
    IngredientDraft d(IngredientKind::fermentable, units);
    d.name = f.name;
    d.type = f.type;
    const QuantityText amount = describe(f.amount);
    d.amount = amount.value;
    d.unit = amount.unit;
    if (f.yield.potential) {
        const QuantityText potential = describe(*f.yield.potential);
        d.yieldValue = potential.value;
        d.yieldUnit = potential.unit;
    } else {
        d.yieldValue = "";
        d.yieldUnit = "sg";
    }
    const QuantityText color = describe(f.color);
    d.colorValue = color.value;
    d.colorUnit = color.unit;
    d.adoptPrice(describe(f.price));
    return d;
}

IngredientDraft IngredientDraft::from(const model::Hop& h, UnitSystem units) {
    IngredientDraft d(IngredientKind::hop, units);
    d.name = h.name;
    d.alpha = describe(h.alphaAcid).value;
    d.beta = h.betaAcid ? describe(*h.betaAcid).value : "";
    const QuantityText amount = describe(h.amount);
    d.amount = amount.value;
    d.unit = amount.unit;
    d.form = h.form.value_or("");
    d.use = h.timing.use.value_or("");
    if (h.timing.time) {
        const QuantityText time = describe(*h.timing.time);
        d.time = time.value;
        d.timeUnit = time.unit;
    } else {
        d.time = "";
        d.timeUnit = hopTimeUnits()[0];
    }
    d.adoptPrice(describe(h.price));
    return d;
}

IngredientDraft IngredientDraft::from(const model::Culture& c, UnitSystem units) {
    IngredientDraft d(IngredientKind::culture, units);
    d.name = c.name;
    d.type = c.type;
    d.form = c.form;
    const QuantityText amount = describe(c.amount);
    d.amount = amount.value;
    d.unit = amount.unit;
    d.attenuation = c.attenuation ? describe(*c.attenuation).value : "";
    d.adoptPrice(describe(c.price));
    return d;
}

IngredientDraft IngredientDraft::from(const model::WaterAddition& w, UnitSystem units) {
    IngredientDraft d(IngredientKind::water, units);
    d.name = w.water.name;
    d.calcium = describe(w.water.calcium).value;
    d.bicarbonate = describe(w.water.bicarbonate).value;
    d.sulfate = describe(w.water.sulfate).value;
    d.chloride = describe(w.water.chloride).value;
    d.sodium = describe(w.water.sodium).value;
    d.magnesium = describe(w.water.magnesium).value;
    d.waterVolume = w.amount ? describe(*w.amount).value : "";
    return d;
}

IngredientDraft IngredientDraft::from(const model::Miscellaneous& m, UnitSystem units) {
    IngredientDraft d(IngredientKind::miscellaneous, units);
    d.name = m.name;
    d.type = m.type;
    const QuantityText amount = describe(m.amount);
    d.amount = amount.value;
    d.unit = amount.unit;
    d.adoptPrice(describe(m.price));
    return d;
}

IngredientDraft IngredientDraft::from(const FermentableTemplate& t, UnitSystem units) {
    IngredientDraft d(IngredientKind::fermentable, units);
    d.name = t.name;
    d.type = t.type;
    d.yieldValue = t.yieldSG;
    d.colorValue = t.colorSRM;
    return d;
}

IngredientDraft IngredientDraft::from(const HopTemplate& t, UnitSystem units) {
    IngredientDraft d(IngredientKind::hop, units);
    d.name = t.name;
    d.alpha = t.alphaPercent;
    d.beta = t.betaPercent.value_or("");
    return d;
}

IngredientDraft IngredientDraft::from(const CultureTemplate& t, UnitSystem units) {
    IngredientDraft d(IngredientKind::culture, units);
    d.name = t.name;
    d.type = t.type;
    d.form = t.form;
    d.attenuation = t.attenuationPercent.value_or("");
    return d;
}

IngredientDraft IngredientDraft::from(const WaterTemplate& t, UnitSystem units) {
    IngredientDraft d(IngredientKind::water, units);
    d.name = t.name;
    d.calcium = t.calcium;
    d.bicarbonate = t.bicarbonate;
    d.sulfate = t.sulfate;
    d.chloride = t.chloride;
    d.sodium = t.sodium;
    d.magnesium = t.magnesium;
    return d;
}

IngredientDraft IngredientDraft::from(const MiscellaneousTemplate& t, UnitSystem units) {
    IngredientDraft d(IngredientKind::miscellaneous, units);
    d.name = t.name;
    d.type = t.type;
    d.unit = t.unit;
    d.priceUnit = t.unit;
    return d;
}

IngredientDraft IngredientDraft::adopting(const IngredientDraft& entry) const {
    IngredientDraft copy = *this;
    copy.name = entry.name;
    switch (kind) {
        case IngredientKind::fermentable:
            copy.type = entry.type;
            copy.yieldValue = entry.yieldValue;
            copy.yieldUnit = entry.yieldUnit;
            copy.colorValue = entry.colorValue;
            copy.colorUnit = entry.colorUnit;
            break;
        case IngredientKind::hop:
            copy.alpha = entry.alpha;
            copy.beta = entry.beta;
            break;
        case IngredientKind::culture:
            copy.type = entry.type;
            copy.form = entry.form;
            copy.attenuation = entry.attenuation;
            break;
        case IngredientKind::water:
            copy.calcium = entry.calcium;
            copy.bicarbonate = entry.bicarbonate;
            copy.sulfate = entry.sulfate;
            copy.chloride = entry.chloride;
            copy.sodium = entry.sodium;
            copy.magnesium = entry.magnesium;
            break;
        case IngredientKind::miscellaneous:
            copy.type = entry.type;
            copy.unit = entry.unit;
            break;
    }
    // A library entry's price is kept when it can price this amount. A catalog entry has none.
    if (!trimmed(entry.price).empty()) {
        const std::vector<std::string> allowed = priceUnits(unit);
        if (std::find(allowed.begin(), allowed.end(), entry.priceUnit) != allowed.end()) {
            copy.price = entry.price;
            copy.priceUnit = entry.priceUnit;
        }
    }
    return copy;
}

bool IngredientDraft::isComplete(bool quantities) const {
    if (trimmed(name).empty()) {
        return false;
    }
    switch (kind) {
        case IngredientKind::fermentable:
            return !yieldValue.empty() && !colorValue.empty() && (!quantities || !amount.empty());
        case IngredientKind::hop:
            return !alpha.empty() && (!quantities || !amount.empty());
        case IngredientKind::culture:
        case IngredientKind::miscellaneous:
            return !quantities || !amount.empty();
        case IngredientKind::water:
            for (const std::string* mineral : {&calcium, &bicarbonate, &sulfate, &chloride, &sodium, &magnesium}) {
                if (mineral->empty()) {
                    return false;
                }
            }
            return true;
    }
    return false;
}

std::string IngredientDraft::summary() const {
    std::vector<std::string> parts;
    switch (kind) {
        case IngredientKind::fermentable:
            parts = {type, "yield " + yieldValue + " " + yieldUnit, colorValue + " " + colorUnit};
            break;
        case IngredientKind::hop:
            parts.push_back(alpha + "% alpha acid");
            if (!beta.empty()) parts.push_back(beta + "% beta acid");
            if (!form.empty()) parts.push_back(form);
            if (!use.empty()) parts.push_back(hopUseName(use));
            break;
        case IngredientKind::culture:
            parts.push_back(type + " \xC2\xB7 " + form);
            if (!attenuation.empty()) parts.push_back(attenuation + "% attenuation");
            break;
        case IngredientKind::water:
            parts.push_back("Ca " + calcium + " \xC2\xB7 Mg " + magnesium + " \xC2\xB7 Na " + sodium + " \xC2\xB7 Cl " +
                            chloride + " \xC2\xB7 SO4 " + sulfate + " \xC2\xB7 HCO3 " + bicarbonate + " mg/L");
            break;
        case IngredientKind::miscellaneous:
            parts = {type, "measured in " + unit};
            break;
    }
    if (!trimmed(price).empty()) {
        parts.push_back(trimmed(price) + " / " + priceUnit);
    }
    std::string out;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        out += (out.empty() ? "" : " \xC2\xB7 ") + part;
    }
    return out;
}

void IngredientDraft::commit(RecipeDocument& document, EntityId recipe, std::optional<EntityId> existing) const {
    const std::string cleanName = trimmed(name);
    const bool hasTime = !trimmed(time).empty();

    switch (kind) {
        case IngredientKind::fermentable: {
            const FermentableInput in{cleanName, type, amount, unit, yieldValue, yieldUnit, colorValue, colorUnit};
            if (existing) document.setFermentable(recipe, *existing, in);
            else document.addFermentable(recipe, in);
            break;
        }
        case IngredientKind::hop: {
            const HopInput in{cleanName,       alpha,
                              nonBlank(beta),  amount,
                              unit,            nonEmpty(form),
                              nonEmpty(use),   hasTime ? std::optional<std::string>(time) : std::nullopt,
                              hasTime ? std::optional<std::string>(timeUnit) : std::nullopt};
            if (existing) document.setHop(recipe, *existing, in);
            else document.addHop(recipe, in);
            break;
        }
        case IngredientKind::culture: {
            const CultureInput in{cleanName, type, form, amount, unit, nonBlank(attenuation)};
            if (existing) document.setCulture(recipe, *existing, in);
            else document.addCulture(recipe, in);
            break;
        }
        case IngredientKind::water: {
            const WaterInput in{cleanName, calcium, bicarbonate, sulfate, chloride, sodium, magnesium,
                                nonBlank(waterVolume)};
            if (existing) document.setWater(recipe, *existing, in);
            else document.addWater(recipe, in);
            break;
        }
        case IngredientKind::miscellaneous: {
            const MiscellaneousInput in{cleanName, type, amount, unit};
            if (existing) document.setMiscellaneous(recipe, *existing, in);
            else document.addMiscellaneous(recipe, in);
            break;
        }
    }
    savePrice(document, recipe, existing);
}

void IngredientDraft::savePrice(RecipeDocument& document, EntityId recipeId, std::optional<EntityId> existing) const {
    const model::Recipe* recipe = document.recipe(recipeId);
    if (kind == IngredientKind::water || recipe == nullptr) {
        return;
    }
    struct Item {
        EntityId id;
        std::optional<PriceText> price;
    };
    std::vector<Item> items;
    switch (kind) {
        case IngredientKind::fermentable:
            for (const auto& f : recipe->fermentables) items.push_back({f.id, describe(f.price)});
            break;
        case IngredientKind::hop:
            for (const auto& h : recipe->hops.value_or(std::vector<model::Hop>{})) items.push_back({h.id, describe(h.price)});
            break;
        case IngredientKind::culture:
            for (const auto& c : recipe->cultures.value_or(std::vector<model::Culture>{})) items.push_back({c.id, describe(c.price)});
            break;
        case IngredientKind::miscellaneous:
            for (const auto& m : recipe->miscellaneous.value_or(std::vector<model::Miscellaneous>{})) items.push_back({m.id, describe(m.price)});
            break;
        case IngredientKind::water:
            break;
    }
    const Item* target = nullptr;
    if (existing) {
        const auto found = std::find_if(items.begin(), items.end(), [&](const Item& i) { return i.id == *existing; });
        if (found != items.end()) {
            target = &*found;
        }
    }
    if (target == nullptr && !items.empty()) {
        target = &items.back();
    }
    if (target == nullptr) {
        return;
    }
    const std::string amountText = trimmed(price);
    const std::optional<PriceText> wanted =
        amountText.empty() ? std::nullopt : std::optional<PriceText>(PriceText{amountText, priceUnit});
    if (wanted == target->price) {
        return;
    }
    document.setPrice(recipeId, kind, target->id, wanted ? std::optional<std::string>(wanted->amount) : std::nullopt,
                      wanted ? std::optional<std::string>(wanted->per) : std::nullopt);
}

// MARK: Menus

std::vector<MenuEntry> catalogEntries(IngredientKind kind, UnitSystem units) {
    std::vector<MenuEntry> out;
    const auto add = [&](const auto& items) {
        for (const auto& item : items) {
            out.push_back(MenuEntry{item.group + "/" + item.name, item.group, item.name,
                                    IngredientDraft::from(item, units)});
        }
    };
    switch (kind) {
        case IngredientKind::fermentable: add(Catalog::fermentables()); break;
        case IngredientKind::hop: add(Catalog::hops()); break;
        case IngredientKind::culture: add(Catalog::cultures()); break;
        case IngredientKind::water: add(Catalog::waters()); break;
        case IngredientKind::miscellaneous: add(Catalog::miscellaneous()); break;
    }
    return out;
}

std::vector<MenuEntry> menuEntries(IngredientKind kind, const std::vector<CustomIngredient>& custom,
                                   UnitSystem units) {
    std::vector<MenuEntry> out = catalogEntries(kind, units);
    for (const CustomIngredient& item : custom) {
        if (item.draft.kind == kind) {
            out.push_back(MenuEntry{"custom-" + item.id, "Custom", item.draft.name, item.draft});
        }
    }
    return out;
}

// MARK: Custom ingredient store

namespace {

using json::Member;
using json::Value;

const char* const kDraftKeys[] = {"name",      "type",     "form",       "use",         "amount",    "unit",
                                  "alpha",     "beta",     "price",      "priceUnit",   "time",      "timeUnit",
                                  "attenuation", "yieldValue", "yieldUnit", "colorValue", "colorUnit", "calcium",
                                  "bicarbonate", "sulfate", "chloride",   "sodium",      "magnesium", "waterVolume"};

std::string* draftField(IngredientDraft& d, const std::string& key) {
    if (key == "name") return &d.name;
    if (key == "type") return &d.type;
    if (key == "form") return &d.form;
    if (key == "use") return &d.use;
    if (key == "amount") return &d.amount;
    if (key == "unit") return &d.unit;
    if (key == "alpha") return &d.alpha;
    if (key == "beta") return &d.beta;
    if (key == "price") return &d.price;
    if (key == "priceUnit") return &d.priceUnit;
    if (key == "time") return &d.time;
    if (key == "timeUnit") return &d.timeUnit;
    if (key == "attenuation") return &d.attenuation;
    if (key == "yieldValue") return &d.yieldValue;
    if (key == "yieldUnit") return &d.yieldUnit;
    if (key == "colorValue") return &d.colorValue;
    if (key == "colorUnit") return &d.colorUnit;
    if (key == "calcium") return &d.calcium;
    if (key == "bicarbonate") return &d.bicarbonate;
    if (key == "sulfate") return &d.sulfate;
    if (key == "chloride") return &d.chloride;
    if (key == "sodium") return &d.sodium;
    if (key == "magnesium") return &d.magnesium;
    if (key == "waterVolume") return &d.waterVolume;
    return nullptr;
}

Value draftValue(const IngredientDraft& source) {
    IngredientDraft d = source;
    json::Object o;
    o.push_back(Member{"kind", Value::string(ingredientKindName(d.kind))});
    for (const char* key : kDraftKeys) {
        o.push_back(Member{key, Value::string(*draftField(d, key))});
    }
    return Value::object(std::move(o));
}

// Reads a draft saved by any version of the app. A field a saved library entry predates, such as the beta acid or
// the price, keeps the value a new draft starts with.
IngredientDraft draftOf(const Value& v, UnitSystem units) {
    const Value* kindValue = v.find("kind");
    const auto kind = kindValue != nullptr && kindValue->isString() ? ingredientKindFromName(kindValue->asString())
                                                                     : std::nullopt;
    if (!kind) {
        throw std::invalid_argument("a saved ingredient has no kind");
    }
    IngredientDraft d(*kind, units);
    for (const char* key : kDraftKeys) {
        const Value* field = v.find(key);
        if (field != nullptr && field->isString()) {
            *draftField(d, key) = field->asString();
        }
    }
    if (v.find("priceUnit") == nullptr) {
        d.priceUnit = d.unit;
    }
    return d;
}

}  // namespace

CustomIngredientStore::CustomIngredientStore(std::filesystem::path file) : file_(std::move(file)) {
    if (file_.empty()) {
        return;
    }
    std::ifstream in(file_, std::ios::binary);
    if (!in) {
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        const Value root = json::parse(buffer.str());
        if (!root.isArray()) {
            return;
        }
        std::vector<CustomIngredient> loaded;
        for (const Value& item : root.asArray()) {
            const Value* id = item.find("id");
            const Value* draft = item.find("draft");
            if (id == nullptr || !id->isString() || draft == nullptr) {
                throw std::invalid_argument("a saved ingredient has no id");
            }
            loaded.push_back(CustomIngredient{id->asString(), draftOf(*draft, UnitSystem::metric)});
        }
        ingredients_ = std::move(loaded);
    } catch (const std::exception&) {
        ingredients_.clear();  // a file that cannot be read starts the library empty
    }
}

void CustomIngredientStore::save(IngredientDraft draft, const std::optional<std::string>& replacing) {
    draft.name = trimmed(draft.name);
    const auto found = replacing ? std::find_if(ingredients_.begin(), ingredients_.end(),
                                                [&](const CustomIngredient& i) { return i.id == *replacing; })
                                 : ingredients_.end();
    if (found != ingredients_.end()) {
        found->draft = std::move(draft);
    } else {
        ingredients_.push_back(CustomIngredient{newUuidText(), std::move(draft)});
    }
    write();
}

void CustomIngredientStore::duplicate(const std::string& id) {
    const auto found = std::find_if(ingredients_.begin(), ingredients_.end(),
                                    [&](const CustomIngredient& i) { return i.id == id; });
    if (found == ingredients_.end()) {
        return;
    }
    IngredientDraft draft = found->draft;
    draft.name = trimmed(draft.name) + " copy";
    ingredients_.push_back(CustomIngredient{newUuidText(), std::move(draft)});
    write();
}

void CustomIngredientStore::remove(const std::string& id) {
    ingredients_.erase(std::remove_if(ingredients_.begin(), ingredients_.end(),
                                      [&](const CustomIngredient& i) { return i.id == id; }),
                       ingredients_.end());
    write();
}

void CustomIngredientStore::write() const {
    if (file_.empty()) {
        return;
    }
    json::Array items;
    for (const CustomIngredient& item : ingredients_) {
        json::Object o;
        o.push_back(Member{"id", Value::string(item.id)});
        o.push_back(Member{"draft", draftValue(item.draft)});
        items.push_back(Value::object(std::move(o)));
    }
    std::error_code ignored;
    std::filesystem::create_directories(file_.parent_path(), ignored);
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw EngineError("The ingredient library could not be saved.");
    }
    out << json::serialize(Value::array(std::move(items)));
}

// MARK: Hop timing

const std::vector<HopTiming>& allHopTimings() {
    static const std::vector<HopTiming> all = {HopTiming::start,    HopTiming::thirty,  HopTiming::fifteen,
                                               HopTiming::five,     HopTiming::flameout, HopTiming::dryHop,
                                               HopTiming::custom};
    return all;
}

std::string hopTimingTitle(HopTiming timing) {
    switch (timing) {
        case HopTiming::start: return "Start of boil (60 min left)";
        case HopTiming::thirty: return "30 minutes left";
        case HopTiming::fifteen: return "15 minutes left";
        case HopTiming::five: return "5 minutes left";
        case HopTiming::flameout: return "Flameout (end of boil)";
        case HopTiming::dryHop: return "Dry hop (in the fermenter)";
        case HopTiming::custom: return "Custom\xE2\x80\xA6";
    }
    return "";
}

int hopTimingMinutes(HopTiming timing) {
    switch (timing) {
        case HopTiming::start: return 60;
        case HopTiming::thirty: return 30;
        case HopTiming::fifteen: return 15;
        case HopTiming::five: return 5;
        default: return 0;
    }
}

std::optional<HopTimingSetting> hopTimingSetting(HopTiming timing) {
    switch (timing) {
        case HopTiming::start:
        case HopTiming::thirty:
        case HopTiming::fifteen:
        case HopTiming::five:
            return HopTimingSetting{"add_to_boil", std::to_string(hopTimingMinutes(timing)), "min"};
        case HopTiming::flameout: return HopTimingSetting{"add_to_boil", std::string("0"), "min"};
        case HopTiming::dryHop: return HopTimingSetting{"add_to_fermentation", std::nullopt, "day"};
        case HopTiming::custom: return std::nullopt;
    }
    return std::nullopt;
}

namespace {

std::optional<int> wholeNumber(const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty() || t.find_first_not_of("0123456789") != std::string::npos || t.size() > 9) {
        return std::nullopt;
    }
    return std::stoi(t);
}

}  // namespace

HopTiming timingOf(const IngredientDraft& draft) {
    const std::string time = trimmed(draft.time);
    if (draft.use == "add_to_boil" && draft.timeUnit == "min") {
        if (time == "0") {
            return HopTiming::flameout;
        }
        if (const auto minutes = wholeNumber(time)) {
            for (const HopTiming preset : {HopTiming::start, HopTiming::thirty, HopTiming::fifteen, HopTiming::five}) {
                if (hopTimingMinutes(preset) == *minutes) {
                    return preset;
                }
            }
        }
    } else if (draft.use == "add_to_fermentation" && draft.timeUnit == "day") {
        if (wholeNumber(time)) {
            return HopTiming::dryHop;
        }
    }
    return HopTiming::custom;
}

void chooseHopTiming(IngredientDraft& draft, HopTiming choice) {
    const auto setting = hopTimingSetting(choice);
    if (!setting) {
        return;
    }
    // Moving to a dry hop from anything else starts at three days, not at the old boil minutes.
    const bool wasDryHop = draft.use == setting->use && wholeNumber(draft.time).has_value();
    draft.use = setting->use;
    draft.timeUnit = setting->unit;
    if (setting->time) {
        draft.time = *setting->time;
    } else if (!wasDryHop) {
        draft.time = "3";
    }
}

std::string nudgedAmount(const std::string& text, const std::string& unit, int direction) {
    double step = 1;
    if (unit == "g" || unit == "ml") step = 5;
    else if (unit == "oz" || unit == "kg") step = 0.25;
    else if (unit == "lb") step = 0.5;
    const double current = parseNumber(text).value_or(0);
    const double next = std::max(0.0, std::round((current + direction * step) * 1000) / 1000);
    return number(next, 0, 3);
}

}  // namespace kettlelogic::app
