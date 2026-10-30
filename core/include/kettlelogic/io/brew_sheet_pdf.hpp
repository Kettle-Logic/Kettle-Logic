#pragma once

#include "kettlelogic/calc/brew_sheet.hpp"

#include <filesystem>
#include <string>

// Draws a brew sheet as a one-page US Letter PDF, with a box for each value and its code printed in the box. The
// engine draws the page itself, so every platform prints the same sheet. Text is set in Helvetica, so the sheet
// needs no fonts from the system.
namespace kettlelogic::io {

// The PDF file's bytes.
std::string brewSheetPdf(const calc::BrewSheet& sheet);

// Writes the PDF to `file`. Throws std::runtime_error with a message the user can read when the file cannot be written.
void writeBrewSheetPdf(const calc::BrewSheet& sheet, const std::filesystem::path& file);

}  // namespace kettlelogic::io
