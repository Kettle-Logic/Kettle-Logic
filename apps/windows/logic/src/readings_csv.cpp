#include "kettlelogic/app/readings_csv.hpp"

#include "kettlelogic/app/text.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace kettlelogic::app::ReadingsCsv {

const char* const header = "date,day,temperature_c,gravity_sg";

namespace {

// Swift's String(Double): the shortest text that reads back as the same number.
std::string shortest(double value) {
    char buffer[40];
    for (int digits = 1; digits <= 17; ++digits) {
        std::snprintf(buffer, sizeof buffer, "%.*g", digits, value);
        if (std::strtod(buffer, nullptr) == value) {
            break;
        }
    }
    return buffer;
}

std::string dateText(double referenceSeconds) {
    const CivilDate d = civilDateOf(referenceSeconds);
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", d.year, d.month, d.day);
    return buffer;
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (const char c : line) {
        if (c == ',') {
            fields.push_back(trimmed(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(trimmed(current));
    return fields;
}

std::optional<double> dateFrom(const std::string& text) {
    int parts[3];
    int count = 0;
    std::string current;
    const auto flush = [&]() -> bool {
        if (count == 3 || current.empty() || current.find_first_not_of("0123456789") != std::string::npos) {
            return false;
        }
        parts[count++] = std::stoi(current);
        current.clear();
        return true;
    };
    for (const char c : text) {
        if (c == '-') {
            if (!flush()) return std::nullopt;
        } else {
            current.push_back(c);
        }
    }
    if (!flush() || count != 3) {
        return std::nullopt;
    }
    return referenceSecondsAtMidday({parts[0], parts[1], parts[2]});
}

}  // namespace

std::string exportReadings(std::vector<FermentationReading> readings, double brewDate) {
    std::stable_sort(readings.begin(), readings.end(),
                     [](const FermentationReading& a, const FermentationReading& b) { return a.date < b.date; });
    std::string out = std::string(header) + "\n";
    for (const FermentationReading& r : readings) {
        out += dateText(r.date) + "," + std::to_string(daysBetween(brewDate, r.date)) + "," +
               (r.temperatureC ? shortest(*r.temperatureC) : "") + "," + (r.gravity ? shortest(*r.gravity) : "") +
               "\n";
    }
    return out;
}

Parsed parse(const std::string& text) {
    Parsed parsed;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (trimmed(line).empty()) {
            continue;
        }
        const std::vector<std::string> fields = splitFields(line);
        if (fields[0] == "date") {
            continue;
        }
        const auto date = fields.size() >= 4 ? dateFrom(fields[0]) : std::nullopt;
        if (!date) {
            ++parsed.skipped;
            continue;
        }
        FermentationReading reading;
        reading.date = *date;
        reading.temperatureC = parseNumber(fields[2]);
        reading.gravity = parseNumber(fields[3]);
        parsed.readings.push_back(reading);
    }
    return parsed;
}

}  // namespace kettlelogic::app::ReadingsCsv
