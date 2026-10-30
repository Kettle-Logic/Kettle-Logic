#include "kettlelogic/app/water_profiles.hpp"

#include "kettlelogic/app/engine.hpp"
#include "kettlelogic/app/text.hpp"
#include "kettlelogic/json.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace kettlelogic::app {

using json::Member;
using json::Object;
using json::Value;

const std::vector<WaterProfileSalt>& waterProfileSalts() {
    static const std::vector<WaterProfileSalt> salts = {
        {"gypsum", "gypsum"},
        {"calciumChloride", "calcium chloride"},
        {"epsomSalt", "epsom salt"},
        {"magnesiumChloride", "magnesium chloride"},
        {"tableSalt", "table salt"},
        {"bakingSoda", "baking soda"},
        {"chalk", "chalk"},
        {"picklingLime", "pickling lime"},
    };
    return salts;
}

std::optional<SavedWaterProfile> SavedWaterProfile::fromTool(const std::string& name,
                                                             const std::map<std::string, std::string>& inputs) {
    const auto number = [&](const std::string& key) {
        const auto found = inputs.find(key);
        return found == inputs.end() ? 0.0 : parseNumber(found->second).value_or(0.0);
    };
    const double liters = number("volume");
    if (!(liters > 0)) {
        return std::nullopt;
    }
    SavedWaterProfile profile;
    profile.name = name;
    profile.start = IonsInput{number("calcium"), number("magnesium"), number("sodium"),
                              number("sulfate"), number("chloride"),  number("bicarbonate")};
    profile.roPercent = number("roPercent");
    for (const WaterProfileSalt& salt : waterProfileSalts()) {
        const double grams = number(salt.input);
        if (grams > 0) {
            profile.salts.push_back(SavedSalt{salt.kind, grams / liters});
        }
    }
    const auto useTarget = inputs.find("useTarget");
    if (useTarget != inputs.end() && useTarget->second == "1") {
        profile.target = IonsInput{number("targetCalcium"), number("targetMagnesium"), number("targetSodium"),
                                   number("targetSulfate"), number("targetChloride"),  number("targetBicarbonate")};
    }
    return profile;
}

void SavedWaterProfile::apply(WaterSettings& water, double mashLiters) const {
    water.baseName = "Custom";
    water.base = start;
    water.roPercent = roPercent;
    water.leaveOutSalts = false;
    water.salts.clear();
    for (const SavedSalt& salt : salts) {
        SaltInput input;
        input.kind = salt.kind;
        input.grams = salt.gramsPerLiter * mashLiters;
        water.salts.push_back(input);
    }
    if (target) {
        water.useTarget = true;
        water.target = *target;
    }
}

namespace {

Value num(double value) {
    char buffer[40];
    for (int digits = 6; digits <= 17; ++digits) {
        std::snprintf(buffer, sizeof buffer, "%.*g", digits, value);
        if (std::strtod(buffer, nullptr) == value) {
            break;
        }
    }
    return Value::number(buffer);
}

Value ionsValue(const IonsInput& ions) {
    Object o;
    o.push_back(Member{"calcium", num(ions.calcium)});
    o.push_back(Member{"magnesium", num(ions.magnesium)});
    o.push_back(Member{"sodium", num(ions.sodium)});
    o.push_back(Member{"sulfate", num(ions.sulfate)});
    o.push_back(Member{"chloride", num(ions.chloride)});
    o.push_back(Member{"bicarbonate", num(ions.bicarbonate)});
    return Value::object(std::move(o));
}

double numberOf(const Value& object, const char* key) {
    const Value* value = object.find(key);
    if (value == nullptr || !value->isNumber()) {
        throw std::invalid_argument("a saved profile is missing a number");
    }
    return value->asRational().toDouble();
}

IonsInput ionsOf(const Value& object) {
    if (!object.isObject()) {
        throw std::invalid_argument("a saved profile is missing its ions");
    }
    return IonsInput{numberOf(object, "calcium"),  numberOf(object, "magnesium"), numberOf(object, "sodium"),
                     numberOf(object, "sulfate"), numberOf(object, "chloride"),   numberOf(object, "bicarbonate")};
}

SavedWaterProfile profileOf(const Value& v) {
    SavedWaterProfile profile;
    const Value* id = v.find("id");
    const Value* name = v.find("name");
    if (id == nullptr || !id->isString() || name == nullptr || !name->isString()) {
        throw std::invalid_argument("a saved profile has no name");
    }
    profile.id = id->asString();
    profile.name = name->asString();
    profile.start = ionsOf(*v.find("start"));
    profile.roPercent = numberOf(v, "roPercent");
    const Value* salts = v.find("salts");
    if (salts == nullptr || !salts->isArray()) {
        throw std::invalid_argument("a saved profile has no salts");
    }
    for (const Value& salt : salts->asArray()) {
        const Value* kind = salt.find("kind");
        if (kind == nullptr || !kind->isString()) {
            throw std::invalid_argument("a saved salt has no kind");
        }
        profile.salts.push_back(SavedSalt{kind->asString(), numberOf(salt, "gramsPerLiter")});
    }
    if (const Value* target = v.find("target"); target != nullptr && target->isObject()) {
        profile.target = ionsOf(*target);
    }
    return profile;
}

}  // namespace

WaterProfileLibrary::WaterProfileLibrary(std::filesystem::path file) : file_(std::move(file)) {
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
        std::vector<SavedWaterProfile> loaded;
        for (const Value& item : root.asArray()) {
            loaded.push_back(profileOf(item));
        }
        profiles_ = std::move(loaded);
    } catch (const std::exception&) {
        profiles_.clear();  // a file that cannot be read starts the library empty
    }
}

void WaterProfileLibrary::save(SavedWaterProfile profile) {
    const auto found = std::find_if(profiles_.begin(), profiles_.end(),
                                    [&](const SavedWaterProfile& p) { return p.name == profile.name; });
    if (found != profiles_.end()) {
        profile.id = found->id;
        *found = std::move(profile);
    } else {
        profiles_.push_back(std::move(profile));
    }
    write();
}

void WaterProfileLibrary::remove(const std::string& id) {
    profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(),
                                   [&](const SavedWaterProfile& p) { return p.id == id; }),
                    profiles_.end());
    write();
}

void WaterProfileLibrary::write() const {
    if (file_.empty()) {
        return;
    }
    json::Array items;
    for (const SavedWaterProfile& p : profiles_) {
        Object o;
        o.push_back(Member{"id", Value::string(p.id)});
        o.push_back(Member{"name", Value::string(p.name)});
        o.push_back(Member{"start", ionsValue(p.start)});
        o.push_back(Member{"roPercent", num(p.roPercent)});
        json::Array salts;
        for (const SavedSalt& salt : p.salts) {
            Object s;
            s.push_back(Member{"kind", Value::string(salt.kind)});
            s.push_back(Member{"gramsPerLiter", num(salt.gramsPerLiter)});
            salts.push_back(Value::object(std::move(s)));
        }
        o.push_back(Member{"salts", Value::array(std::move(salts))});
        if (p.target) {
            o.push_back(Member{"target", ionsValue(*p.target)});
        }
        items.push_back(Value::object(std::move(o)));
    }
    std::error_code ignored;
    std::filesystem::create_directories(file_.parent_path(), ignored);
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw EngineError("The water profile library could not be saved.");
    }
    out << json::serialize(Value::array(std::move(items)));
}

}  // namespace kettlelogic::app
