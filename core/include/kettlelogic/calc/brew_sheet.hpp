#pragma once

#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/model/model.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

// The brew sheet: a printed page the brewer fills in with a pencil on brew day, and the values read back from it.
//
// Every box has a code made of a section letter and a number, such as "B1" for the measured mash pH. The codes are
// the contract between the printed sheet, the entry screens on each platform, and the session they fill, so a sheet
// written on paper goes back into the same session whichever app reads it. The layout is fixed, so the codes never
// move.
//
// A sheet is in metric units (liters, degrees C) or imperial units (gallons, degrees F). The values read back are
// always in SI, which is the engine's canonical unit: liters, degrees C, specific gravity, and pH.
namespace kettlelogic::calc {

enum class SheetUnits { Metric, Imperial };

enum class BoxKind { Date, Volume, Temperature, Gravity, Ph };

struct BrewSheetBox {
    std::string code;                 // "A1", "F18"
    std::string label;                // "Measured mash pH"
    BoxKind kind = BoxKind::Gravity;
    std::string unit;                 // "SG", "pH", "L" or "gal", "°C" or "°F", "yyyy-mm-dd"
    std::optional<double> expected;   // the recipe's figure, in the sheet's units, when it has one
    std::string expectedText;         // the same figure as the sheet prints it; empty when there is none
};

struct BrewSheetSection {
    std::string code;   // "A"
    std::string title;  // "Brew day"
    std::vector<BrewSheetBox> boxes;
};

struct BrewSheet {
    std::string recipeName;
    std::string batchText;  // the expected batch size with its unit, such as "20.00 L"
    SheetUnits units = SheetUnits::Metric;
    std::vector<BrewSheetSection> sections;

    // The box with this code, or nothing when the sheet has no such box.
    const BrewSheetBox* find(const std::string& code) const;
};

// The fermentation readings table has this many rows. Row k (from 1) has three boxes: its date, its temperature,
// and its gravity, which are codes 3k-2, 3k-1, and 3k in section F.
inline constexpr int kReadingRows = 6;

// Lays out the sheet for a recipe. `results` must be the recipe's calculated results.
BrewSheet brewSheet(const model::Recipe& recipe, const Results& results, SheetUnits units);

struct BrewSheetIssue {
    std::string code;
    std::string message;
};

// The values read back from a sheet. Numbers are in SI. Dates stay as yyyy-MM-dd text, which the caller converts.
struct BrewSheetValues {
    std::map<std::string, double> numbers;
    std::map<std::string, std::string> dates;
    std::vector<BrewSheetIssue> issues;  // the boxes that could not be read; the others are still kept
};

// Reads the text written in each box. `typed` maps a code to the text in its box. Empty boxes are left out. A box
// that holds text the sheet cannot take, or a value outside the range the sheet accepts, gives an issue instead.
BrewSheetValues readBrewSheet(const BrewSheet& sheet, const std::map<std::string, std::string>& typed);

}  // namespace kettlelogic::calc
