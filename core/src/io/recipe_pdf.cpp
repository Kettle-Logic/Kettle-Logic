#include "kettlelogic/io/recipe_pdf.hpp"

#include "pdf_writer.hpp"
#include "recipe_text.hpp"

#include "kettlelogic/calc/style.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using kettlelogic::Dimension;
using kettlelogic::Quantity;
using kettlelogic::Unit;
using kettlelogic::model::Recipe;

namespace kettlelogic::io {
namespace {

using pdf::Page;

constexpr double kMargin = 40;
constexpr double kRight = pdf::kPageWidth - kMargin;
constexpr double kWidth = kRight - kMargin;             // the width of the text column
constexpr double kFirstTop = 40;                        // where the first page's content starts
constexpr double kContinuedTop = 52;                    // below the running head of the pages after the first
constexpr double kBottom = pdf::kPageHeight - 60;       // nothing is set below this, which leaves room for the footer

constexpr double kBodySize = 9.5;
constexpr double kBodyLineHeight = 13;
constexpr double kCellSize = 8.5;
constexpr double kCellLineHeight = 11;
constexpr double kCellPadding = 4;
constexpr double kHeadingHeight = 30;                   // the space above a heading, the heading, and its rule
constexpr double kStatHeight = 40;                      // one row of estimates
constexpr double kLitersPerUsGallon = 3.785411784;
const char* const kMissing = "\xE2\x80\x94";            // an em dash, for a value that was not calculated or not given

std::string fixed(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

// A volume in liters, as gallons when the recipe is in US gallons and as liters otherwise.
std::string volume(double liters, bool gallons) {
    return gallons ? fixed(liters / kLitersPerUsGallon, 2) + " gal" : fixed(liters, 1) + " L";
}

// A word or phrase the recipe stores in BeerJSON's lower case, such as "add_to_boil", as it reads: "Add to boil".
std::string phrase(std::string text) {
    std::replace(text.begin(), text.end(), '_', ' ');
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') text[0] = static_cast<char>(text[0] - 'a' + 'A');
    return text;
}

// A value that may not have been calculated, shown with the given number of decimals and unit, or as a dash.
std::string valueOr(const std::optional<double>& value, int decimals, const std::string& unit) {
    return value ? fixed(*value, decimals) + unit : kMissing;
}

// A quantity, or a dash when the recipe does not give one.
template <typename Optional>
std::string optionalDisplay(const Optional& quantity) {
    return quantity ? display(*quantity) : kMissing;
}

// The IBU the engine calculated for a hop addition, when it did.
std::optional<double> ibuOf(const calc::Results& results, const Uuid& hop) {
    for (const calc::HopBitterness& entry : results.ibuByHop) {
        if (entry.hop == hop) return entry.ibu;
    }
    return std::nullopt;
}

// The page the recipe is laid out on, top to bottom. A block that does not fit on the current page starts a new one.
class Flow {
public:
    explicit Flow(std::string name) : name_(std::move(name)) {
        startPage();
    }

    // Starts a new page when `height` points below the cursor would run past the bottom of this one.
    void reserve(double height) {
        if (y_ + height > kBottom) startPage();
    }

    // Text from the left margin, wrapped to the column.
    void paragraph(const std::string& text, double size, bool bold, double gray, double lineHeight) {
        for (const std::string& row : pdf::wrap(text, kWidth, size, bold)) {
            reserve(lineHeight);
            page().text(kMargin, y_, size, bold, row, gray);
            y_ += lineHeight;
        }
    }

    void body(const std::string& text) {
        paragraph(text, kBodySize, false, 0, kBodyLineHeight);
        y_ += 6;
    }

    void rule() {
        reserve(12);
        page().rule(y_);
        y_ += 12;
    }

    // A section heading with a rule under it. `keep` is the height of the content that must follow it on the same
    // page, so a heading is never left alone at the foot of a page.
    void heading(const std::string& title, double keep) {
        reserve(kHeadingHeight + keep);
        drawHeading(title);
    }

    // Figures in a grid of four across, each with a small label above its value.
    struct Stat {
        std::string label;
        std::string value;
    };

    void stats(const std::vector<Stat>& cells) {
        const double cellWidth = kWidth / 4;
        for (std::size_t first = 0; first < cells.size(); first += 4) {
            reserve(kStatHeight);
            for (std::size_t i = first; i < std::min(first + 4, cells.size()); ++i) {
                const double x = kMargin + (i - first) * cellWidth;
                page().text(x, y_, 7, true, cells[i].label, 0.4);
                page().text(x, y_ + 11, 14, true, cells[i].value);
            }
            y_ += kStatHeight;
        }
        y_ += 4;
    }

    // A table under a heading. Its header repeats at the top of each page it runs onto, and it takes a line of
    // width per column from `widths`, which sum to the column's width.
    void table(const std::string& title, const std::vector<double>& widths, const std::vector<std::string>& header,
               const std::vector<std::vector<std::string>>& rows) {
        if (rows.empty()) return;
        const Cells head = cellsOf(widths, header, true);
        const Cells first = cellsOf(widths, rows.front(), false);
        reserve(kHeadingHeight + head.height + first.height);
        drawHeading(title);
        drawRow(widths, head, true);
        for (const std::vector<std::string>& row : rows) {
            const Cells cells = cellsOf(widths, row, false);
            if (y_ + cells.height > kBottom) {
                startPage();
                drawRow(widths, head, true);
            }
            drawRow(widths, cells, false);
        }
        y_ += 8;
    }

    // The pages, each with its footer and page number.
    std::vector<std::string> finish() {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < pages_.size(); ++i) {
            Page& page = pages_[i];
            const double top = pdf::kPageHeight - 36;
            page.text(kMargin, top, 7, false, "Kettle Logic recipe", 0.4);
            const std::string number = "Page " + std::to_string(i + 1) + " of " + std::to_string(pages_.size());
            page.text(kRight - pdf::textWidth(number, 7, false), top, 7, false, number, 0.4);
            out.push_back(page.ops());
        }
        return out;
    }

private:
    // A row's cells, each broken into the lines it needs, and the height of the tallest of them.
    struct Cells {
        std::vector<std::vector<std::string>> lines;
        double height = 0;
    };

    Cells cellsOf(const std::vector<double>& widths, const std::vector<std::string>& texts, bool bold) const {
        Cells cells;
        std::size_t most = 1;
        for (std::size_t i = 0; i < texts.size(); ++i) {
            cells.lines.push_back(pdf::wrap(texts[i], widths[i] - 2 * kCellPadding, kCellSize, bold));
            most = std::max(most, cells.lines.back().size());
        }
        cells.height = static_cast<double>(most) * kCellLineHeight + 2 * kCellPadding;
        return cells;
    }

    void drawRow(const std::vector<double>& widths, const Cells& cells, bool header) {
        if (header) page().fill(kMargin, y_, kWidth, cells.height, 0.93);
        double x = kMargin;
        for (std::size_t i = 0; i < cells.lines.size(); ++i) {
            for (std::size_t k = 0; k < cells.lines[i].size(); ++k) {
                page().text(x + kCellPadding, y_ + kCellPadding + static_cast<double>(k) * kCellLineHeight, kCellSize,
                            header, cells.lines[i][k], header ? 0.25 : 0);
            }
            x += widths[i];
        }
        page().rule(y_ + cells.height, kMargin, kRight);
        y_ += cells.height;
    }

    void drawHeading(const std::string& title) {
        y_ += 8;
        page().text(kMargin, y_, 11, true, title);
        page().rule(y_ + 15);
        y_ += kHeadingHeight - 8;
    }

    Page& page() { return pages_.back(); }

    // Every page after the first carries the recipe's name at its top, so a loose page still says what it is.
    void startPage() {
        pages_.emplace_back();
        if (pages_.size() > 1) {
            page().text(kMargin, 22, 8, false, name_, 0.4);
            page().rule(40);
            y_ = kContinuedTop;
        } else {
            y_ = kFirstTop;
        }
    }

    std::string name_;
    std::vector<Page> pages_;
    double y_ = 0;
};

}  // namespace

std::string recipePdf(const model::Document& document, const Recipe& recipe, const calc::Results& results) {
    const bool gallons = recipe.batchSize.unit() == Unit::Gallon;
    Flow flow(recipe.name);

    flow.paragraph(recipe.name, 22, true, 0, 28);
    std::vector<std::string> parts = {phrase(std::string(model::recipeTypeName(recipe.type)))};
    if (!recipe.author.empty()) parts.push_back("By " + recipe.author);
    if (recipe.bjcpStyle) {
        const std::optional<calc::StyleRef> style = calc::findStyle(document, *recipe.bjcpStyle);
        parts.push_back(style ? style->name : *recipe.bjcpStyle);
    }
    std::string subtitle;
    for (const std::string& part : parts) subtitle += (subtitle.empty() ? "" : "   |   ") + part;
    flow.paragraph(subtitle, 9.5, false, 0.3, 13);
    flow.rule();

    flow.heading("Estimates", 2 * kStatHeight);
    flow.stats({
        {"ORIGINAL GRAVITY", valueOr(results.originalGravity, 3, "")},
        {"FINAL GRAVITY", valueOr(results.finalGravity, 3, "")},
        {"ALCOHOL", valueOr(results.abv, 1, " %")},
        {"BITTERNESS", valueOr(results.ibu, 0, " IBU")},
        {"COLOR", valueOr(results.srm, 1, " SRM")},
        {"PRE-BOIL VOLUME", results.preBoilLiters ? volume(*results.preBoilLiters, gallons) : kMissing},
        {"POST-BOIL VOLUME", results.postBoilLiters ? volume(*results.postBoilLiters, gallons) : kMissing},
        {"BATCH SIZE", volume(recipe.batchSize.valueIn(Unit::Liter).toDouble(), gallons)},
    });

    if (!recipe.fermentables.empty()) {
        double grist = 0;
        for (const model::Fermentable& f : recipe.fermentables) {
            if (f.amount.dimension() == Dimension::Mass) grist += f.amount.valueIn(Unit::Kilogram).toDouble();
        }
        std::vector<std::vector<std::string>> rows;
        for (const model::Fermentable& f : recipe.fermentables) {
            const bool weighed = f.amount.dimension() == Dimension::Mass && grist > 0;
            rows.push_back({display(f.amount), f.name, phrase(f.type),
                            weighed ? fixed(f.amount.valueIn(Unit::Kilogram).toDouble() / grist * 100, 1) + " %"
                                    : kMissing});
        }
        flow.table("Fermentables", {90, 232, 110, 100}, {"AMOUNT", "INGREDIENT", "TYPE", "SHARE OF GRIST"}, rows);
    }

    if (recipe.hops && !recipe.hops->empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const model::Hop& hop : *recipe.hops) {
            const std::optional<double> ibu = ibuOf(results, hop.uuid);
            rows.push_back({display(hop.amount), hop.name, display(hop.alphaAcid), optionalDisplay(hop.timing.time),
                            hop.timing.use ? phrase(*hop.timing.use) : kMissing, valueOr(ibu, 1, "")});
        }
        flow.table("Hops", {78, 172, 62, 70, 90, 60}, {"AMOUNT", "HOP", "ALPHA", "TIME", "USE", "IBU"}, rows);
    }

    if (recipe.miscellaneous && !recipe.miscellaneous->empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const model::Miscellaneous& misc : *recipe.miscellaneous) {
            rows.push_back({display(misc.amount), misc.name, phrase(misc.type),
                            optionalDisplay(misc.timing ? misc.timing->time : std::nullopt),
                            misc.timing && misc.timing->use ? phrase(*misc.timing->use) : kMissing});
        }
        flow.table("Miscellaneous", {78, 200, 110, 70, 74}, {"AMOUNT", "INGREDIENT", "TYPE", "TIME", "USE"}, rows);
    }

    if (recipe.cultures && !recipe.cultures->empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const model::Culture& culture : *recipe.cultures) {
            rows.push_back({culture.name, phrase(culture.type), phrase(culture.form),
                            optionalDisplay(culture.attenuation)});
        }
        flow.table("Yeast", {200, 110, 100, 122}, {"YEAST", "TYPE", "FORM", "ATTENUATION"}, rows);
    }

    if (recipe.waters && !recipe.waters->empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const model::WaterAddition& addition : *recipe.waters) {
            rows.push_back({addition.water.name, optionalDisplay(addition.amount)});
        }
        flow.table("Water additions", {400, 132}, {"WATER", "AMOUNT"}, rows);
    }

    if (recipe.mash && !recipe.mash->steps.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const model::MashStep& step : recipe.mash->steps) {
            rows.push_back({step.name, phrase(step.type), display(step.stepTemperature), display(step.stepTime),
                            optionalDisplay(step.amount)});
        }
        flow.table("Mash", {150, 90, 100, 90, 102}, {"STEP", "TYPE", "TEMPERATURE", "TIME", "WATER"}, rows);
        flow.body("Grain temperature: " + display(recipe.mash->grainTemperature) + ".");
        if (recipe.mash->notes && !recipe.mash->notes->empty()) flow.body(*recipe.mash->notes);
    }

    if (recipe.fermentation && !recipe.fermentation->steps.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const model::FermentationStep& step : recipe.fermentation->steps) {
            std::string temperature = kMissing;
            if (step.startTemperature && step.endTemperature) {
                temperature = display(*step.startTemperature) + " to " + display(*step.endTemperature);
            } else if (step.startTemperature || step.endTemperature) {
                temperature = display(step.startTemperature ? *step.startTemperature : *step.endTemperature);
            }
            rows.push_back({step.name, temperature, optionalDisplay(step.stepTime),
                            step.vessel ? *step.vessel : kMissing});
        }
        flow.table("Fermentation", {150, 170, 100, 112}, {"STEP", "TEMPERATURE", "TIME", "VESSEL"}, rows);
    }

    if (recipe.boil && recipe.boil->notes && !recipe.boil->notes->empty()) {
        flow.heading("Boil", kBodyLineHeight);
        flow.body(*recipe.boil->notes);
    }

    if (recipe.notes && !recipe.notes->empty()) {
        flow.heading("Notes", kBodyLineHeight);
        flow.body(*recipe.notes);
    }

    const std::string title = "Recipe: " + recipe.name;
    return pdf::assemble(flow.finish(), title);
}

void writeRecipePdf(const model::Document& document, const Recipe& recipe, const calc::Results& results,
                    const std::filesystem::path& file) {
    const std::string bytes = recipePdf(document, recipe, results);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("The recipe could not be written to " + file.string() + ".");
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        throw std::runtime_error("The recipe could not be written to " + file.string() + ".");
    }
}

}  // namespace kettlelogic::io
