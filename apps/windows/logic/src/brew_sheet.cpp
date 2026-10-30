#include "kettlelogic/app/brew_sheet.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace kettlelogic::app {

namespace {

std::optional<double> numberAt(const calc::BrewSheetValues& values, const std::string& code) {
    const auto found = values.numbers.find(code);
    if (found == values.numbers.end()) return std::nullopt;
    return found->second;
}

// The date in a box, as a moment in the planning's reference seconds, at midday on that day.
std::optional<double> dateAt(const calc::BrewSheetValues& values, const std::string& code) {
    const auto found = values.dates.find(code);
    if (found == values.dates.end()) return std::nullopt;
    CivilDate day;
    if (std::sscanf(found->second.c_str(), "%d-%d-%d", &day.year, &day.month, &day.day) != 3) return std::nullopt;
    return referenceSecondsAtMidday(day);
}

}  // namespace

calc::BrewSheet brewSheetFor(const model::Recipe& recipe, const calc::Results& results, const Units& units) {
    return calc::brewSheet(recipe, results, units.metric() ? calc::SheetUnits::Metric : calc::SheetUnits::Imperial);
}

void applyBrewSheet(BrewSession& session, const calc::BrewSheetValues& values) {
    if (const auto day = dateAt(values, "A1")) session.brewDate = *day;

    if (const auto v = numberAt(values, "B1")) session.mashPh = *v;
    if (const auto v = numberAt(values, "B2")) session.spargeRunoffPh = *v;
    if (const auto v = numberAt(values, "B3")) session.endOfRunningsGravity = *v;

    if (const auto v = numberAt(values, "C1")) session.preBoilLiters = *v;
    if (const auto v = numberAt(values, "C2")) session.preBoilGravity = *v;

    if (const auto v = numberAt(values, "D1")) session.batchLiters = *v;
    if (const auto v = numberAt(values, "D2")) session.originalGravity = *v;

    if (const auto v = numberAt(values, "E1")) session.finalGravity = *v;
    if (const auto v = numberAt(values, "E2")) session.bottlingLiters = *v;

    // Reading row k is codes 3k-2 (date), 3k-1 (temperature), and 3k (gravity).
    std::vector<FermentationReading> readings;
    for (int row = 1; row <= calc::kReadingRows; ++row) {
        const std::string date = "F" + std::to_string(3 * row - 2);
        const std::string temperature = "F" + std::to_string(3 * row - 1);
        const std::string gravity = "F" + std::to_string(3 * row);
        const std::optional<double> day = dateAt(values, date);
        const std::optional<double> temperatureC = numberAt(values, temperature);
        const std::optional<double> specificGravity = numberAt(values, gravity);
        if (!day && !temperatureC && !specificGravity) continue;

        FermentationReading reading;
        reading.date = day.value_or(session.brewDate);
        reading.temperatureC = temperatureC;
        reading.gravity = specificGravity;
        readings.push_back(reading);
    }
    if (!readings.empty()) session.readings = readings;
}

}  // namespace kettlelogic::app
