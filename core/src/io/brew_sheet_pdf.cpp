#include "kettlelogic/io/brew_sheet_pdf.hpp"

#include "pdf_writer.hpp"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace kettlelogic::io {

namespace {

constexpr double kPageHeight = 792;
constexpr double kMargin = 40;
constexpr double kRow = 24;       // one measurement, with its box
constexpr double kBoxHeight = 18;
constexpr double kBoxWidth = 120;
constexpr double kExpectX = 296;  // the recipe's figure
constexpr double kBoxX = 420;     // the box the brewer writes in

// The readings table's columns.
constexpr double kDateX = 170;
constexpr double kTempX = 300;
constexpr double kGravityX = 430;
constexpr double kReadingBoxWidth = 120;

using Canvas = pdf::Page;

// The code printed in the box's top-left corner, small, so the brewer can find the box in the app.
void codeBox(Canvas& canvas, double x, double top, double width, const std::string& code) {
    canvas.box(x, top, width, kBoxHeight);
    canvas.text(x + 3, top + 2, 6.5, false, code, 0.45);
}

void sectionHeading(Canvas& canvas, double top, const calc::BrewSheetSection& section) {
    canvas.text(kMargin, top, 10, true, section.code + "   " + section.title);
    canvas.rule(top + 16);
}

}  // namespace

std::string brewSheetPdf(const calc::BrewSheet& sheet) {
    Canvas canvas;
    const bool imperial = sheet.units == calc::SheetUnits::Imperial;

    canvas.text(kMargin, 34, 20, true, "Brew Sheet");
    canvas.text(kMargin, 60, 13, true, sheet.recipeName);
    std::string meta;
    if (!sheet.batchText.empty()) meta += "Batch " + sheet.batchText + "   |   ";
    meta += imperial ? "Imperial units" : "Metric units";
    canvas.text(kMargin, 80, 9, false, meta, 0.3);
    canvas.text(kMargin, 94, 8, false,
                "Write each value in pencil in its box, and dates as yyyy-mm-dd. Type each value into Kettle Logic under its code.",
                0.3);

    double y = 118;
    canvas.text(kMargin + 8, y, 7.5, true, "MEASUREMENT", 0.3);
    canvas.text(kExpectX, y, 7.5, true, "RECIPE EXPECTS", 0.3);
    canvas.text(kBoxX, y, 7.5, true, "MEASURED", 0.3);
    y += 16;

    for (const calc::BrewSheetSection& section : sheet.sections) {
        sectionHeading(canvas, y, section);
        y += 22;

        if (section.code != "F") {
            for (const calc::BrewSheetBox& b : section.boxes) {
                canvas.text(kMargin + 8, y + 5, 9, false, b.label);
                if (!b.expectedText.empty()) {
                    canvas.text(kExpectX, y + 5, 9, false, b.expectedText + " " + b.unit, 0.3);
                }
                codeBox(canvas, kBoxX, y + 2, kBoxWidth, b.code);
                // A date's format is in the instructions, which keeps the unit inside the margin.
                if (b.kind != calc::BoxKind::Date) canvas.text(kBoxX + kBoxWidth + 6, y + 5, 8, false, b.unit, 0.3);
                y += kRow;
            }
            y += 8;
            continue;
        }

        // The readings table: a row for each reading, with its date, temperature, and gravity boxes.
        const std::string temperatureUnit = section.boxes.size() > 1 ? section.boxes[1].unit : "";
        canvas.text(kDateX, y - 4, 7.5, true, "DATE", 0.3);
        canvas.text(kTempX, y - 4, 7.5, true, "TEMPERATURE (" + temperatureUnit + ")", 0.3);
        canvas.text(kGravityX, y - 4, 7.5, true, "GRAVITY (SG)", 0.3);
        y += 8;
        for (std::size_t i = 0; i < section.boxes.size(); i += 3) {
            canvas.text(kMargin + 8, y + 5, 9, false, "Reading " + std::to_string(i / 3 + 1));
            codeBox(canvas, kDateX, y + 2, kReadingBoxWidth, section.boxes[i].code);
            codeBox(canvas, kTempX, y + 2, kReadingBoxWidth, section.boxes[i + 1].code);
            codeBox(canvas, kGravityX, y + 2, kReadingBoxWidth, section.boxes[i + 2].code);
            y += kRow;
        }
        y += 8;
    }

    canvas.text(kMargin, kPageHeight - 40, 7, false,
                "Kettle Logic brew sheet. Read it back with Brew Journal, Enter Brew Sheet.", 0.4);

    return pdf::assemble({canvas.ops()}, "Brew sheet: " + sheet.recipeName);
}

void writeBrewSheetPdf(const calc::BrewSheet& sheet, const std::filesystem::path& file) {
    const std::string bytes = brewSheetPdf(sheet);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("The brew sheet could not be written to " + file.string() + ".");
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        throw std::runtime_error("The brew sheet could not be written to " + file.string() + ".");
    }
}

}  // namespace kettlelogic::io
