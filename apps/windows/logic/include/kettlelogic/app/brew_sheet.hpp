#pragma once

#include "kettlelogic/app/planning.hpp"
#include "kettlelogic/app/preferences.hpp"
#include "kettlelogic/calc/brew_sheet.hpp"

// The brew sheet as the app uses it: the sheet for a recipe in the user's units, and the values read back from a
// printed sheet put into a brew session. The codes are the engine's (calc/brew_sheet.hpp); this file only says which
// session field each code fills, so the macOS and Windows and Linux apps read the same sheet the same way.
namespace kettlelogic::app {

// The sheet for a recipe, in the units the user has chosen.
calc::BrewSheet brewSheetFor(const model::Recipe& recipe, const calc::Results& results, const Units& units);

// Puts the values read from a sheet into a session. A value on the sheet replaces the session's value, and an empty
// box keeps it. When the sheet has any readings, they replace the session's readings. A reading with no date takes
// the brew day.
void applyBrewSheet(BrewSession& session, const calc::BrewSheetValues& values);

}  // namespace kettlelogic::app
