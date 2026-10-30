#pragma once

#include "kettlelogic/model/model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A brew-day procedure, generated from a recipe, the boil length the user gives,
// and the brewing system. The steps use the recipe's own figures (strike
// temperatures, volumes, expected gravities), so a missing figure is left out
// rather than guessed.
namespace kettlelogic::calc {

// A separate tun is a mash tun beside the kettle, such as a cooler. All in one
// mashes and boils in the same vessel.
enum class MashSystem { SeparateTun, AllInOne };

// The system named "separate tun" or "all in one". Throws std::invalid_argument otherwise.
MashSystem mashSystemFromName(std::string_view name);

struct ProcedureStep {
    std::string phase;                 // "Mash", "Boil", "Chill", and so on
    std::optional<long long> minute;   // minutes into the boil, for boil steps
    std::string text;
    bool warning = false;              // something the brewer must fix before the step can happen
    std::optional<long long> timerMinutes;  // how long the step runs, for a step that needs a timer
};

// Throws std::invalid_argument if boilMinutes is not from 1 to 600.
std::vector<ProcedureStep> brewDayProcedure(const model::Recipe& recipe, long long boilMinutes, MashSystem system);

}  // namespace kettlelogic::calc
