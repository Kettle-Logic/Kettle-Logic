#pragma once

#include <string>
#include <vector>

namespace kettlelogic::catalog {

// A brewing system, with typical starting figures. The batch size, efficiency, and boil-off depend on the
// kettle, the grain bed, and how hard the boil runs, so measure your own system and change the recipe's
// figures to match. The figures are decimal text, as a user would type them.
struct EquipmentProfile {
    std::string name;
    std::string batchLiters;
    std::string efficiencyPercent;
    std::string evaporationPercent;
    std::string trubLossLiters;
    int boilMinutes = 60;
    std::string mashSystem;  // "separate tun" or "all in one", as the engine names the mash system
    std::string note;        // where the figures come from
};

// The built-in brewing systems, in menu order. The first is the default.
const std::vector<EquipmentProfile>& builtInEquipment();

}  // namespace kettlelogic::catalog
