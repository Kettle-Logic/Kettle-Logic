#pragma once

#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/model/model.hpp"

#include <filesystem>
#include <string>

// Draws a recipe as a PDF to read, print, and share: its estimates, ingredients, mash, fermentation, and notes, over as
// many US Letter pages as it needs. It is not a brew sheet. It has no boxes to write in and no codes to type back into
// the app; the brew sheet (brew_sheet_pdf.hpp) is the form a brewer fills in on brew day. The engine draws the page
// itself, so every platform's PDF is the same.
namespace kettlelogic::io {

// The PDF file's bytes. `document` is for the recipe's BJCP style, when it has one that the document defines.
std::string recipePdf(const model::Document& document, const model::Recipe& recipe, const calc::Results& results);

// Writes the PDF to `file`. Throws std::runtime_error with a message the user can read when the file cannot be written.
void writeRecipePdf(const model::Document& document, const model::Recipe& recipe, const calc::Results& results,
                    const std::filesystem::path& file);

}  // namespace kettlelogic::io
