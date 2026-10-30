#pragma once

#include "kettlelogic/app/planning.hpp"

#include <string>
#include <vector>

namespace kettlelogic::app {

// Fermentation readings as CSV. The file has a header, then one row per reading:
// `date,day,temperature_c,gravity_sg`, with the date as yyyy-MM-dd and the day counted from brew day.
// The day column is worked out on export and ignored on import.
namespace ReadingsCsv {

extern const char* const header;

std::string exportReadings(std::vector<FermentationReading> readings, double brewDate);

struct Parsed {
    std::vector<FermentationReading> readings;
    int skipped = 0;  // the rows that could not be read
};
Parsed parse(const std::string& text);

}  // namespace ReadingsCsv
}  // namespace kettlelogic::app
