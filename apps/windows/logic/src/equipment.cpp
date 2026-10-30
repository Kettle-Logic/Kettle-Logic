#include "kettlelogic/app/equipment.hpp"

#include "kettlelogic/app/engine.hpp"
#include "kettlelogic/app/text.hpp"
#include "kettlelogic/json.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace kettlelogic::app {

std::string summary(const EquipmentProfile& p, UnitSystem system) {
    const Units units{system};
    return units.volumeTextFromLiters(p.batchLiters) + " " + units.volumeSymbol() + " batch \xC2\xB7 " +
           p.efficiencyPercent + "% efficiency \xC2\xB7 " + p.evaporationPercent + "% boil-off per hour \xC2\xB7 " +
           p.mashSystem;
}

namespace {

json::Value toJson(const EquipmentProfile& p) {
    json::Object o;
    o.push_back({"name", json::Value::string(p.name)});
    o.push_back({"batchLiters", json::Value::string(p.batchLiters)});
    o.push_back({"efficiencyPercent", json::Value::string(p.efficiencyPercent)});
    o.push_back({"evaporationPercent", json::Value::string(p.evaporationPercent)});
    o.push_back({"trubLossLiters", json::Value::string(p.trubLossLiters)});
    o.push_back({"boilMinutes", json::Value::number(std::to_string(p.boilMinutes))});
    o.push_back({"mashSystem", json::Value::string(p.mashSystem)});
    o.push_back({"note", json::Value::string(p.note)});
    return json::Value::object(std::move(o));
}

std::string text(const json::Value& o, const char* key) {
    const json::Value* v = o.find(key);
    return v != nullptr && v->isString() ? v->asString() : std::string{};
}

std::optional<EquipmentProfile> fromJson(const json::Value& o) {
    if (!o.isObject()) {
        return std::nullopt;
    }
    EquipmentProfile p;
    p.name = text(o, "name");
    p.batchLiters = text(o, "batchLiters");
    p.efficiencyPercent = text(o, "efficiencyPercent");
    p.evaporationPercent = text(o, "evaporationPercent");
    p.trubLossLiters = text(o, "trubLossLiters");
    p.mashSystem = text(o, "mashSystem");
    p.note = text(o, "note");
    if (const json::Value* minutes = o.find("boilMinutes"); minutes != nullptr && minutes->isNumber()) {
        p.boilMinutes = static_cast<int>(minutes->asRational().toDouble());
    }
    return p;
}

}  // namespace

EquipmentLibrary::EquipmentLibrary(std::filesystem::path file) : file_(std::move(file)) {
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
        const json::Value root = json::parse(buffer.str());
        if (!root.isArray()) {
            return;
        }
        std::vector<EquipmentProfile> loaded;
        for (const json::Value& item : root.asArray()) {
            const auto profile = fromJson(item);
            if (!profile) {
                return;  // a file that cannot be read starts the library empty
            }
            loaded.push_back(*profile);
        }
        custom_ = std::move(loaded);
    } catch (const std::exception&) {
        custom_.clear();
    }
}

std::vector<EquipmentProfile> EquipmentLibrary::all() const {
    std::vector<EquipmentProfile> out = Equipment::builtIn();
    out.insert(out.end(), custom_.begin(), custom_.end());
    return out;
}

EquipmentProfile EquipmentLibrary::profile(const std::string& name) const {
    for (const EquipmentProfile& p : all()) {
        if (p.name == name) {
            return p;
        }
    }
    return Equipment::builtIn()[0];
}

void EquipmentLibrary::save(EquipmentProfile profile, const std::optional<std::string>& replacing) {
    profile.name = trimmed(profile.name);
    if (profile.name.empty()) {
        throw EngineError("A brewing system needs a name.");
    }
    for (const EquipmentProfile& p : all()) {
        if (p.name == profile.name && (!replacing || p.name != *replacing)) {
            throw EngineError("A brewing system called \xE2\x80\x9C" + profile.name + "\xE2\x80\x9D already exists.");
        }
    }
    auto existing = replacing ? std::find_if(custom_.begin(), custom_.end(),
                                             [&](const EquipmentProfile& p) { return p.name == *replacing; })
                              : custom_.end();
    if (existing != custom_.end()) {
        *existing = std::move(profile);
    } else {
        custom_.push_back(std::move(profile));
    }
    write();
}

void EquipmentLibrary::duplicate(const std::string& name) {
    const auto found = std::find_if(custom_.begin(), custom_.end(),
                                    [&](const EquipmentProfile& p) { return p.name == name; });
    if (found == custom_.end()) {
        return;
    }
    EquipmentProfile copy = *found;
    copy.name = uniqueName(name + " copy");
    custom_.push_back(std::move(copy));
    write();
}

void EquipmentLibrary::remove(const std::string& name) {
    custom_.erase(std::remove_if(custom_.begin(), custom_.end(),
                                 [&](const EquipmentProfile& p) { return p.name == name; }),
                  custom_.end());
    write();
}

std::string EquipmentLibrary::uniqueName(const std::string& base) const {
    std::set<std::string> taken;
    for (const EquipmentProfile& p : all()) {
        taken.insert(p.name);
    }
    std::string candidate = base;
    int number = 2;
    while (taken.count(candidate) != 0) {
        candidate = base + " " + std::to_string(number++);
    }
    return candidate;
}

void EquipmentLibrary::write() const {
    if (file_.empty()) {
        return;
    }
    json::Array items;
    for (const EquipmentProfile& p : custom_) {
        items.push_back(toJson(p));
    }
    std::error_code ignored;
    std::filesystem::create_directories(file_.parent_path(), ignored);
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw EngineError("The equipment library could not be saved.");
    }
    out << json::serialize(json::Value::array(std::move(items)));
}

}  // namespace kettlelogic::app
