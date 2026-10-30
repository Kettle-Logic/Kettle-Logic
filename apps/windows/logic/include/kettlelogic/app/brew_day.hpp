#pragma once

#include "kettlelogic/app/preferences.hpp"
#include "kettlelogic/calc/procedure.hpp"

#include <set>
#include <string>
#include <vector>

namespace kettlelogic::app {

// What a brew-day window shows: one generated procedure, with its recipe and boil length. Each generated procedure
// has its own id, so generating again opens a new window.
struct BrewDayPlan {
    std::string id;
    std::string recipeName;
    int boilMinutes = 60;
    std::vector<calc::ProcedureStep> steps;

    // Where this procedure's ticked steps are kept. It comes from the recipe name and the steps, not from `id`, so
    // reopening the same procedure finds its progress, and a changed recipe starts clean. FNV-1a, as on the Mac.
    std::string progressKey() const;

    // The steps ticked off so far, kept in the settings under the progress key.
    std::set<int> loadProgress(const Preferences& preferences) const;
    void saveProgress(Preferences& preferences, const std::set<int>& done) const;
};

// A countdown for a step that runs for a set time, as the brew-day window shows it.
// "m:ss", rounded up so a paused timer never reads 0:00 while time remains.
std::string clockText(double seconds);

}  // namespace kettlelogic::app
