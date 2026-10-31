#pragma once

#include "kettlelogic/app/preferences.hpp"
#include "kettlelogic/catalog/equipment.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kettlelogic::app {

// The brewing system type and the built-in systems live in the engine (`catalog/equipment.hpp`), so every
// front end offers the same ones.
using catalog::EquipmentProfile;

// The starting figures on one line, with the batch in the unit system the user chose.
std::string summary(const EquipmentProfile& profile, UnitSystem units);

namespace Equipment {
inline const std::vector<EquipmentProfile>& builtIn() { return catalog::builtInEquipment(); }
}

// The brewing systems the user made, kept as JSON in the app's data folder. A file that cannot be read
// starts the library empty, and the next save replaces it. With the built-in systems, these are the
// choices a recipe and the settings offer.
class EquipmentLibrary {
public:
    // `file` is where the custom systems are kept. An empty path keeps them in memory only.
    explicit EquipmentLibrary(std::filesystem::path file = {});

    const std::vector<EquipmentProfile>& custom() const { return custom_; }

    // Every brewing system: the built-in ones, then the user's own.
    std::vector<EquipmentProfile> all() const;

    // The system with `name`, or the first built-in system when no system has that name, such as after a
    // custom one is deleted.
    EquipmentProfile profile(const std::string& name) const;

    // Saves a system as a new one, or as the change to the custom system called `replacing`. Each name is
    // used by one system only, so a name that another system has is refused (throws EngineError).
    void save(EquipmentProfile profile, const std::optional<std::string>& replacing);

    // Adds a copy of the custom system called `name`, named after it with " copy" added.
    void duplicate(const std::string& name);
    void remove(const std::string& name);

private:
    std::string uniqueName(const std::string& base) const;
    void write() const;

    std::vector<EquipmentProfile> custom_;
    std::filesystem::path file_;
};

}  // namespace kettlelogic::app
