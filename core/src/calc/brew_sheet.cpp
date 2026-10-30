#include "kettlelogic/calc/brew_sheet.hpp"

#include "kettlelogic/calc/journal.hpp"

#include <cstdio>
#include <locale>
#include <sstream>
#include <utility>

namespace kettlelogic::calc {

namespace {

constexpr double kLitersPerGallon = 3.785411784;

// Ranges the sheet accepts, in SI. A value outside them is almost always a typo, such as a gravity of 1050.
constexpr double kMinGravity = 0.990;
constexpr double kMaxGravity = 1.200;
constexpr double kMinPh = 0;
constexpr double kMaxPh = 14;
constexpr double kMinTempC = -20;
constexpr double kMaxTempC = 120;
constexpr double kMaxLiters = 10000;

std::string formatted(double value, int places) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
    return buffer;
}

BrewSheetBox box(std::string code, std::string label, BoxKind kind, std::string unit) {
    BrewSheetBox b;
    b.code = std::move(code);
    b.label = std::move(label);
    b.kind = kind;
    b.unit = std::move(unit);
    return b;
}

// A box with the recipe's figure beside it. The figure is in the sheet's units already.
BrewSheetBox expecting(BrewSheetBox b, std::optional<double> figure, int places) {
    if (figure) {
        b.expected = figure;
        b.expectedText = formatted(*figure, places);
    }
    return b;
}

// Trims spaces, tabs, and line ends from both ends.
std::string trimmedText(const std::string& text) {
    const std::string spaces = " \t\r\n";
    const std::size_t first = text.find_first_not_of(spaces);
    if (first == std::string::npos) return "";
    const std::size_t last = text.find_last_not_of(spaces);
    return text.substr(first, last - first + 1);
}

// A decimal number, with a comma or a period. Anything else in the text, such as units or letters, is refused.
std::optional<double> parseDecimal(std::string text) {
    for (char& c : text) {
        if (c == ',') c = '.';
    }
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    double value = 0;
    in >> value;
    if (!in) return std::nullopt;
    in >> std::ws;
    if (!in.eof()) return std::nullopt;
    return value;
}

// yyyy-MM-dd, with the month and day in range. The year is not checked against the calendar beyond that.
bool isIsoDate(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (text[i] < '0' || text[i] > '9') return false;
    }
    const int month = std::stoi(text.substr(5, 2));
    const int day = std::stoi(text.substr(8, 2));
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

}  // namespace

const BrewSheetBox* BrewSheet::find(const std::string& code) const {
    for (const BrewSheetSection& section : sections) {
        for (const BrewSheetBox& b : section.boxes) {
            if (b.code == code) return &b;
        }
    }
    return nullptr;
}

BrewSheet brewSheet(const model::Recipe& recipe, const Results& results, SheetUnits units) {
    const bool imperial = units == SheetUnits::Imperial;
    const std::string volumeUnit = imperial ? "gal" : "L";
    const std::string temperatureUnit = imperial ? "°F" : "°C";
    const auto volume = [imperial](std::optional<double> liters) -> std::optional<double> {
        if (!liters) return std::nullopt;
        return imperial ? *liters / kLitersPerGallon : *liters;
    };

    // The journal's expected pre-boil gravity comes from the recipe alone, so no measurement is passed in.
    const JournalResults plan = planJournal(recipe, JournalInputs{});
    const std::optional<double> batchLiters = recipe.batchSize.canonical().toDouble();

    BrewSheet sheet;
    sheet.recipeName = recipe.name;
    sheet.units = units;
    if (batchLiters) {
        sheet.batchText = formatted(*volume(batchLiters), 2) + " " + volumeUnit;
    }

    BrewSheetSection brewDay{"A", "Brew day", {}};
    brewDay.boxes.push_back(box("A1", "Brew day", BoxKind::Date, "yyyy-mm-dd"));
    sheet.sections.push_back(brewDay);

    BrewSheetSection mash{"B", "Mash and runnings", {}};
    mash.boxes.push_back(box("B1", "Measured mash pH", BoxKind::Ph, "pH"));
    mash.boxes.push_back(box("B2", "Sparge runoff pH", BoxKind::Ph, "pH"));
    mash.boxes.push_back(box("B3", "End of runnings gravity", BoxKind::Gravity, "SG"));
    sheet.sections.push_back(mash);

    BrewSheetSection boiler{"C", "Boiler", {}};
    boiler.boxes.push_back(expecting(box("C1", "Pre-boil volume", BoxKind::Volume, volumeUnit),
                                     volume(results.preBoilLiters), 2));
    boiler.boxes.push_back(expecting(box("C2", "Pre-boil gravity", BoxKind::Gravity, "SG"),
                                     plan.expectedPreBoilGravity, 3));
    sheet.sections.push_back(boiler);

    BrewSheetSection fermenter{"D", "Into the fermenter", {}};
    fermenter.boxes.push_back(expecting(box("D1", "Batch size", BoxKind::Volume, volumeUnit),
                                        volume(batchLiters), 2));
    fermenter.boxes.push_back(expecting(box("D2", "Original gravity", BoxKind::Gravity, "SG"),
                                        results.originalGravity, 3));
    sheet.sections.push_back(fermenter);

    BrewSheetSection packaging{"E", "At packaging", {}};
    packaging.boxes.push_back(expecting(box("E1", "Final gravity", BoxKind::Gravity, "SG"),
                                        results.finalGravity, 3));
    packaging.boxes.push_back(expecting(box("E2", "Bottling volume", BoxKind::Volume, volumeUnit),
                                        volume(batchLiters), 2));
    sheet.sections.push_back(packaging);

    BrewSheetSection readings{"F", "Fermentation readings", {}};
    for (int row = 1; row <= kReadingRows; ++row) {
        const std::string name = "Reading " + std::to_string(row) + " ";
        const int first = 3 * row - 2;
        readings.boxes.push_back(box("F" + std::to_string(first), name + "date", BoxKind::Date, "yyyy-mm-dd"));
        readings.boxes.push_back(box("F" + std::to_string(first + 1), name + "temperature", BoxKind::Temperature,
                                     temperatureUnit));
        readings.boxes.push_back(box("F" + std::to_string(first + 2), name + "gravity", BoxKind::Gravity, "SG"));
    }
    sheet.sections.push_back(readings);

    return sheet;
}

BrewSheetValues readBrewSheet(const BrewSheet& sheet, const std::map<std::string, std::string>& typed) {
    BrewSheetValues values;
    const bool imperial = sheet.units == SheetUnits::Imperial;

    for (const auto& [code, text] : typed) {
        const std::string value = trimmedText(text);
        if (value.empty()) continue;

        const BrewSheetBox* b = sheet.find(code);
        if (b == nullptr) {
            values.issues.push_back({code, "There is no box " + code + " on this sheet."});
            continue;
        }
        if (b->kind == BoxKind::Date) {
            if (isIsoDate(value)) {
                values.dates[code] = value;
            } else {
                values.issues.push_back({code, "Write the date as yyyy-mm-dd."});
            }
            continue;
        }

        const std::optional<double> typedNumber = parseDecimal(value);
        if (!typedNumber) {
            values.issues.push_back({code, "Write a number, such as 1.052."});
            continue;
        }

        double si = *typedNumber;
        bool inRange = true;
        switch (b->kind) {
            case BoxKind::Volume:
                if (imperial) si *= kLitersPerGallon;
                inRange = si >= 0 && si <= kMaxLiters;
                break;
            case BoxKind::Temperature:
                if (imperial) si = (si - 32) * 5 / 9;
                inRange = si >= kMinTempC && si <= kMaxTempC;
                break;
            case BoxKind::Gravity:
                inRange = si >= kMinGravity && si <= kMaxGravity;
                break;
            case BoxKind::Ph:
                inRange = si >= kMinPh && si <= kMaxPh;
                break;
            case BoxKind::Date:
                break;
        }
        if (!inRange) {
            values.issues.push_back({code, "This value is outside the range the sheet accepts for " + b->label + "."});
            continue;
        }
        values.numbers[code] = si;
    }
    return values;
}

}  // namespace kettlelogic::calc
