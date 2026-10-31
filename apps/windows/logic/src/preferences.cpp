#include "kettlelogic/app/preferences.hpp"

#include "kettlelogic/app/engine.hpp"
#include "kettlelogic/app/equipment.hpp"
#include "kettlelogic/app/text.hpp"
#include "kettlelogic/json.hpp"

#include <fstream>
#include <sstream>

namespace kettlelogic::app {

std::string defaultPreference(const std::string& key) {
    if (key == PrefKey::simpleMode) return "true";
    if (key == PrefKey::interfaceChosen) return "false";
    if (key == PrefKey::groupedTabs) return "true";  // as on macOS
    if (key == PrefKey::autosave) return "true";
    if (key == PrefKey::confirmDelete) return "true";
    if (key == PrefKey::units) return "metric";
    if (key == PrefKey::defaultEquipment) return Equipment::builtIn()[0].name;
    if (key == PrefKey::recipeFolder) return "";
    if (key == PrefKey::keepDisplayOn) return "true";
    if (key == PrefKey::checkForUpdates) return "true";
    return "";
}

Preferences::Preferences(std::filesystem::path file) : file_(std::move(file)) {
    load();
}

void Preferences::load() {
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
        if (!root.isObject()) {
            return;
        }
        for (const json::Member& member : root.asObject()) {
            if (member.value.isString()) {
                values_[member.key] = member.value.asString();
            } else if (member.value.isBool()) {
                values_[member.key] = member.value.asBool() ? "true" : "false";
            }
        }
    } catch (const std::exception&) {
        values_.clear();  // a file that cannot be read starts the settings at their defaults
    }
}

void Preferences::write() const {
    if (file_.empty()) {
        return;
    }
    json::Object object;
    for (const auto& [key, value] : values_) {
        if (value == "true" || value == "false") {
            object.push_back(json::Member{key, json::Value::boolean(value == "true")});
        } else {
            object.push_back(json::Member{key, json::Value::string(value)});
        }
    }
    std::error_code ignored;
    std::filesystem::create_directories(file_.parent_path(), ignored);
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    out << json::serialize(json::Value::object(std::move(object)));
}

std::string Preferences::string(const std::string& key) const {
    const auto found = values_.find(key);
    return found != values_.end() ? found->second : defaultPreference(key);
}

bool Preferences::boolean(const std::string& key) const {
    return string(key) == "true";
}

void Preferences::setString(const std::string& key, const std::string& value) {
    values_[key] = value;
    write();
}

void Preferences::setBoolean(const std::string& key, bool value) {
    setString(key, value ? "true" : "false");
}

UnitSystem Preferences::units() const {
    return string(PrefKey::units) == "us" ? UnitSystem::us : UnitSystem::metric;
}

std::string Preferences::defaultEquipmentName() const {
    return string(PrefKey::defaultEquipment);
}

std::string Units::temperatureText(double celsius) const {
    return number(displayTemperature(celsius), 0, 1) + " " + temperatureSymbol();
}

std::string Units::litersTextFromStarter(const std::string& text) const {
    if (metric()) {
        return text;
    }
    const auto value = parseNumber(text);
    return value ? fixed(*value / quartsPerLiter, 4) : text;
}

std::string Units::starterTextFromLiters(const std::string& text) const {
    if (metric()) {
        return text;
    }
    const auto value = parseNumber(text);
    return value ? fixed(*value * quartsPerLiter, 2) : text;
}

std::string Units::litersTextFromVolume(const std::string& text) const {
    if (metric()) {
        return text;
    }
    const auto value = parseNumber(text);
    return value ? fixed(*value * litersPerGallon, 3) : text;
}

std::string Units::volumeTextFromLiters(const std::string& text) const {
    if (metric()) {
        return text;
    }
    const auto value = parseNumber(text);
    return value ? fixed(*value / litersPerGallon, 2) : text;
}

}  // namespace kettlelogic::app
