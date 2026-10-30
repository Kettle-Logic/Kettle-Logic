#pragma once

#include <optional>
#include <string>
#include <vector>

// The built-in ingredients that prefill the front ends' add dialogs: malts, sugars, hops, yeasts and
// other cultures, brewing waters, and miscellaneous additions. The figures are typical, not exact: hop
// alpha acid changes with every crop, malt color and yield change with the maltster's lot, and yeast
// attenuation depends on the wort. A dialog shows the values before they are added, so the brewer can
// check them against the packet or the lab sheet. An attenuation is left blank where there is no widely
// agreed figure, because the first culture with an attenuation sets the final gravity.
//
// Water salts, sulfite, and tartaric acid are not here. The engine only counts them when they are
// tagged as a salt or an additive, and the front ends cannot set those tags yet.
//
// Values are decimal text, as a user would type them, so they pass through the add commands unchanged.
// Entries are in menu order, and `group` is the heading an entry sits under. Every front end reads this
// one table, so all of them offer the same entries.
namespace kettlelogic::catalog {

struct FermentableTemplate {
    std::string name;
    std::string group;
    std::string type;      // a BeerJSON fermentable type
    std::string yieldSG;   // the potential, in specific gravity
    std::string colorSRM;
};

struct HopTemplate {
    std::string name;
    std::string group;
    std::string alphaPercent;
    std::optional<std::string> betaPercent;  // a typical mid-range figure, like the alpha acid
};

struct CultureTemplate {
    std::string name;
    std::string group;
    std::string type;  // a BeerJSON culture type
    std::string form;  // a BeerJSON culture form
    std::optional<std::string> attenuationPercent;
};

struct WaterTemplate {
    std::string name;
    std::string group;
    // Calcium, bicarbonate, sulfate, chloride, sodium, and magnesium, in mg/L.
    std::string calcium;
    std::string bicarbonate;
    std::string sulfate;
    std::string chloride;
    std::string sodium;
    std::string magnesium;
};

struct MiscellaneousTemplate {
    std::string name;
    std::string group;
    std::string type;  // a BeerJSON miscellaneous type
    std::string unit;
};

const std::vector<FermentableTemplate>& fermentables();
const std::vector<HopTemplate>& hops();
const std::vector<CultureTemplate>& cultures();
const std::vector<WaterTemplate>& waters();
const std::vector<MiscellaneousTemplate>& miscellaneous();

}  // namespace kettlelogic::catalog
