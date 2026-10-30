#pragma once

#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/styles/style.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Beer style comparison. A style gives a range for each vital statistic. A recipe's
// calculated values are placed within those ranges, so the app can show how far each
// one falls. Styles are either the BJCP 2021 Beer Style Guidelines, which are in
// style.cpp, or styles the user defined, which are kept in the document.
namespace kettlelogic::calc {

using Range = styles::Range;

struct BjcpStyle {
    std::string_view id;    // the guideline number and letter, such as "21A"
    std::string_view name;  // such as "American IPA"
    Range og;               // original gravity, specific gravity
    Range fg;               // final gravity, specific gravity
    Range ibu;              // bitterness
    Range srm;              // color
    Range abv;              // alcohol, percent by volume
};

enum class Metric { OriginalGravity, FinalGravity, Abv, Ibu, Srm };

// Where a value falls against its range. Unknown means the recipe has no value.
enum class Fit { Below, Within, Above, Unknown };

struct Check {
    Metric metric = Metric::OriginalGravity;
    std::optional<double> value;
    Range range;
    Fit fit = Fit::Unknown;
};

// Every style in the table, in guideline order.
std::span<const BjcpStyle> bjcpStyles();

// The style with this guideline id, or nullptr if there is none.
const BjcpStyle* findBjcpStyle(std::string_view id);

// A style a recipe can be compared against: a guideline style or a user style.
struct StyleRef {
    std::string id;
    std::string name;
    styles::Ranges ranges;
};

// The style with this id, from the guideline table or the document's user styles, or nothing.
std::optional<StyleRef> findStyle(const model::Document& document, std::string_view id);

// The recipe's original gravity, final gravity, alcohol, bitterness, and color
// placed against the style's ranges, in that order.
std::vector<Check> compareToStyle(const Results& results, const BjcpStyle& style);
std::vector<Check> compareToStyle(const Results& results, const styles::Ranges& ranges);

}  // namespace kettlelogic::calc
