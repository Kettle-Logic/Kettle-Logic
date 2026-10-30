#pragma once

#include "kettlelogic/app/planning.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Water profiles saved from the Water Profile tool, to load into the Water tab of any recipe. A saved profile
// keeps its salts as grams per liter, so each recipe scales them to its own mash water.
namespace kettlelogic::app {

// A salt in a saved profile: its kind, and its grams for each liter of water.
struct SavedSalt {
    std::string kind;
    double gramsPerLiter = 0;
};

// A water profile saved from the Water Profile tool.
struct SavedWaterProfile {
    std::string id = newUuidText();
    std::string name;
    IonsInput start;                    // the start water in mg/L, before the reverse osmosis share
    double roPercent = 0;               // the share of reverse osmosis water, in percent
    std::vector<SavedSalt> salts;
    std::optional<IonsInput> target;    // when the tool was comparing with one

    // Reads a profile from the Water Profile tool's inputs, as they were typed. Nothing when the volume is not above
    // zero, because the salts are kept per liter.
    static std::optional<SavedWaterProfile> fromTool(const std::string& name,
                                                     const std::map<std::string, std::string>& inputs);

    // Sets a recipe's water to this profile. The salts are scaled to the recipe's mash water, and the profile's
    // target replaces the recipe's only when the profile has one.
    void apply(WaterSettings& water, double mashLiters) const;
};

// The salts the Water Profile tool adds: the tool's input name, and the engine's name for the salt.
struct WaterProfileSalt {
    const char* input;
    const char* kind;
};
const std::vector<WaterProfileSalt>& waterProfileSalts();

// The water profiles the user saved, kept as JSON in the app's data folder. A file that cannot be read starts the
// library empty, and the next save replaces it.
class WaterProfileLibrary {
public:
    explicit WaterProfileLibrary(std::filesystem::path file = {});

    const std::vector<SavedWaterProfile>& profiles() const { return profiles_; }

    // Saves a profile as new, or replaces the saved profile with the same name, so saving again updates it.
    void save(SavedWaterProfile profile);
    void remove(const std::string& id);

private:
    void write() const;

    std::vector<SavedWaterProfile> profiles_;
    std::filesystem::path file_;
};

}  // namespace kettlelogic::app
